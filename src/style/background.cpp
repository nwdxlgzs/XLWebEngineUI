#include "background.h"
#include "document/dom_internal.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <numbers>

namespace xl::detail::cssbackground {
namespace {
using Type = lxb_css_syntax_token_type_t;
struct Token {
  Type type;
  std::string text, raw;
  size_t end = 0;
  bool space = false, comment = false;
};
using Tokens = std::vector<Token>;
std::string lower(std::string value) {
  for (auto &c : value)
    if (c >= 'A' && c <= 'Z')
      c += 'a' - 'A';
  return value;
}
bool wide(std::string_view value) {
  return value == "inherit" || value == "initial" || value == "unset" ||
         value == "revert" || value == "revert-layer";
}
std::optional<Tokens> tokenize(std::string_view source) {
  if (source.size() > 65536)
    return {};
  auto *lexer = lxb_css_syntax_tokenizer_create();
  if (!lexer)
    throw std::bad_alloc();
  struct End {
    lxb_css_syntax_tokenizer_t *lexer;
    ~End() { lxb_css_syntax_tokenizer_destroy(lexer); }
  } end{lexer};
  check_lxb(lxb_css_syntax_tokenizer_init(lexer));
  lexer->with_comment = true;
  lxb_css_syntax_tokenizer_buffer_set(lexer, chars(source), source.size());
  Tokens result;
  std::vector<size_t> stack;
  bool space = false, comment = false;
  while (auto *token = lxb_css_syntax_token(lexer)) {
    if (token->type == LXB_CSS_SYNTAX_TOKEN__EOF)
      break;
    if (token->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE ||
        token->type == LXB_CSS_SYNTAX_TOKEN_COMMENT) {
      space |= token->type == LXB_CSS_SYNTAX_TOKEN_WHITESPACE;
      comment |= token->type == LXB_CSS_SYNTAX_TOKEN_COMMENT;
      lxb_css_syntax_token_consume(lexer);
      continue;
    }
    const auto *base = lxb_css_syntax_token_base(token);
    std::string raw(reinterpret_cast<const char *>(base->begin), base->length),
        text = raw;
    if (token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION ||
        token->type == LXB_CSS_SYNTAX_TOKEN_IDENT) {
      const auto *id = lxb_css_syntax_token_ident(token);
      text.assign(reinterpret_cast<const char *>(id->data), id->length);
    }
    result.push_back(
        {token->type, lower(text), std::move(raw), 0, space, comment});
    space = comment = false;
    const auto i = result.size() - 1;
    if (token->type == LXB_CSS_SYNTAX_TOKEN_FUNCTION ||
        token->type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) {
      stack.push_back(i);
      if (stack.size() > 64)
        return {};
    } else if (token->type == LXB_CSS_SYNTAX_TOKEN_R_PARENTHESIS) {
      if (stack.empty())
        return {};
      result[stack.back()].end = i;
      stack.pop_back();
    } else if (token->type == LXB_CSS_SYNTAX_TOKEN_BAD_STRING ||
               token->type == LXB_CSS_SYNTAX_TOKEN_BAD_URL ||
               token->type == LXB_CSS_SYNTAX_TOKEN_LC_BRACKET ||
               token->type == LXB_CSS_SYNTAX_TOKEN_LS_BRACKET)
      return {};
    if (result.size() > 8192)
      return {};
    lxb_css_syntax_token_consume(lexer);
  }
  return stack.empty() ? std::optional<Tokens>(std::move(result))
                       : std::nullopt;
}
size_t next(const Tokens &t, size_t i) {
  return t[i].type == LXB_CSS_SYNTAX_TOKEN_FUNCTION ||
                 t[i].type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS
             ? t[i].end + 1
             : i + 1;
}
std::string text(const Tokens &t, size_t a, size_t b) {
  std::string result;
  for (size_t i = a; i < b; ++i) {
    if (i > a) {
      if (t[i].space)
        result += ' ';
      else if (t[i].comment)
        result += "/**/";
    }
    result += t[i].raw;
  }
  return result;
}
std::vector<std::pair<size_t, size_t>> split(const Tokens &t, size_t a,
                                             size_t b) {
  std::vector<std::pair<size_t, size_t>> result;
  size_t begin = a;
  for (size_t i = a; i < b; i = next(t, i))
    if (t[i].type == LXB_CSS_SYNTAX_TOKEN_COMMA) {
      result.emplace_back(begin, i);
      begin = i + 1;
    }
  result.emplace_back(begin, b);
  return result;
}
std::vector<std::pair<size_t, size_t>> parts(const Tokens &t, size_t a,
                                             size_t b) {
  std::vector<std::pair<size_t, size_t>> result;
  for (size_t i = a; i < b;) {
    const auto n = next(t, i);
    result.emplace_back(i, n);
    i = n;
  }
  return result;
}
Length scale(Length value, double factor) {
  value.px *= factor;
  value.percent *= factor;
  value.em *= factor;
  value.rem *= factor;
  value.vw *= factor;
  value.vh *= factor;
  value.vmin *= factor;
  value.vmax *= factor;
  return value;
}
Length add(Length a, const Length &b) {
  a.px += b.px;
  a.percent += b.percent;
  a.em += b.em;
  a.rem += b.rem;
  a.vw += b.vw;
  a.vh += b.vh;
  a.vmin += b.vmin;
  a.vmax += b.vmax;
  return a;
}
struct Math {
  Length value;
  double number = 0;
  bool dimension = false, calculated = false;
};
class LengthParser {
  const Tokens &t;
  bool angular;
  std::optional<Math> atom(size_t &i, size_t end, unsigned depth) const {
    if (i == end || depth > 32)
      return {};
    const auto &token = t[i++];
    if (token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION ||
        token.type == LXB_CSS_SYNTAX_TOKEN_L_PARENTHESIS) {
      if (token.end >= end ||
          (token.type == LXB_CSS_SYNTAX_TOKEN_FUNCTION && token.text != "calc"))
        return {};
      auto value = sum(i, token.end, depth + 1);
      if (!value || i != token.end)
        return {};
      i = token.end + 1;
      value->calculated = true;
      return value;
    }
    if (token.type == LXB_CSS_SYNTAX_TOKEN_DELIM &&
        (token.text == "+" || token.text == "-")) {
      auto value = atom(i, end, depth + 1);
      if (value && token.text == "-") {
        value->value = scale(value->value, -1);
        value->number = -value->number;
      }
      return value;
    }
    if (token.type != LXB_CSS_SYNTAX_TOKEN_NUMBER &&
        token.type != LXB_CSS_SYNTAX_TOKEN_DIMENSION &&
        token.type != LXB_CSS_SYNTAX_TOKEN_PERCENTAGE)
      return {};
    auto raw = std::string_view(token.text);
    if (raw.starts_with('+'))
      raw.remove_prefix(1);
    double n = 0;
    const auto parsed = std::from_chars(raw.data(), raw.data() + raw.size(), n);
    if (parsed.ec != std::errc{} || !std::isfinite(n) || std::abs(n) > 1e20)
      return {};
    const auto unit = std::string_view(
        parsed.ptr, size_t(raw.data() + raw.size() - parsed.ptr));
    Math value;
    if (token.type == LXB_CSS_SYNTAX_TOKEN_NUMBER) {
      value.number = n;
      return value;
    }
    value.dimension = true;
    if (unit == "%")
      value.value.percent = n / 100;
    else if (angular) {
      if (unit == "deg")
        value.value.percent = n / 360;
      else if (unit == "turn")
        value.value.percent = n;
      else if (unit == "grad")
        value.value.percent = n / 400;
      else if (unit == "rad")
        value.value.percent = n / (2 * std::numbers::pi);
      else
        return {};
    } else if (unit == "px")
      value.value.px = n;
    else if (unit == "em")
      value.value.em = n;
    else if (unit == "rem")
      value.value.rem = n;
    else if (unit == "vw" || unit == "svw" || unit == "lvw" || unit == "dvw")
      value.value.vw = n / 100;
    else if (unit == "vh" || unit == "svh" || unit == "lvh" || unit == "dvh")
      value.value.vh = n / 100;
    else if (unit == "vmin")
      value.value.vmin = n / 100;
    else if (unit == "vmax")
      value.value.vmax = n / 100;
    else if (unit == "in")
      value.value.px = n * 96;
    else if (unit == "cm")
      value.value.px = n * 96 / 2.54;
    else if (unit == "mm")
      value.value.px = n * 96 / 25.4;
    else if (unit == "q")
      value.value.px = n * 96 / 101.6;
    else if (unit == "pt")
      value.value.px = n * 96 / 72;
    else if (unit == "pc")
      value.value.px = n * 16;
    else
      return {};
    return value;
  }
  std::optional<Math> product(size_t &i, size_t end, unsigned depth) const {
    auto a = atom(i, end, depth);
    if (!a)
      return {};
    while (i < end && t[i].type == LXB_CSS_SYNTAX_TOKEN_DELIM &&
           (t[i].text == "*" || t[i].text == "/")) {
      const bool divide = t[i++].text == "/";
      auto b = atom(i, end, depth);
      if (!b)
        return {};
      if (divide) {
        if (b->dimension || !b->number)
          return {};
        a->value = scale(a->value, 1 / b->number);
        a->number /= b->number;
      } else {
        if (a->dimension && b->dimension)
          return {};
        if (!a->dimension && b->dimension)
          std::swap(a, b);
        a->value = scale(a->value, b->number);
        a->number *= b->number;
      }
    }
    return a;
  }
  std::optional<Math> sum(size_t &i, size_t end, unsigned depth) const {
    auto a = product(i, end, depth);
    if (!a)
      return {};
    while (i < end && t[i].type == LXB_CSS_SYNTAX_TOKEN_DELIM &&
           (t[i].text == "+" || t[i].text == "-")) {
      if (!t[i].space || i + 1 >= end || !t[i + 1].space)
        return {};
      const auto sign = t[i++].text == "-" ? -1. : 1.;
      auto b = product(i, end, depth);
      if (!b || a->dimension != b->dimension)
        return {};
      a->value = add(a->value, scale(b->value, sign));
      a->number += sign * b->number;
    }
    return a;
  }

public:
  LengthParser(const Tokens &tokens, bool angle = false)
      : t(tokens), angular(angle) {}
  std::optional<Length> value(size_t first, size_t last,
                              bool nonnegative = false) const {
    auto i = first;
    const auto a = atom(i, last, 0);
    if (!a || i != last || (!a->dimension && a->number != 0))
      return {};
    const auto &n = a->value;
    for (const auto v :
         {n.px, n.percent, n.em, n.rem, n.vw, n.vh, n.vmin, n.vmax})
      if (!std::isfinite(v) || std::abs(v) > 1e20 ||
          (nonnegative && !a->calculated && v < 0))
        return {};
    return n;
  }
};
std::optional<double> angle(const Tokens &t, size_t a, size_t b) {
  const auto n = LengthParser(t, true).value(a, b);
  if (!n)
    return {};
  // A percentage is a conic stop, never a direction angle.
  for (size_t i = a; i < b; ++i)
    if (t[i].type == LXB_CSS_SYNTAX_TOKEN_PERCENTAGE)
      return {};
  return n->percent * 360;
}
std::optional<Position> position(const Tokens &t, size_t a, size_t b) {
  const auto p = parts(t, a, b);
  if (p.empty() || p.size() > 4)
    return {};
  const auto key = [&](size_t i) {
    return t[p[i].first].type == LXB_CSS_SYNTAX_TOKEN_IDENT ? t[p[i].first].text
                                                            : std::string{};
  };
  const auto axis = [&](size_t i, bool horizontal) -> std::optional<Axis> {
    const auto k = key(i);
    if (k == "center")
      return Axis{};
    if (k == (horizontal ? "left" : "top"))
      return Axis{Length{}};
    if (k == (horizontal ? "right" : "bottom"))
      return Axis{Length{}, true};
    if (auto n = LengthParser(t).value(p[i].first, p[i].second))
      return Axis{*n};
    return {};
  };
  Position out;
  if (p.size() == 1) {
    if (key(0) == "top" || key(0) == "bottom") {
      out.y = *axis(0, false);
      return out;
    }
    auto x = axis(0, true);
    if (!x)
      return {};
    out.x = *x;
    return out;
  }
  if (p.size() == 2) {
    size_t x = 0, y = 1;
    if (key(0) == "top" || key(0) == "bottom" || key(1) == "left" ||
        key(1) == "right")
      std::swap(x, y);
    auto px = axis(x, true), py = axis(y, false);
    if (!px || !py)
      return {};
    out.x = *px;
    out.y = *py;
    return out;
  }
  bool used_x = false, used_y = false;
  for (size_t i = 0; i < p.size(); ++i) {
    const auto k = key(i);
    const bool x = k == "left" || k == "right", y = k == "top" || k == "bottom";
    if (!x && !y && k != "center")
      return {};
    if (k == "center") {
      if (!used_x) {
        out.x = Axis{};
        used_x = true;
      } else if (!used_y) {
        out.y = Axis{};
        used_y = true;
      } else
        return {};
      continue;
    }
    if ((x && used_x) || (y && used_y))
      return {};
    Axis value{Length{}, k == "right" || k == "bottom"};
    if (i + 1 < p.size())
      if (auto n = LengthParser(t).value(p[i + 1].first, p[i + 1].second)) {
        value.offset = *n;
        ++i;
      }
    if (x) {
      out.x = value;
      used_x = true;
    } else {
      out.y = value;
      used_y = true;
    }
  }
  return used_x && used_y ? std::optional<Position>(out) : std::nullopt;
}
std::optional<Size> size(const Tokens &t, size_t a, size_t b) {
  const auto p = parts(t, a, b);
  if (p.empty() || p.size() > 2)
    return {};
  Size out;
  if (p.size() == 1 && (t[a].text == "cover" || t[a].text == "contain")) {
    out.fit = t[a].text == "cover" ? 1 : 2;
    return out;
  }
  for (size_t i = 0; i < p.size(); ++i) {
    if (t[p[i].first].text == "auto" && p[i].second == p[i].first + 1)
      continue;
    auto n = LengthParser(t).value(p[i].first, p[i].second, true);
    if (!n)
      return {};
    if (i) {
      out.y = *n;
      out.auto_y = false;
    } else {
      out.x = *n;
      out.auto_x = false;
    }
  }
  return out;
}
std::optional<std::array<Repeat, 2>> repeat(const Tokens &t, size_t a,
                                            size_t b) {
  if (a + 1 == b && t[a].text == "repeat-x")
    return std::array{Repeat::repeat, Repeat::none};
  if (a + 1 == b && t[a].text == "repeat-y")
    return std::array{Repeat::none, Repeat::repeat};
  if (b - a < 1 || b - a > 2)
    return {};
  std::array<Repeat, 2> out;
  for (size_t i = a; i < b; ++i) {
    const auto &key = t[i].text;
    Repeat value;
    if (key == "repeat")
      value = Repeat::repeat;
    else if (key == "no-repeat")
      value = Repeat::none;
    else if (key == "round")
      value = Repeat::round;
    else if (key == "space")
      value = Repeat::space;
    else
      return {};
    out[i - a] = value;
  }
  if (b - a == 1)
    out[1] = out[0];
  return out;
}
std::optional<Box> box(std::string_view value) {
  if (value == "border-box")
    return Box::border;
  if (value == "padding-box")
    return Box::padding;
  if (value == "content-box")
    return Box::content;
  return {};
}
std::optional<Attachment> attachment(std::string_view value) {
  if (value == "scroll")
    return Attachment::scroll;
  if (value == "fixed")
    return Attachment::fixed;
  if (value == "local")
    return Attachment::local;
  return {};
}
std::optional<csscolor::Space> space(std::string_view value) {
  using S = csscolor::Space;
  for (const auto &[name, id] :
       std::initializer_list<std::pair<const char *, S>>{
           {"srgb", S::srgb},
           {"srgb-linear", S::linear_srgb},
           {"display-p3", S::p3},
           {"a98-rgb", S::a98},
           {"prophoto-rgb", S::prophoto},
           {"rec2020", S::rec2020},
           {"xyz", S::xyz65},
           {"xyz-d65", S::xyz65},
           {"xyz-d50", S::xyz50},
           {"lab", S::lab},
           {"lch", S::lch},
           {"oklab", S::oklab},
           {"oklch", S::oklch},
           {"hsl", S::hsl},
           {"hwb", S::hwb}})
    if (value == name)
      return id;
  return {};
}
Image gradient(const Tokens &t, size_t first, size_t last) {
  if (first == last || t[first].type != LXB_CSS_SYNTAX_TOKEN_FUNCTION ||
      t[first].end + 1 != last)
    return {};
  auto name = t[first].text;
  auto out = std::make_shared<Gradient>();
  if (name.starts_with("repeating-")) {
    out->repeating = true;
    name.erase(0, 10);
  }
  if (name == "linear-gradient")
    out->kind = Gradient::linear;
  else if (name == "radial-gradient")
    out->kind = Gradient::radial;
  else if (name == "conic-gradient") {
    out->kind = Gradient::conic;
    out->angle = 0;
  } else
    return {};
  const auto components = split(t, first + 1, last - 1);
  if (components.size() < 2 || components.size() > 256)
    return {};
  auto [a, b] = components[0];
  if (a == b)
    return {};
  bool prelude = !csscolor::parse(text(t, a, next(t, a)));
  if (prelude) {
    auto atoms = parts(t, a, b);
    std::vector<std::pair<size_t, size_t>> geometry;
    for (size_t i = 0; i < atoms.size(); ++i) {
      const auto start = atoms[i].first;
      if (t[start].text != "in") {
        geometry.push_back(atoms[i]);
        continue;
      }
      if (out->explicit_space || ++i == atoms.size())
        return {};
      auto method = space(t[atoms[i].first].text);
      if (!method)
        return {};
      out->space = *method;
      out->explicit_space = true;
      if (i + 2 < atoms.size() && t[atoms[i + 2].first].text == "hue") {
        const auto &hue = t[atoms[i + 1].first].text;
        if (out->space != csscolor::Space::lch &&
            out->space != csscolor::Space::oklch &&
            out->space != csscolor::Space::hsl &&
            out->space != csscolor::Space::hwb)
          return {};
        if (hue == "shorter")
          out->hue = 0;
        else if (hue == "longer")
          out->hue = 1;
        else if (hue == "increasing")
          out->hue = 2;
        else if (hue == "decreasing")
          out->hue = 3;
        else
          return {};
        i += 2;
      }
    }
    if (out->kind == Gradient::linear) {
      if (!geometry.empty()) {
        const auto start = geometry[0].first;
        if (t[start].text == "to") {
          if (geometry.size() < 2 || geometry.size() > 3)
            return {};
          for (size_t i = 1; i < geometry.size(); ++i) {
            const auto &k = t[geometry[i].first].text;
            if (k == "left" || k == "right") {
              if (out->direction_x)
                return {};
              out->direction_x = k == "right" ? 1 : -1;
            } else if (k == "top" || k == "bottom") {
              if (out->direction_y)
                return {};
              out->direction_y = k == "bottom" ? 1 : -1;
            } else
              return {};
          }
        } else {
          if (geometry.size() != 1)
            return {};
          auto n = angle(t, start, geometry[0].second);
          if (!n)
            return {};
          out->angle = *n;
        }
      }
    } else {
      bool shape = false, extent = false, at = false, from = false;
      for (size_t i = 0; i < geometry.size(); ++i) {
        const auto [begin, end] = geometry[i];
        const auto &k = t[begin].text;
        if (k == "at") {
          if (at || i + 1 == geometry.size())
            return {};
          at = true;
          auto pos = position(t, geometry[i + 1].first, geometry.back().second);
          if (!pos)
            return {};
          out->center = *pos;
          i = geometry.size();
        } else if (k == "from" && out->kind == Gradient::conic) {
          if (from || i + 1 == geometry.size())
            return {};
          from = true;
          const auto [x, y] = geometry[++i];
          auto n = angle(t, x, y);
          if (!n)
            return {};
          out->angle = *n;
        } else if (out->kind == Gradient::radial) {
          if (k == "circle" || k == "ellipse") {
            if (shape)
              return {};
            shape = true;
            out->circle = k == "circle";
          } else if (k == "closest-side" || k == "closest-corner" ||
                     k == "farthest-side" || k == "farthest-corner") {
            if (extent || !out->radii.empty())
              return {};
            extent = true;
            out->extent = k == "closest-side"     ? 0
                          : k == "closest-corner" ? 1
                          : k == "farthest-side"  ? 2
                                                  : 3;
          } else {
            if (extent || out->radii.size() >= 2)
              return {};
            auto n = LengthParser(t).value(begin, end, true);
            if (!n)
              return {};
            out->radii.push_back(*n);
          }
        } else
          return {};
      }
      if (out->kind == Gradient::radial && !out->radii.empty()) {
        if (out->radii.size() == 1) {
          if ((shape && !out->circle) || out->radii[0].percent != 0)
            return {};
          out->circle = true;
        } else if (out->circle)
          return {};
      }
    }
  }
  bool hint = false, legacy = true;
  size_t colors = 0;
  for (size_t i = prelude ? 1 : 0; i < components.size(); ++i) {
    auto [begin, end] = components[i];
    if (begin == end)
      return {};
    const auto color_end = next(t, begin);
    auto color = csscolor::parse(text(t, begin, color_end));
    if (color) {
      ++colors;
      legacy &= csscolor::legacy(color);
      hint = false;
      if (color_end == end)
        out->stops.push_back({std::move(color), {}});
      else {
        const auto values = parts(t, color_end, end);
        if (values.size() > 2 ||
            (values.size() == 2 && !t[values[1].first].space))
          return {};
        for (const auto [a, b] : values) {
          auto n = LengthParser(t, out->kind == Gradient::conic).value(a, b);
          if (!n)
            return {};
          out->stops.push_back({color, *n});
        }
      }
    } else {
      if (!colors || hint || i + 1 == components.size())
        return {};
      auto n = LengthParser(t, out->kind == Gradient::conic).value(begin, end);
      if (!n)
        return {};
      out->stops.push_back({{}, *n});
      hint = true;
    }
  }
  if (colors < 2)
    return {};
  if (!out->explicit_space && !legacy)
    out->space = csscolor::Space::oklab;
  return out;
}
bool component(Declaration &out, size_t index, const Tokens &t, size_t a,
               size_t b) {
  const auto layers = split(t, a, b);
  if (layers.size() > 64)
    return false;
  out.style.values[index] = text(t, a, b);
  switch (index) {
  case 0:
    out.style.images.clear();
    break;
  case 1:
    out.style.positions.clear();
    break;
  case 2:
    out.style.sizes.clear();
    break;
  case 3:
    out.style.repeats.clear();
    break;
  case 4:
    out.style.origins.clear();
    break;
  case 5:
    out.style.clips.clear();
    break;
  case 6:
    out.style.attachments.clear();
    break;
  }
  for (const auto [first, last] : layers) {
    if (first == last)
      return false;
    if (index == 0) {
      if (first + 1 == last && t[first].text == "none")
        out.style.images.push_back({});
      else {
        auto image = gradient(t, first, last);
        if (!image)
          return false;
        out.style.images.push_back(std::move(image));
      }
    }
    if (index == 1) {
      auto n = position(t, first, last);
      if (!n)
        return false;
      out.style.positions.push_back(*n);
    }
    if (index == 2) {
      auto n = size(t, first, last);
      if (!n)
        return false;
      out.style.sizes.push_back(*n);
    }
    if (index == 3) {
      auto n = repeat(t, first, last);
      if (!n)
        return false;
      out.style.repeats.push_back(*n);
    }
    if (index == 4 || index == 5) {
      if (first + 1 != last)
        return false;
      auto n = box(t[first].text);
      if (!n)
        return false;
      (index == 4 ? out.style.origins : out.style.clips).push_back(*n);
    }
    if (index == 6) {
      if (first + 1 != last)
        return false;
      auto n = attachment(t[first].text);
      if (!n)
        return false;
      out.style.attachments.push_back(*n);
    }
  }
  return true;
}
} // namespace
std::optional<Length> parse_length(std::string_view source, bool* percentage) {
  auto tokens = tokenize(source);
  if(percentage)*percentage=tokens&&std::any_of(tokens->begin(),tokens->end(),[](const auto& token){return token.type==LXB_CSS_SYNTAX_TOKEN_PERCENTAGE;});
  return tokens && !tokens->empty() ? LengthParser(*tokens).value(0, tokens->size()) : std::nullopt;
}
bool property(std::string_view name) {
  return name == "background" ||
         std::any_of(std::begin(properties), std::end(properties),
                     [&](auto n) { return name == n; });
}
std::shared_ptr<const Declaration> parse(std::string_view name,
                                         std::string_view source) {
  if (!property(name))
    return {};
  auto tokens = tokenize(source);
  if (!tokens || tokens->empty())
    return {};
  const auto &t = *tokens;
  auto out = std::make_shared<Declaration>();
  if (t.size() == 1 && t[0].type == LXB_CSS_SYNTAX_TOKEN_IDENT &&
      wide(t[0].text)) {
    out->wide = t[0].text;
    return out;
  }
  if (name != "background") {
    for (size_t i = 0; i < std::size(properties); ++i)
      if (name == properties[i])
        return component(*out, i, t, 0, t.size()) ? out : nullptr;
    return {};
  }
  const auto layers = split(t, 0, t.size());
  if (layers.size() > 64)
    return {};
  std::array<std::vector<std::string>, 7> values;
  for (size_t layer = 0; layer < layers.size(); ++layer) {
    const auto [a, b] = layers[layer];
    if (a == b)
      return {};
    std::array<std::string, 7> v = Style{}.values;
    bool image = false, rep = false, attach = false, sized = false,
         has_color = false;
    std::vector<std::pair<size_t, size_t>> positions;
    std::vector<Box> boxes;
    for (size_t i = a; i < b;) {
      const auto end = next(t, i);
      const auto &k = t[i].text;
      if (k == "none" || gradient(t, i, end)) {
        if (image)
          return {};
        image = true;
        v[0] = text(t, i, end);
        i = end;
        continue;
      }
      if (auto bx = box(k); bx && i + 1 == end) {
        if (boxes.size() >= 2)
          return {};
        boxes.push_back(*bx);
        v[boxes.size() == 1 ? 4 : 5] = text(t, i, end);
        i = end;
        continue;
      }
      if (attachment(k) && i + 1 == end) {
        if (attach)
          return {};
        attach = true;
        v[6] = k;
        i = end;
        continue;
      }
      if (k == "repeat" || k == "no-repeat" || k == "round" || k == "space" ||
          k == "repeat-x" || k == "repeat-y") {
        if (rep)
          return {};
        rep = true;
        size_t finish = end;
        if (end < b && repeat(t, i, end + 1))
          finish = end + 1;
        if (!repeat(t, i, finish))
          return {};
        v[3] = text(t, i, finish);
        i = finish;
        continue;
      }
      if (t[i].type == LXB_CSS_SYNTAX_TOKEN_DELIM && k == "/") {
        if (sized || positions.empty() || end >= b)
          return {};
        sized = true;
        size_t finish = next(t, end);
        if (finish < b && size(t, end, next(t, finish)))
          finish = next(t, finish);
        if (!size(t, end, finish))
          return {};
        v[2] = text(t, end, finish);
        i = finish;
        continue;
      }
      if (auto color = csscolor::parse(text(t, i, end))) {
        if (layer + 1 != layers.size() || has_color)
          return {};
        has_color = true;
        out->color = text(t, i, end);
        i = end;
        continue;
      }
      positions.emplace_back(i, end);
      if (positions.size() > 4 || sized)
        return {};
      i = end;
    }
    if (!positions.empty()) {
      v[1] = text(t, positions.front().first, positions.back().second);
      if (!position(t, positions.front().first, positions.back().second))
        return {};
    }
    if (boxes.size() == 1)
      v[5] = v[4];
    for (size_t i = 0; i < values.size(); ++i)
      values[i].push_back(v[i]);
  }
  for (size_t i = 0; i < values.size(); ++i) {
    std::string joined;
    for (const auto &v : values[i]) {
      if (!joined.empty())
        joined += ", ";
      joined += v;
    }
    auto component_tokens = tokenize(joined);
    if (!component_tokens ||
        !component(*out, i, *component_tokens, 0, component_tokens->size()))
      return {};
  }
  return out;
}
void assign(Style &target, const Style &source, size_t property) {
  target.values[property] = source.values[property];
  switch (property) {
  case 0:
    target.images = source.images;
    break;
  case 1:
    target.positions = source.positions;
    break;
  case 2:
    target.sizes = source.sizes;
    break;
  case 3:
    target.repeats = source.repeats;
    break;
  case 4:
    target.origins = source.origins;
    break;
  case 5:
    target.clips = source.clips;
    break;
  case 6:
    target.attachments = source.attachments;
    break;
  }
}
bool initial(const Style &value) { return value.values == Style{}.values; }
double resolve(const Length &v, double basis, const Environment &e) {
  return v.px + v.percent * basis + v.em * e.em + v.rem * e.rem + v.vw * e.vw +
         v.vh * e.vh + v.vmin * std::min(e.vw, e.vh) +
         v.vmax * std::max(e.vw, e.vh);
}
bool equal(const std::shared_ptr<const Paint> &a,
           const std::shared_ptr<const Paint> &b) {
  if (a == b)
    return true;
  if (!a || !b)
    return false;
  const auto &x = a->environment;
  const auto &y = b->environment;
  return a->style.values == b->style.values && a->padding == b->padding &&
         a->border == b->border && x.em == y.em && x.rem == y.rem &&
         x.vw == y.vw && x.vh == y.vh && x.color.dark == y.color.dark &&
         x.color.current.space == y.color.current.space &&
         x.color.current.channels == y.color.current.channels &&
         x.color.current.alpha == y.color.current.alpha &&
         x.color.current.missing == y.color.current.missing;
}
} // namespace xl::detail::cssbackground
