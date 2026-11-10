#pragma once
#include "color.h"
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace xl::detail::cssbackground {
// Percentages remain unresolved until the gradient/tile has a concrete size.
struct Length {
  double px = 0, percent = 0, em = 0, rem = 0, vw = 0, vh = 0, vmin = 0,
         vmax = 0;
};
// Shared private CSS length expression parser; no property-specific clamping.
std::optional<Length> parse_length(std::string_view source, bool* percentage = nullptr);
struct Axis {
  Length offset{0, .5};
  bool end = false;
};
struct Position {
  Axis x, y;
};
struct Size {
  Length x, y;
  bool auto_x = true, auto_y = true;
  unsigned fit = 0;
};
enum class Repeat { repeat, none, round, space };
enum class Box { border, padding, content };
enum class Attachment { scroll, fixed, local };
struct Stop {
  csscolor::Color color; // Null is an interpolation hint.
  std::optional<Length> position;
};
struct Gradient {
  enum Kind { linear, radial, conic } kind = linear;
  bool repeating = false, circle = false, explicit_space = false;
  csscolor::Space space = csscolor::Space::srgb;
  unsigned hue = 0, extent = 3; // closest-side/corner, farthest-side/corner
  double angle = 180;
  int direction_x = 0, direction_y = 0;
  Position center;
  std::vector<Length> radii;
  std::vector<Stop> stops;
};
using Image = std::shared_ptr<const Gradient>;
inline constexpr const char *properties[]{
    "background-image",     "background-position", "background-size",
    "background-repeat",    "background-origin",   "background-clip",
    "background-attachment"};
struct Style {
  std::vector<Image> images{nullptr};
  std::vector<Position> positions{Position{Axis{Length{}}, Axis{Length{}}}};
  std::vector<Size> sizes{Size{}};
  std::vector<std::array<Repeat, 2>> repeats{{Repeat::repeat, Repeat::repeat}};
  std::vector<Box> origins{Box::padding}, clips{Box::border};
  std::vector<Attachment> attachments{Attachment::scroll};
  std::array<std::string, 7> values{
      "none", "0% 0%", "auto", "repeat", "padding-box", "border-box", "scroll"};
};
struct Declaration {
  Style style;
  std::string wide, color = "transparent";
};
bool property(std::string_view name);
std::shared_ptr<const Declaration> parse(std::string_view name,
                                         std::string_view value);
void assign(Style &target, const Style &source, size_t property);
bool initial(const Style &value);
struct Environment {
  double em = 16, rem = 16, vw = 0, vh = 0;
  csscolor::Context color{};
};
double resolve(const Length &value, double basis,
               const Environment &environment);
struct Paint {
  Style style;
  Environment environment;
  std::array<float, 4> padding{};
  std::array<float, 4> border{};
  struct Cache;
  mutable std::shared_ptr<Cache> cache;
};
bool equal(const std::shared_ptr<const Paint> &a,
           const std::shared_ptr<const Paint> &b);
} // namespace xl::detail::cssbackground
