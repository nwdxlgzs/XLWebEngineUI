#include "box_paint.h"
#include "document/document.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/effects/SkGradient.h"
#include "include/effects/SkRuntimeEffect.h"
#include "sk_rounded.h"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace xl::detail::cssbackground {
struct Paint::Cache {
  struct Entry {
    const Gradient *image;
    float width, height, cell_x, cell_y;
    SkTileMode x, y;
    bool bounded;
    sk_sp<SkShader> shader;
    size_t bytes;
  };
  std::vector<Entry> entries;
  size_t bytes = 0;
};
} // namespace xl::detail::cssbackground
namespace xl::detail {
namespace {
using namespace cssbackground;
struct Sample {
  double position;
  csscolor::Value color;
};
struct Colors {
  std::vector<SkColor4f> values;
  std::vector<float> positions;
};
SkColor4f output(csscolor::Value value) {
  // Gradients retain floating-point colors until destination conversion,
  // like Skia's native interpolation shaders. Do not quantize color stops
  // through the solid-color ARGB accessor before interpolating them.
  for (unsigned i = 0; i < 3; ++i)
    if (value.missing & (1u << i))
      value.channels[i] = 0;
  if (value.missing & 8)
    value.alpha = 0;
  value.missing = 0;
  const auto c = csscolor::convert(value, csscolor::Space::srgb);
  return {float(std::clamp(c.channels[0], 0., 1.)),
          float(std::clamp(c.channels[1], 0., 1.)),
          float(std::clamp(c.channels[2], 0., 1.)), float(c.alpha)};
}
std::vector<double> positions(const Gradient &image, double basis,
                              const Environment &environment) {
  std::vector<double> values;
  values.reserve(image.stops.size());
  for (const auto &stop : image.stops)
    values.push_back(stop.position
                         ? resolve(*stop.position, basis, environment) / basis
                         : NAN);
  if (std::isnan(values.front()))
    values.front() = 0;
  if (std::isnan(values.back()))
    values.back() = 1;
  double previous = values.front();
  for (auto &value : values)
    if (std::isfinite(value)) {
      value = std::max(value, previous);
      previous = value;
    }
  for (size_t i = 1; i < values.size(); ++i)
    if (std::isnan(values[i])) {
      const auto begin = i - 1;
      size_t end = i;
      while (end < values.size() && std::isnan(values[end]))
        ++end;
      for (size_t j = i; j < end; ++j)
        values[j] = values[begin] + (values[end] - values[begin]) *
                                        double(j - begin) / double(end - begin);
      i = end;
    }
  return values;
}
csscolor::Value blend(const Gradient &image, const Environment &environment,
                      size_t a, size_t b, double t, double hint) {
  double weight = t;
  if (std::isfinite(hint) && std::abs(hint - .5) > 1e-9) {
    if (hint <= 0)
      weight = t <= 0 ? 0 : 1;
    else if (hint >= 1)
      weight = t >= 1 ? 1 : 0;
    else
      weight = std::pow(std::clamp(t, 0., 1.), std::log(.5) / std::log(hint));
  }
  return csscolor::interpolate(
      csscolor::resolve(image.stops[a].color, environment.color),
      csscolor::resolve(image.stops[b].color, environment.color), image.space,
      weight, image.hue);
}
csscolor::Value average(const Gradient &image, const Environment &environment) {
  csscolor::Value result;
  size_t n = 0;
  for (const auto &stop : image.stops)
    if (stop.color) {
      const auto c = csscolor::resolve(stop.color, environment.color);
      result = n ? csscolor::interpolate(result, c, image.space,
                                         1. / double(n + 1), image.hue)
                 : c;
      ++n;
    }
  return result;
}
Colors samples(const Gradient &image, const Environment &environment,
               const std::vector<double> &stops, double lo, double hi) {
  Colors out;
  const auto append = [&](double position, SkColor4f color) {
    if (out.values.size() >= 8192)
      throw DocumentFailure{XL_UNSUPPORTED};
    out.positions.push_back(
        float(std::clamp((position - lo) / (hi - lo), 0., 1.)));
    out.values.push_back(color);
  };
  std::vector<size_t> colors;
  for (size_t i = 0; i < image.stops.size(); ++i)
    if (image.stops[i].color)
      colors.push_back(i);
  if (lo < stops.front())
    append(lo, output(csscolor::resolve(image.stops.front().color,
                                        environment.color)));
  for (size_t j = 1; j < colors.size(); ++j) {
    const auto a = colors[j - 1], b = colors[j];
    const auto first = stops[a], last = stops[b];
    if (last < lo || first > hi)
      continue;
    if (first == last) {
      if (first >= lo && first <= hi) {
        append(first, output(csscolor::resolve(image.stops[a].color,
                                               environment.color)));
        append(last, output(csscolor::resolve(image.stops[b].color,
                                              environment.color)));
      }
      continue;
    }
    const auto hint = b > a + 1 ? (stops[a + 1] - first) / (last - first) : NAN;
    const auto start = std::max(lo, first), finish = std::min(hi, last);
    const auto value = [&](double p) {
      return output(
          blend(image, environment, a, b, (p - first) / (last - first), hint));
    };
    const auto beginning = value(start), ending = value(finish);
    append(start, beginning);
    const auto refine = [&](auto &&self, double x, SkColor4f left, double y,
                            SkColor4f right, unsigned depth) -> void {
      const auto error = [&](double t) {
        const auto actual = value(x + (y - x) * t);
        double worst = 0;
        for (int channel = 0; channel < 4; ++channel) {
          const double actual_value =
              actual[channel] * (channel == 3 ? 1 : actual.fA);
          const double expected =
              left[channel] * (channel == 3 ? 1 : left.fA) * (1 - t) +
              right[channel] * (channel == 3 ? 1 : right.fA) * t;
          worst = std::max(worst, std::abs(actual_value - expected));
        }
        return worst;
      };
      if (depth < 12 && std::max({error(.25), error(.5), error(.75)}) > .0008) {
        const auto middle = (x + y) * .5;
        const auto color = value(middle);
        self(self, x, left, middle, color, depth + 1);
        self(self, middle, color, y, right, depth + 1);
      } else
        append(y, right);
    };
    refine(refine, start, beginning, finish, ending, 0);
  }
  if (hi > stops.back())
    append(hi, output(csscolor::resolve(image.stops.back().color,
                                        environment.color)));
  if (out.values.empty()) {
    append(lo, output(csscolor::resolve(image.stops.back().color,
                                        environment.color)));
    append(hi, out.values.back());
  }
  if (out.values.size() == 1)
    append(hi, out.values.front());
  return out;
}
sk_sp<SkShader> gradient_shader(const Gradient &image, float width,
                                float height, const Environment &environment,
                                size_t &bytes) {
  double dx = 0, dy = 1, line = 1, cx = 0, cy = 0, rx = 0, ry = 0;
  const auto coordinate = [&](const Axis &axis, double extent) {
    const auto n = resolve(axis.offset, extent, environment);
    return axis.end ? extent - n : n;
  };
  if (image.kind == Gradient::linear) {
    if (image.direction_x || image.direction_y) {
      dx = image.direction_x;
      dy = image.direction_y;
      if (dx && dy) {
        dx *= height;
        dy *= width;
      }
      const auto length = std::hypot(dx, dy);
      dx /= length;
      dy /= length;
    } else {
      const auto angle = image.angle * std::numbers::pi / 180;
      dx = std::sin(angle);
      dy = -std::cos(angle);
    }
    line = std::abs(width * dx) + std::abs(height * dy);
  } else {
    cx = coordinate(image.center.x, width);
    cy = coordinate(image.center.y, height);
    if (image.kind == Gradient::radial) {
      if (!image.radii.empty()) {
        rx = resolve(image.radii[0], width, environment);
        ry = image.circle ? rx : resolve(image.radii[1], height, environment);
      } else {
        const bool closest = image.extent < 2;
        const auto horizontal =
            closest ? std::min(std::abs(cx), std::abs(width - cx))
                    : std::max(std::abs(cx), std::abs(width - cx));
        const auto vertical =
            closest ? std::min(std::abs(cy), std::abs(height - cy))
                    : std::max(std::abs(cy), std::abs(height - cy));
        if (image.circle) {
          if (image.extent % 2 == 0)
            rx = closest ? std::min(horizontal, vertical)
                         : std::max(horizontal, vertical);
          else {
            rx = closest ? INFINITY : 0;
            for (double x : {0., double(width)})
              for (double y : {0., double(height)}) {
                const auto d = std::hypot(x - cx, y - cy);
                rx = closest ? std::min(rx, d) : std::max(rx, d);
              }
          }
          ry = rx;
        } else {
          rx = horizontal;
          ry = vertical;
          if (image.extent % 2 && rx > 0 && ry > 0) {
            double factor = closest ? INFINITY : 0;
            for (double x : {0., double(width)})
              for (double y : {0., double(height)}) {
                const auto d = std::hypot((x - cx) / rx, (y - cy) / ry);
                factor = closest ? std::min(factor, d) : std::max(factor, d);
              }
            rx *= factor;
            ry *= factor;
          }
        }
      }
      line = rx;
    }
  }
  if (!std::isfinite(line) || line <= 1e-12 ||
      (image.kind == Gradient::radial && ry <= 1e-12))
    return SkShaders::Color(
        output(image.repeating ? average(image, environment)
                               : csscolor::resolve(image.stops.back().color,
                                                   environment.color)),
        SkColorSpace::MakeSRGB());
  auto offsets = positions(image, line, environment);
  double lo = offsets.front(), hi = offsets.back();
  if (image.repeating && hi - lo <= 1e-12)
    return SkShaders::Color(output(average(image, environment)),
                            SkColorSpace::MakeSRGB());
  bool step = hi == lo;
  if (step)
    hi = lo + 1;
  if (image.kind == Gradient::radial && !image.repeating) {
    if (hi <= 0)
      return SkShaders::Color(output(csscolor::resolve(image.stops.back().color,
                                                       environment.color)),
                              SkColorSpace::MakeSRGB());
    lo = std::max(0., lo);
  }
  auto colors = samples(image, environment, offsets, lo, hi);
  if (step) {
    colors.values = {
        output(csscolor::resolve(image.stops.front().color, environment.color)),
        output(csscolor::resolve(image.stops.back().color, environment.color))};
    colors.positions = {0, 0};
  }
  bytes = colors.values.size() * (sizeof(SkColor4f) + sizeof(float)) + 1024;
  SkGradient::Colors data(colors.values, colors.positions,
                          image.repeating ? SkTileMode::kRepeat
                                          : SkTileMode::kClamp,
                          SkColorSpace::MakeSRGB());
  SkGradient gradient(data, {SkGradient::Interpolation::InPremul::kYes,
                             SkGradient::Interpolation::ColorSpace::kSRGB,
                             SkGradient::Interpolation::HueMethod::kShorter});
  if (image.kind == Gradient::linear) {
    const double x = width * .5 - dx * line * .5,
                 y = height * .5 - dy * line * .5;
    const SkPoint points[]{
        {float(x + lo * line * dx), float(y + lo * line * dy)},
        {float(x + hi * line * dx), float(y + hi * line * dy)}};
    return SkShaders::LinearGradient(points, gradient);
  }
  if (image.kind == Gradient::radial) {
    if (image.repeating) {
      const auto period = hi - lo;
      lo -= std::floor(lo / period) * period;
      hi = lo + period;
    }
    SkMatrix matrix;
    matrix.setScale(1, float(ry / rx));
    matrix.postTranslate(float(cx), float(cy));
    return SkShaders::TwoPointConicalGradient(
        {0, 0}, float(lo * rx), {0, 0}, float(hi * rx), gradient, &matrix);
  }
  SkMatrix matrix;
  matrix.setRotate(float(image.angle - 90));
  matrix.postTranslate(float(cx), float(cy));
  return SkShaders::SweepGradient({0, 0}, float(lo * 360), float(hi * 360),
                                  gradient, &matrix);
}
RoundedRect area(const BoxPaint &box, Box type) {
  RoundedRect outer{{box.rect.x, box.rect.y, box.rect.width, box.rect.height},
                    box.radii};
  if (type == Box::border)
    return outer;
  auto value = outer.inset(box.borders[0].width, box.borders[1].width,
                           box.borders[2].width, box.borders[3].width);
  if (type == Box::content && box.background) {
    const auto &p = box.background->padding;
    value = value.inset(p[0], p[1], p[2], p[3]);
  }
  return value;
}
void clip(SkCanvas *canvas, const RoundedRect &shape) {
  if (shape.curved())
    canvas->clipRRect(sk_rounded(shape), true);
  else
    canvas->clipRect(SkRect::MakeXYWH(shape.rect.x, shape.rect.y,
                                      shape.rect.width, shape.rect.height));
}
sk_sp<SkShader> tile(const Paint &paint, const Gradient &image, float width,
                     float height, float cell_x, float cell_y, SkTileMode x,
                     SkTileMode y,bool bounded) {
  if (!paint.cache)
    paint.cache = std::make_shared<Paint::Cache>();
  auto &cache = *paint.cache;
  for (const auto &entry : cache.entries)
    if (entry.image == &image && entry.width == width &&
        entry.height == height && entry.cell_x == cell_x &&
        entry.cell_y == cell_y && entry.x == x && entry.y == y&&entry.bounded==bounded)
      return entry.shader;
  size_t bytes = 0;
  auto shader = gradient_shader(image, width, height, paint.environment, bytes);
  if (!shader)
    throw DocumentFailure{XL_INTERNAL_ERROR};
  // Repeating coordinates stay procedural: a large CSS tile must never
  // allocate a width*height intermediate bitmap on CPU or GPU.
  if(!bounded) {
  static const auto effect = [] {
    auto result = SkRuntimeEffect::MakeForShader(SkString(R"(
            uniform shader image;
            uniform float2 tileSize;
            uniform float2 cellSize;
            uniform int2 repeated;
            half4 main(float2 p) {
                if(repeated.x!=0) p.x=mod(p.x,cellSize.x);
                if(repeated.y!=0) p.y=mod(p.y,cellSize.y);
                if(p.x<0 || p.y<0 || p.x>=tileSize.x || p.y>=tileSize.y) return half4(0);
                return image.eval(p);
            }
        )"));
    if (!result.effect)
      throw DocumentFailure{XL_INTERNAL_ERROR};
    return result.effect;
  }();
  SkRuntimeShaderBuilder builder(effect);
  builder.child("image") = shader;
  builder.uniform("tileSize") = std::array<float, 2>{width, height};
  builder.uniform("cellSize") = std::array<float, 2>{cell_x, cell_y};
  builder.uniform("repeated") =
      std::array<int, 2>{x == SkTileMode::kRepeat, y == SkTileMode::kRepeat};
  shader = builder.makeShader();
  if (!shader)
    throw DocumentFailure{XL_INTERNAL_ERROR};
  }
  while (!cache.entries.empty() && (cache.entries.size() >= 64 ||
                                    cache.bytes + bytes > 4 * 1024 * 1024)) {
    cache.bytes -= cache.entries.front().bytes;
    cache.entries.erase(cache.entries.begin());
  }
  cache.bytes += bytes;
  cache.entries.push_back(
      {&image, width, height, cell_x, cell_y, x, y, bounded,shader, bytes});
  return shader;
}
} // namespace
bool background_equal(const BoxPaint &a, const BoxPaint &b) {
  return cssbackground::equal(a.background, b.background) &&
         a.background_local.x == b.background_local.x &&
         a.background_local.y == b.background_local.y &&
         a.background_local.width == b.background_local.width &&
         a.background_local.height == b.background_local.height &&
         a.background_reference.x == b.background_reference.x &&
         a.background_reference.y == b.background_reference.y &&
         a.background_reference.width == b.background_reference.width &&
         a.background_reference.height == b.background_reference.height;
}
void paint_background(SkCanvas *canvas, const BoxPaint &box) {
  using namespace cssbackground;
  static const Style defaults;
  const auto &style = box.background ? box.background->style : defaults;
  SkPaint brush;
  if (box.rect.argb) {
    SkAutoCanvasRestore restore(canvas, true);
    clip(canvas, area(box, box.background_canvas_node
                               ? Box::border
                               : style.clips[(style.images.size() - 1) %
                                             style.clips.size()]));
    brush.setColor(box.rect.argb);
    canvas->drawRect(SkRect::MakeXYWH(box.rect.x, box.rect.y, box.rect.width,
                                      box.rect.height),
                     brush);
  }
  if (!box.background)
    return;
  const auto &environment = box.background->environment;
  brush.setColor(SK_ColorWHITE);
  for (size_t remaining = style.images.size(); remaining > 0; --remaining) {
    const auto i = remaining - 1;
    if (!style.images[i])
      continue;
    auto reference = area(box, style.origins[i % style.origins.size()]).rect;
    if (box.background_canvas_node && box.background_reference.width > 0 &&
        box.background_reference.height > 0) {
      auto copy = box;
      copy.background_canvas_node = 0;
      copy.rect.x = box.background_reference.x;
      copy.rect.y = box.background_reference.y;
      copy.rect.width = box.background_reference.width;
      copy.rect.height = box.background_reference.height;
      for (size_t side = 0; side < 4; ++side)
        copy.borders[side].width = box.background->border[side];
      reference = area(copy, style.origins[i % style.origins.size()]).rect;
    }
    const auto attach = style.attachments[i % style.attachments.size()];
    if (attach == Attachment::fixed) {
      SkMatrix inverse;
      if (!canvas->getLocalToDeviceAs3x3().invert(&inverse))
        continue;
      auto viewport = inverse.mapRect(
          SkRect::MakeWH(float(environment.vw), float(environment.vh)));
      reference = {viewport.x(), viewport.y(), viewport.width(),
                   viewport.height()};
    } else if (attach == Attachment::local && box.background_local.width > 0 &&
               box.background_local.height > 0) {
      reference.x -= box.background_local.x;
      reference.y -= box.background_local.y;
      reference.width +=
          box.background_local.width - area(box, Box::padding).rect.width;
      reference.height +=
          box.background_local.height - area(box, Box::padding).rect.height;
    }
    if (reference.width <= 0 || reference.height <= 0)
      continue;
    const auto &size = style.sizes[i % style.sizes.size()];
    const auto &position = style.positions[i % style.positions.size()];
    const auto &repeat = style.repeats[i % style.repeats.size()];
    double width =
        size.auto_x || size.fit
            ? reference.width
            : std::max(0., resolve(size.x, reference.width, environment));
    double height =
        size.auto_y || size.fit
            ? reference.height
            : std::max(0., resolve(size.y, reference.height, environment));
    if (width <= 0 || height <= 0 || !std::isfinite(width) ||
        !std::isfinite(height))
      continue;
    if (repeat[0] == Repeat::round)
      width =
          reference.width / std::max(1., std::round(reference.width / width));
    if (repeat[1] == Repeat::round)
      height = reference.height /
               std::max(1., std::round(reference.height / height));
    double cell_x = width, cell_y = height;
    const auto place = [&](const Axis &axis, double extent, double tile_size) {
      const auto value = resolve(axis.offset, extent - tile_size, environment);
      return axis.end ? extent - tile_size - value : value;
    };
    double x = reference.x + place(position.x, reference.width, width),
           y = reference.y + place(position.y, reference.height, height);
    SkTileMode modes[2]{SkTileMode::kRepeat, SkTileMode::kRepeat};
    const auto configure = [&](Repeat repeat, double extent, double tile_size,
                               double origin, double &cell, double &start,
                               size_t axis) {
      if (repeat == Repeat::none)
        modes[axis] = SkTileMode::kDecal;
      if (repeat == Repeat::space) {
        const auto n = std::floor(extent / tile_size);
        if (n >= 2) {
          cell = tile_size + (extent - n * tile_size) / (n - 1);
          start = origin;
        } else
          modes[axis] = SkTileMode::kDecal;
      }
    };
    configure(repeat[0], reference.width, width, reference.x, cell_x, x, 0);
    configure(repeat[1], reference.height, height, reference.y, cell_y, y, 1);
    auto visible=canvas->getLocalClipBounds();
    const auto paint_area=area(box,box.background_canvas_node?Box::border:style.clips[i%style.clips.size()]).rect;
    const bool nonempty_area=visible.intersect(SkRect::MakeXYWH(box.rect.x,box.rect.y,box.rect.width,box.rect.height))&&
        visible.intersect(SkRect::MakeXYWH(paint_area.x,paint_area.y,paint_area.width,paint_area.height));
    if(!nonempty_area)continue;
    // Pixel cells touching fractional/periodic tile boundaries retain the
    // procedural path. The native shader is equivalent only inside one tile.
    const bool bounded=std::floor(visible.left())>=x&&std::floor(visible.top())>=y&&
        std::ceil(visible.right())<=x+width&&std::ceil(visible.bottom())<=y+height;
    const auto shader =
        tile(*box.background, *style.images[i], float(width), float(height),
             float(cell_x), float(cell_y), modes[0], modes[1],bounded);
    SkMatrix matrix;
    matrix.setTranslate(float(x), float(y));
    brush.setShader(shader->makeWithLocalMatrix(matrix));
    SkAutoCanvasRestore restore(canvas, true);
    clip(canvas, area(box, box.background_canvas_node
                               ? Box::border
                               : style.clips[i % style.clips.size()]));
    canvas->drawRect(SkRect::MakeXYWH(box.rect.x, box.rect.y, box.rect.width,
                                      box.rect.height),
                     brush);
  }
}
} // namespace xl::detail
