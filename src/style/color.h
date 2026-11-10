#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace xl::detail::csscolor {
enum class Space {
    srgb,
    linear_srgb,
    p3,
    linear_p3,
    a98,
    prophoto,
    rec2020,
    xyz65,
    xyz50,
    lab,
    lch,
    oklab,
    oklch,
    hsl,
    hwb
};
struct Value {
    Space space = Space::srgb;
    std::array<double, 3> channels{};
    double alpha = 1;
    unsigned missing = 0;
};
struct Context {
    Value current;
    bool dark = false;
};
struct Expression;
using Color = std::shared_ptr<const Expression>;
Color parse(std::string_view source);
Value resolve(const Color &expression, const Context &context = {});
Value convert(Value value, Space space);
Value interpolate(Value first, Value second, Space space, double position, unsigned hue_method = 0);
Value output_srgb(Value value);
bool legacy(const Color& expression);
uint32_t argb(Value value);
Value from_argb(uint32_t value);
bool property(std::string_view name);
bool valid(std::string_view value);
bool valid_scheme(std::string_view value);
std::string trim(std::string_view value);
bool dark_scheme(std::string_view value, bool inherited, bool preferred);
} // namespace xl::detail::csscolor
