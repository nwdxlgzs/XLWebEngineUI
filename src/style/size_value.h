#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

namespace xl::detail {
enum class SizeKind { automatic, none, length, percentage, min_content, max_content, fit_content };
enum SizeProperty : size_t {
    size_width,
    size_height,
    size_min_width,
    size_max_width,
    size_min_height,
    size_max_height
};
struct SizeValue {
    SizeKind kind = SizeKind::automatic;
    float value = 0;
    bool intrinsic() const {
        return kind == SizeKind::min_content || kind == SizeKind::max_content ||
               kind == SizeKind::fit_content;
    }
    float resolve(float basis, float fallback) const {
        if (kind == SizeKind::length)
            return value;
        if (kind == SizeKind::percentage && std::isfinite(basis))
            return value * basis;
        return fallback;
    }
};
inline std::pair<float, float> content_limits(float minimum, float maximum, float decoration,
                                              bool border_box) {
    const auto extra = border_box ? decoration : 0;
    const auto lower = std::isfinite(minimum) ? std::max(0.0f, minimum - extra) : 0;
    const auto upper = std::isfinite(maximum) ? std::max(0.0f, maximum - extra) : INFINITY;
    return {lower, std::max(lower, upper)};
}
} // namespace xl::detail
