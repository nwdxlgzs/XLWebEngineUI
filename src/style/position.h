#pragma once
#include "background.h"
#include <utility>

namespace xl::detail::cssposition {
enum class Kind { normal, relative, absolute, fixed, sticky };
inline constexpr const char *properties[]{"position", "top", "right", "bottom",
                                          "left", "z-index", "isolation"};
struct Style {
  Kind kind = Kind::normal;
  // Computed absolute lengths plus unresolved percentages; null is auto.
  std::array<std::optional<cssbackground::Length>, 4> inset{};
  std::array<bool, 4> percentages{};
  std::optional<int32_t> z;
  bool isolate = false;
};
struct Declaration {
  Style style;
  std::string wide;
  std::array<std::string, 7> values{"static", "auto", "auto", "auto", "auto", "auto", "auto"};
};
bool property(std::string_view name);
std::shared_ptr<const Declaration> parse(std::string_view name,
                                         std::string_view source);
std::pair<float, float> relative(const Style &style, float width, float height,
                                 bool containing_rtl);
} // namespace xl::detail::cssposition
