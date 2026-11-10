#pragma once
#include <XLWebEngineUI/xl.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace xl::detail {
struct RadiusLength {
    float length = 0, percentage = 0;
    bool operator==(const RadiusLength &) const = default;
    float resolve(float basis) const {
        return static_cast<float>(std::clamp(double(length) + double(percentage) * basis, 0.0, 1e30));
    }
};
struct RadiusPair {
    RadiusLength x, y;
    bool operator==(const RadiusPair &) const = default;
};
using RadiusStyle = std::array<RadiusPair, 4>; // TL, TR, BR, BL
struct CornerRadius {
    float x = 0, y = 0;
    bool operator==(const CornerRadius &) const = default;
};
using CornerRadii = std::array<CornerRadius, 4>;
struct RoundedRect {
    xl_node_bounds rect{};
    CornerRadii radii{};
    bool operator==(const RoundedRect &other) const {
        return rect.x == other.rect.x && rect.y == other.rect.y && rect.width == other.rect.width &&
               rect.height == other.rect.height && radii == other.radii;
    }
    bool curved() const {
        return std::any_of(radii.begin(), radii.end(), [](const auto &r) { return r.x > 0 && r.y > 0; });
    }
    RoundedRect moved(float x, float y) const {
        auto result = *this;
        result.rect.x += x;
        result.rect.y += y;
        return result;
    }
    bool contains(float x, float y) const;
    xl_node_bounds bounds(xl_node_bounds box) const;
    RoundedRect inset(float top, float right, float bottom, float left) const;
};
CornerRadii normalize_radii(CornerRadii radii, float width, float height);
CornerRadii resolve_radii(const RadiusStyle &style, float width, float height);
} // namespace xl::detail
