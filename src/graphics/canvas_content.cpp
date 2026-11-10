#include "canvas_content.h"
#include "document/document.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPathUtils.h"
#include "include/core/SkPicture.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkDashPathEffect.h"
#include "include/effects/SkGradient.h"
#include "include/encode/SkPngEncoder.h"
#include "include/utils/SkPaintFilterCanvas.h"
#include "style/color.h"
#include "style/font_shorthand.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <numbers>

namespace xl::detail {
namespace {
constexpr size_t bitmap_limit = 64 * 1024 * 1024, command_limit = 65536,
                 command_bytes = 64 * 1024 * 1024;
SkMatrix make_matrix(std::span<const double> a) {
  return SkMatrix::MakeAll(float(a[0]), float(a[2]), float(a[4]), float(a[1]),
                           float(a[3]), float(a[5]), 0, 0, 1);
}
SkMatrix make_matrix(const std::array<float, 6> &a) {
  return SkMatrix::MakeAll(a[0], a[2], a[4], a[1], a[3], a[5], 0, 0, 1);
}
struct ClipPath {
  SkPath path;
  std::shared_ptr<const ClipPath> previous;
  size_t depth = 1;
};
struct Style {
  uint32_t color = 0xff000000;
  std::optional<CanvasGradient> gradient;
};
struct State {
  SkMatrix transform = SkMatrix::I();
  std::shared_ptr<const ClipPath> clip;
  Style fill, stroke;
  float alpha = 1, line_width = 1, miter = 10, dash_offset = 0;
  SkPaint::Cap cap = SkPaint::kButt_Cap;
  SkPaint::Join join = SkPaint::kMiter_Join;
  SkBlendMode blend = SkBlendMode::kSrcOver;
  TextFormat font = [] {
    TextFormat f;
    f.size = 10;
    f.families = {"sans-serif"};
    f.whitespace = WhiteSpace::pre;
    return f;
  }();
  std::vector<float> dash;
  std::string align = "start", baseline = "alphabetic";
  bool rtl = false;
};
struct Command {
  enum Kind { path, text, image } kind = path;
  State state;
  SkPath geometry;
  std::string words;
  float x = 0, y = 0, max_width = INFINITY;
  bool clear = false, stroke = false;
  bool raw = false;
  std::shared_ptr<const ImageDrawing> pixels;
  xl_node_bounds source{}, destination{};
  size_t bytes = 0;
};
class TextMask final : public SkPaintFilterCanvas {
  const State &state_;
  bool stroke_;

public:
  TextMask(SkCanvas *canvas, const State &state, bool stroke)
      : SkPaintFilterCanvas(canvas), state_(state), stroke_(stroke) {}
  bool onFilter(SkPaint &p) const override {
    p.setColor(SK_ColorBLACK);
    p.setShader(nullptr);
    p.setBlendMode(SkBlendMode::kSrcOver);
    if (stroke_) {
      p.setStyle(SkPaint::kStroke_Style);
      p.setStrokeWidth(state_.line_width);
      p.setStrokeJoin(state_.join);
      p.setStrokeCap(state_.cap);
      p.setStrokeMiter(state_.miter);
    }
    return true;
  }
};
sk_sp<SkShader> shader(const CanvasGradient &source) {
  if (source.stops.empty())
    return SkShaders::Color(SK_ColorTRANSPARENT);
  auto stops = source.stops;
  std::stable_sort(
      stops.begin(), stops.end(),
      [](const auto &a, const auto &b) { return a.first < b.first; });
  if (stops.size() == 1)
    stops.emplace_back(stops.front());
  std::vector<SkColor4f> colors;
  std::vector<float> positions;
  for (const auto &[offset, color] : stops) {
    colors.push_back(SkColor4f::FromColor(color));
    positions.push_back(offset);
  }
  SkGradient::Colors values({colors.data(), colors.size()},
                            {positions.data(), positions.size()},
                            SkTileMode::kClamp);
  SkGradient gradient(values, {SkGradient::Interpolation::InPremul::kNo,
                               SkGradient::Interpolation::ColorSpace::kSRGB});
  const auto m = make_matrix(source.matrix);
  const auto &p = source.points;
  sk_sp<SkShader> result;
  if (source.kind == CanvasGradient::linear) {
    if (p[0] == p[2] && p[1] == p[3])
      return SkShaders::Color(SK_ColorTRANSPARENT);
    SkPoint points[]{SkPoint::Make(p[0], p[1]), SkPoint::Make(p[2], p[3])};
    result = SkShaders::LinearGradient(points, gradient, &m);
  } else if (source.kind == CanvasGradient::radial) {
    if (p[0] == p[3] && p[1] == p[4] && p[2] == p[5])
      return SkShaders::Color(SK_ColorTRANSPARENT);
    result = SkShaders::TwoPointConicalGradient(
        {p[0], p[1]}, p[2], {p[3], p[4]}, p[5], gradient, &m);
  } else {
    auto rotated = m;
    rotated.preRotate(p[0] * 180 / std::numbers::pi_v<float>, p[1], p[2]);
    result = SkShaders::SweepGradient({p[1], p[2]}, gradient, &rotated);
  }
  return result ? result : SkShaders::Color(SK_ColorTRANSPARENT);
}
SkPaint paint(const State &state, bool stroke, bool clear = false) {
  SkPaint p;
  p.setAntiAlias(true);
  p.setBlendMode(clear ? SkBlendMode::kClear : state.blend);
  const auto &source = stroke ? state.stroke : state.fill;
  if (source.gradient) {
    p.setColor(SK_ColorBLACK);
    p.setShader(shader(*source.gradient));
  } else
    p.setColor(source.color);
  p.setAlphaf(p.getAlphaf() * state.alpha);
  if (stroke) {
    p.setStyle(SkPaint::kStroke_Style);
    p.setStrokeWidth(state.line_width);
    p.setStrokeCap(state.cap);
    p.setStrokeJoin(state.join);
    p.setStrokeMiter(state.miter);
    if (!state.dash.empty())
      p.setPathEffect(SkDashPathEffect::Make(
          {state.dash.data(), state.dash.size()}, state.dash_offset));
  }
  return p;
}
void clips(SkCanvas *canvas, const std::shared_ptr<const ClipPath> &value) {
  // Intersection is commutative; avoid recursion for author-controlled clip
  // depth.
  for (auto entry = value; entry; entry = entry->previous)
    canvas->clipPath(entry->path, true);
}
bool valid_size(uint32_t w, uint32_t h) {
  return uint64_t(w) * h <= bitmap_limit / 4 && w <= 32768 && h <= 32768;
}
} // namespace
struct CanvasContent::Impl {
  uint32_t width = 0, height = 0;
  State state;
  std::vector<State> stack;
  SkPathBuilder path;
  std::vector<Command> commands;
  size_t bytes = 0;
  bool needs_text = false, dirty = true;
  // Font leases outlive the bitmap's last text draw and immutable snapshots.
  std::shared_ptr<void> text_session;
  sk_sp<SkSurface> surface;
  std::shared_ptr<const ImageDrawing> snapshot;
  void push(Command value) {
    value.bytes = sizeof(Command) + value.words.size() +
                  value.geometry.approximateBytesUsed() +
                  value.state.dash.size() * sizeof(float);
    for (const auto &family : value.state.font.families)
      value.bytes += family.size();
    for (const auto *style : {&value.state.fill, &value.state.stroke})
      if (style->gradient)
        value.bytes +=
            style->gradient->stops.size() * sizeof(std::pair<float, uint32_t>);
    if (value.pixels && value.pixels->image)
      value.bytes += size_t(value.pixels->image->width()) *
                     value.pixels->image->height() * 4;
    if (commands.size() >= command_limit ||
        value.bytes > command_bytes - std::min(bytes, command_bytes))
      throw DocumentFailure{XL_OUT_OF_MEMORY};
    commands.push_back(std::move(value));
    bytes += commands.back().bytes;
    dirty = true;
  }
};
CanvasContent::CanvasContent(uint32_t w, uint32_t h)
    : impl_(std::make_unique<Impl>()) {
  reset(w, h);
}
CanvasContent::~CanvasContent() = default;
void CanvasContent::reset(uint32_t w, uint32_t h) {
  auto next = std::make_unique<Impl>();
  next->width = w;
  next->height = h;
  impl_ = std::move(next);
  ++generation_;
}
uint32_t CanvasContent::width() const { return impl_->width; }
uint32_t CanvasContent::height() const { return impl_->height; }
bool CanvasContent::needs_text() const { return impl_->needs_text; }
bool CanvasContent::pending() const { return !impl_->commands.empty(); }
size_t CanvasContent::retained_bytes() const {
  return impl_->bytes +
         (impl_->surface ? size_t(impl_->width) * impl_->height * 4 : 0);
}
std::array<float, 6> CanvasContent::matrix() const {
  const auto &m = impl_->state.transform;
  return {m.getScaleX(), m.getSkewY(),      m.getSkewX(),
          m.getScaleY(), m.getTranslateX(), m.getTranslateY()};
}
void CanvasContent::gradient(bool stroke, const CanvasGradient &value) {
  (stroke ? impl_->state.stroke : impl_->state.fill).gradient = value;
}
void CanvasContent::style(std::string_view name, std::string_view value) {
  auto &s = impl_->state;
  if (name == "fillStyle" || name == "strokeStyle") {
    if (const auto color = csscolor::parse(value)) {
      auto &v = name == "fillStyle" ? s.fill : s.stroke;
      v.color = csscolor::argb(csscolor::resolve(color));
      v.gradient.reset();
    }
  } else if (name == "font") {
    if (auto font = canvas_font(value))
      s.font = *font;
  } else if (name == "lineCap")
    s.cap = value == "round"    ? SkPaint::kRound_Cap
            : value == "square" ? SkPaint::kSquare_Cap
                                : SkPaint::kButt_Cap;
  else if (name == "lineJoin")
    s.join = value == "round"   ? SkPaint::kRound_Join
             : value == "bevel" ? SkPaint::kBevel_Join
                                : SkPaint::kMiter_Join;
  else if (name == "textAlign")
    s.align = value;
  else if (name == "textBaseline")
    s.baseline = value;
  else if (name == "direction")
    s.rtl = value == "rtl";
  else if (name == "globalCompositeOperation") {
    constexpr std::pair<std::string_view, SkBlendMode> modes[]{
        {"source-over", SkBlendMode::kSrcOver},
        {"source-in", SkBlendMode::kSrcIn},
        {"source-out", SkBlendMode::kSrcOut},
        {"source-atop", SkBlendMode::kSrcATop},
        {"destination-over", SkBlendMode::kDstOver},
        {"destination-in", SkBlendMode::kDstIn},
        {"destination-out", SkBlendMode::kDstOut},
        {"destination-atop", SkBlendMode::kDstATop},
        {"lighter", SkBlendMode::kPlus},
        {"copy", SkBlendMode::kSrc},
        {"xor", SkBlendMode::kXor},
        {"multiply", SkBlendMode::kMultiply},
        {"screen", SkBlendMode::kScreen},
        {"overlay", SkBlendMode::kOverlay},
        {"darken", SkBlendMode::kDarken},
        {"lighten", SkBlendMode::kLighten},
        {"color-dodge", SkBlendMode::kColorDodge},
        {"color-burn", SkBlendMode::kColorBurn},
        {"hard-light", SkBlendMode::kHardLight},
        {"soft-light", SkBlendMode::kSoftLight},
        {"difference", SkBlendMode::kDifference},
        {"exclusion", SkBlendMode::kExclusion},
        {"hue", SkBlendMode::kHue},
        {"saturation", SkBlendMode::kSaturation},
        {"color", SkBlendMode::kColor},
        {"luminosity", SkBlendMode::kLuminosity}};
    for (const auto &[text, mode] : modes)
      if (value == text)
        s.blend = mode;
  }
}
void CanvasContent::command(std::string_view op, std::span<const double> a) {
  auto &v = *impl_;
  auto &s = v.state;
  for (auto number : a)
    if (!std::isfinite(number) || std::abs(number) > 1e12)
      return;
  if (op == "save") {
    if (v.stack.size() >= 1024)
      throw DocumentFailure{XL_OUT_OF_MEMORY};
    v.stack.push_back(s);
    return;
  }
  if (op == "restore") {
    if (!v.stack.empty()) {
      s = std::move(v.stack.back());
      v.stack.pop_back();
    }
    return;
  }
  if (op == "transform" || op == "setTransform") {
    if (a.size() != 6)
      throw DocumentFailure{XL_INVALID_ARGUMENT};
    if (op == "setTransform")
      s.transform = make_matrix(a);
    else
      s.transform.preConcat(make_matrix(a));
    return;
  }
  if (op == "resetTransform") {
    s.transform = SkMatrix::I();
    return;
  }
  if (op == "globalAlpha") {
    if (a[0] >= 0 && a[0] <= 1)
      s.alpha = float(a[0]);
    return;
  }
  if (op == "lineWidth") {
    if (a[0] > 0)
      s.line_width = float(a[0]);
    return;
  }
  if (op == "miterLimit") {
    if (a[0] > 0)
      s.miter = float(a[0]);
    return;
  }
  if (op == "lineDashOffset") {
    s.dash_offset = float(a[0]);
    return;
  }
  if (op == "setLineDash") {
    if (a.size() > 1024)
      throw DocumentFailure{XL_OUT_OF_MEMORY};
    float total = 0;
    for (auto x : a) {
      if (x < 0)
        return;
      total += float(x);
    }
    s.dash.assign(a.begin(), a.end());
    if (s.dash.size() % 2)
      s.dash.insert(s.dash.end(), a.begin(), a.end());
    if (!total)
      s.dash.clear();
    return;
  }
  if (op == "beginPath") {
    v.path.reset();
    return;
  }
  if (v.path.countPoints() > 65536)
    throw DocumentFailure{XL_OUT_OF_MEMORY};
  if (op == "closePath") {
    v.path.close();
    return;
  }
  const auto point = [&](size_t i) {
    return s.transform.mapPoint({float(a[i]), float(a[i + 1])});
  };
  if (op == "moveTo") {
    v.path.moveTo(point(0));
    return;
  }
  if (op == "lineTo") {
    if (!v.path.getLastPt())
      v.path.moveTo(point(0));
    else
      v.path.lineTo(point(0));
    return;
  }
  if (op == "quadraticCurveTo") {
    if (!v.path.getLastPt())
      v.path.moveTo(point(0));
    v.path.quadTo(point(0), point(2));
    return;
  }
  if (op == "bezierCurveTo") {
    if (!v.path.getLastPt())
      v.path.moveTo(point(0));
    v.path.cubicTo(point(0), point(2), point(4));
    return;
  }
  if (op == "rect") {
    SkPathBuilder b;
    b.addRect(
        SkRect::MakeXYWH(float(a[0]), float(a[1]), float(a[2]), float(a[3])));
    v.path.addPath(b.snapshot(), s.transform);
    return;
  }
  if (op == "arc" || op == "ellipse") {
    const bool ellipse = op == "ellipse";
    const float rx = float(a[2]), ry = ellipse ? float(a[3]) : rx;
    if (rx < 0 || ry < 0)
      throw DocumentFailure{XL_INVALID_ARGUMENT};
    const auto index = ellipse ? 5u : 3u;
    const double start = a[index], end = a[index + 1],
                 tau = 2 * std::numbers::pi;
    const bool ccw = a[index + 2] != 0;
    double sweep = end - start;
    if (!ccw && sweep >= tau)
      sweep = tau;
    else if (ccw && -sweep >= tau)
      sweep = -tau;
    else {
      sweep = std::fmod(sweep, tau);
      if (!ccw && sweep < 0)
        sweep += tau;
      if (ccw && sweep > 0)
        sweep -= tau;
    }
    SkPathBuilder b;
    auto m = s.transform;
    m.preTranslate(float(a[0]), float(a[1]));
    if (ellipse)
      m.preRotate(float(a[4] * 180 / std::numbers::pi));
    b.moveTo({rx * float(std::cos(start)), ry * float(std::sin(start))});
    // Split full turns so Skia's arcTo does not normalize a circle to empty.
    const auto pieces =
        std::max(1, int(std::ceil(std::abs(sweep) / std::numbers::pi)));
    for (int i = 0; i < pieces; ++i)
      b.arcTo(SkRect::MakeLTRB(-rx, -ry, rx, ry),
              float((start + sweep * i / pieces) * 180 / std::numbers::pi),
              float(sweep / pieces * 180 / std::numbers::pi), false);
    v.path.addPath(b.snapshot(), m,
                   v.path.getLastPt() ? SkPath::kExtend_AddPathMode
                                      : SkPath::kAppend_AddPathMode);
    return;
  }
  if (op == "arcTo") {
    if (a[4] < 0)
      throw DocumentFailure{XL_INVALID_ARGUMENT};
    SkMatrix inverse;
    if (!s.transform.invert(&inverse))
      return;
    v.path.transform(inverse);
    if (!v.path.getLastPt())
      v.path.moveTo(float(a[0]), float(a[1]));
    else
      v.path.arcTo({float(a[0]), float(a[1])}, {float(a[2]), float(a[3])},
                   float(a[4]));
    v.path.transform(s.transform);
    return;
  }
  if (op == "clip") {
    const auto depth = s.clip ? s.clip->depth + 1 : 1;
    if (depth > 1024)
      throw DocumentFailure{XL_OUT_OF_MEMORY};
    auto path = v.path.snapshot();
    path.setFillType(!a.empty() && a[0] ? SkPathFillType::kEvenOdd
                                        : SkPathFillType::kWinding);
    s.clip =
        std::make_shared<ClipPath>(ClipPath{std::move(path), s.clip, depth});
    return;
  }
  Command item;
  item.state = s;
  if (op == "fill" || op == "stroke") {
    item.geometry = v.path.snapshot();
    item.stroke = op == "stroke";
    item.geometry.setFillType(!a.empty() && a[0] ? SkPathFillType::kEvenOdd
                                                 : SkPathFillType::kWinding);
  } else if (op == "fillRect" || op == "strokeRect" || op == "clearRect") {
    if (a.size() != 4)
      throw DocumentFailure{XL_INVALID_ARGUMENT};
    if ((!a[2] || !a[3]) && op != "strokeRect")
      return;
    SkPathBuilder b;
    const auto x = float(std::min(a[0], a[0] + a[2])),
               y = float(std::min(a[1], a[1] + a[3]));
    b.addRect(
        SkRect::MakeXYWH(x, y, float(std::abs(a[2])), float(std::abs(a[3]))));
    item.geometry = b.snapshot(&s.transform);
    item.stroke = op == "strokeRect";
    item.clear = op == "clearRect";
  } else
    throw DocumentFailure{XL_UNSUPPORTED};
  if (item.stroke) {
    SkMatrix inverse;
    if (!s.transform.invert(&inverse))
      return;
    auto local = item.geometry.makeTransform(inverse);
    auto p = paint(s, true);
    SkPathBuilder outline;
    skpathutils::FillPathWithPaint(local, p, &outline);
    item.geometry = outline.snapshot(&s.transform);
    item.stroke = false;
    item.state.fill = item.state.stroke;
  }
  v.push(std::move(item));
}
void CanvasContent::text(std::string text, float x, float y, float max_width,
                         bool stroke) {
  if (!std::isfinite(x) || !std::isfinite(y) || !(max_width > 0) ||
      text.empty())
    return;
  auto &v = *impl_;
  if (!v.width || !v.height)
    return;
  Command item;
  item.kind = Command::text;
  item.state = v.state;
  item.words = std::move(text);
  for (auto &c : item.words)
    if (c == '\t' || c == '\n' || c == '\r' || c == '\f')
      c = ' ';
  item.x = x;
  item.y = y;
  item.max_width = max_width;
  item.stroke = stroke;
  v.push(std::move(item));
  v.needs_text = true;
}
CanvasContent::Metrics CanvasContent::measure(std::string text,
                                              TextEngine *engine,
                                              std::shared_ptr<void> session) {
  for (auto &c : text)
    if (c == '\t' || c == '\n' || c == '\r' || c == '\f')
      c = ' ';
  const auto &s = impl_->state;
  const auto paragraph = engine->shape({TextRun{text, s.font}}, {}, 1e9f, s.rtl,
                                       0, s.font, std::move(session));
  const auto width = paragraph->width(), base = paragraph->first_baseline();
  const auto ink = paragraph->ink_bounds();
  const auto font = engine->font_metrics(s.font);
  float offset = 0;
  if (s.align == "center")
    offset = width / 2;
  else if (s.align == "right" || (s.align == "start" && s.rtl) ||
           (s.align == "end" && !s.rtl))
    offset = width;
  float baseline = 0;
  if (s.baseline == "top")
    baseline = font.ascent;
  else if (s.baseline == "hanging")
    baseline = font.ascent * .8f;
  else if (s.baseline == "middle")
    baseline = (font.ascent - font.descent) / 2;
  else if (s.baseline == "bottom" || s.baseline == "ideographic")
    baseline = -font.descent;
  return {width,
          offset - ink.x,
          ink.x + ink.width - offset,
          base - baseline - ink.y,
          ink.y + ink.height - base + baseline,
          font.ascent - baseline,
          font.descent + baseline};
}
std::vector<uint8_t> CanvasContent::pixels(int x, int y, uint32_t w,
                                           uint32_t h) const {
  if (!valid_size(w, h))
    throw DocumentFailure{XL_OUT_OF_MEMORY};
  std::vector<uint8_t> result(size_t(w) * h * 4);
  if (impl_->surface && !result.empty())
    impl_->surface->readPixels(SkImageInfo::Make(int(w), int(h),
                                                 kRGBA_8888_SkColorType,
                                                 kUnpremul_SkAlphaType),
                               result.data(), size_t(w) * 4, x, y);
  return result;
}
std::string CanvasContent::data_url() const {
  const auto &v = *impl_;
  if (!v.width || !v.height || !valid_size(v.width, v.height))
    return "data:,";
  const auto bytes = pixels(0, 0, v.width, v.height);
  SkPixmap pixels(SkImageInfo::Make(int(v.width), int(v.height),
                                    kRGBA_8888_SkColorType,
                                    kUnpremul_SkAlphaType),
                  bytes.data(), size_t(v.width) * 4);
  const auto encoded = SkPngEncoder::Encode(pixels, {});
  if (!encoded)
    throw DocumentFailure{XL_INTERNAL_ERROR};
  constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result = "data:image/png;base64,";
  result.reserve(result.size() + ((encoded->size() + 2) / 3) * 4);
  const auto *data = encoded->bytes();
  for (size_t i = 0; i < encoded->size(); i += 3) {
    uint32_t value = uint32_t(data[i]) << 16;
    if (i + 1 < encoded->size())
      value |= uint32_t(data[i + 1]) << 8;
    if (i + 2 < encoded->size())
      value |= data[i + 2];
    result += alphabet[(value >> 18) & 63];
    result += alphabet[(value >> 12) & 63];
    result += i + 1 < encoded->size() ? alphabet[(value >> 6) & 63] : '=';
    result += i + 2 < encoded->size() ? alphabet[value & 63] : '=';
  }
  return result;
}
void CanvasContent::image(std::shared_ptr<const ImageDrawing> image,
                          xl_node_bounds source, xl_node_bounds destination,
                          bool raw) {
  if (!image || source.width <= 0 || source.height <= 0 ||
      destination.width <= 0 || destination.height <= 0)
    return;
  Command item;
  item.kind = Command::image;
  item.state = impl_->state;
  item.pixels = std::move(image);
  item.source = source;
  item.destination = destination;
  item.raw = raw;
  impl_->push(std::move(item));
}
void CanvasContent::put_pixels(std::span<const uint8_t> pixels, uint32_t width,
                               uint32_t height, int x, int y,
                               xl_node_bounds dirty) {
  if (!valid_size(width, height) ||
      pixels.size() != uint64_t(width) * height * 4)
    throw DocumentFailure{XL_INVALID_ARGUMENT};
  const auto left = std::clamp(dirty.x, 0.f, float(width)),
             top = std::clamp(dirty.y, 0.f, float(height));
  const auto right = std::clamp(dirty.x + dirty.width, 0.f, float(width)),
             bottom = std::clamp(dirty.y + dirty.height, 0.f, float(height));
  if (right <= left || bottom <= top)
    return;
  auto drawing = std::make_shared<ImageDrawing>();
  const auto data = SkData::MakeWithCopy(pixels.data(), pixels.size());
  drawing->image = SkImages::RasterFromData(
      SkImageInfo::Make(int(width), int(height), kRGBA_8888_SkColorType,
                        kUnpremul_SkAlphaType),
      data, size_t(width) * 4);
  if (!drawing->image)
    throw std::bad_alloc();
  image(std::move(drawing), {left, top, right - left, bottom - top},
        {float(x) + left, float(y) + top, right - left, bottom - top}, true);
}
void CanvasContent::render(TextEngine *text, std::shared_ptr<void> session) {
  auto &v = *impl_;
  if (v.commands.empty())
    return;
  if (!valid_size(v.width, v.height))
    throw DocumentFailure{XL_OUT_OF_MEMORY};
  if (v.needs_text && (!text || !session))
    throw DocumentFailure{XL_BUSY};
  if (!v.width || !v.height) {
    v.commands.clear();
    v.bytes = 0;
    v.needs_text = false;
    return;
  }
  if (!v.surface) {
    v.surface = SkSurfaces::Raster(
        SkImageInfo::MakeN32Premul(int(v.width), int(v.height)));
    if (!v.surface)
      throw std::bad_alloc();
    v.surface->getCanvas()->clear(SK_ColorTRANSPARENT);
  }
  auto *canvas = v.surface->getCanvas();
  size_t completed = 0;
  // Commit one command at a time: preparation failure never replays earlier
  // alpha draws.
  try {
    for (const auto &item : v.commands) {
      std::shared_ptr<TextParagraph> paragraph;
      TextFontMetrics metrics;
      if (item.kind == Command::text) {
        auto f = item.state.font;
        f.color = 0xff000000;
        paragraph = text->shape({TextRun{item.words, f}}, {}, 1e9f,
                                item.state.rtl, 0, f, session);
        metrics = text->font_metrics(f);
      }
      SkAutoCanvasRestore save(canvas, true);
      if (!item.raw)
        clips(canvas, item.state.clip);
      auto p = paint(item.state, false, item.clear);
      if (item.kind == Command::path) {
        if (!item.clear && (item.state.blend == SkBlendMode::kSrc ||
                            item.state.blend == SkBlendMode::kSrcIn ||
                            item.state.blend == SkBlendMode::kSrcOut ||
                            item.state.blend == SkBlendMode::kDstIn ||
                            item.state.blend == SkBlendMode::kDstATop)) {
          SkPaint composite;
          composite.setBlendMode(item.state.blend);
          canvas->saveLayer(nullptr, &composite);
          p.setBlendMode(SkBlendMode::kSrcOver);
          canvas->drawPath(item.geometry, p);
          canvas->restore();
        } else
          canvas->drawPath(item.geometry, p);
      } else if (item.kind == Command::image) {
        const auto &src = item.source;
        const auto &dst = item.destination;
        if (!item.raw)
          canvas->concat(item.state.transform);
        if (item.raw) {
          SkPaint copy;
          copy.setBlendMode(SkBlendMode::kSrc);
          canvas->drawImageRect(
              item.pixels->image,
              SkRect::MakeXYWH(src.x, src.y, src.width, src.height),
              SkRect::MakeXYWH(dst.x, dst.y, dst.width, dst.height),
              SkSamplingOptions(), &copy, SkCanvas::kStrict_SrcRectConstraint);
        } else {
          SkPaint composite;
          composite.setBlendMode(item.state.blend);
          composite.setAlphaf(item.state.alpha);
          canvas->saveLayer(nullptr, &composite);
          canvas->clipRect(
              SkRect::MakeXYWH(dst.x, dst.y, dst.width, dst.height));
          canvas->translate(dst.x, dst.y);
          canvas->scale(dst.width / src.width, dst.height / src.height);
          canvas->translate(-src.x, -src.y);
          if (item.pixels->image)
            canvas->drawImage(item.pixels->image, 0, 0,
                              SkSamplingOptions(SkFilterMode::kLinear));
          else if (item.pixels->picture)
            canvas->drawPicture(item.pixels->picture);
          canvas->restore();
        }
      } else {
        v.text_session = session;
        const auto width = paragraph->width();
        float x = item.x, y = item.y;
        const auto &s = item.state;
        const auto scale =
            width > item.max_width ? item.max_width / width : 1.f;
        const auto used = width * scale;
        if (s.align == "center")
          x -= used / 2;
        else if (s.align == "right" || (s.align == "end" && !s.rtl) ||
                 (s.align == "start" && s.rtl))
          x -= used;
        float baseline = 0;
        if (s.baseline == "top")
          baseline = metrics.ascent;
        else if (s.baseline == "hanging")
          baseline = metrics.ascent * .8f;
        else if (s.baseline == "middle")
          baseline = (metrics.ascent - metrics.descent) / 2;
        else if (s.baseline == "bottom" || s.baseline == "ideographic")
          baseline = -metrics.descent;
        // The alpha mask uses the same complex-text shaper and font fallback as
        // DOM text.
        SkPaint composite;
        composite.setBlendMode(s.blend);
        canvas->saveLayer(nullptr, &composite);
        SkPaint fill = paint(s, item.stroke);
        fill.setStyle(SkPaint::kFill_Style);
        fill.setBlendMode(SkBlendMode::kSrcOver);
        canvas->drawPaint(fill);
        SkPaint mask;
        mask.setBlendMode(SkBlendMode::kDstIn);
        canvas->saveLayer(nullptr, &mask);
        canvas->concat(s.transform);
        canvas->translate(x, y + baseline - paragraph->first_baseline());
        canvas->scale(scale, 1);
        TextMask mask_canvas(canvas, s, item.stroke);
        paragraph->paint(&mask_canvas, 0, 0);
        canvas->restore();
        canvas->restore();
      }
      ++completed;
    }
  } catch (...) {
    v.commands.erase(v.commands.begin(), v.commands.begin() + completed);
    v.bytes = 0;
    for (const auto &item : v.commands)
      v.bytes += item.bytes;
    throw;
  }
  v.commands.clear();
  v.bytes = 0;
  v.needs_text = false;
  v.snapshot.reset();
}
std::shared_ptr<const ImageDrawing> CanvasContent::drawing() {
  auto &v = *impl_;
  if (!v.snapshot) {
    auto result = std::make_shared<ImageDrawing>();
    result->text_session = v.text_session;
    if (v.surface)
      result->image = v.surface->makeImageSnapshot();
    v.snapshot = std::move(result);
    v.dirty = false;
  }
  return v.snapshot;
}
} // namespace xl::detail
