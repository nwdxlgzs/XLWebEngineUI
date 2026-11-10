#include "rounded_rect.h"

namespace xl::detail {
CornerRadii normalize_radii(CornerRadii radii, float width, float height) {
    double factor = 1;
    const auto fit = [&](double sum, double extent) {
        if (sum > extent && sum > 0)
            factor = std::min(factor, std::max(0.0, extent) / sum);
    };
    for (auto &radius : radii)
        if (radius.x <= 0 || radius.y <= 0)
            radius = {};
    fit(double(radii[0].x) + radii[1].x, width);
    fit(double(radii[3].x) + radii[2].x, width);
    fit(double(radii[0].y) + radii[3].y, height);
    fit(double(radii[1].y) + radii[2].y, height);
    for (auto &radius : radii) {
        radius.x = static_cast<float>(radius.x * factor);
        radius.y = static_cast<float>(radius.y * factor);
    }
    return radii;
}
CornerRadii resolve_radii(const RadiusStyle &style, float width, float height) {
    CornerRadii radii;
    for (size_t i = 0; i < 4; ++i)
        radii[i] = {style[i].x.resolve(width), style[i].y.resolve(height)};
    return normalize_radii(radii, width, height);
}
bool RoundedRect::contains(float x, float y) const {
    if (x < rect.x || x >= rect.x + rect.width || y < rect.y || y >= rect.y + rect.height)
        return false;
    for (size_t i = 0; i < 4; ++i) {
        const auto radius = radii[i];
        if (radius.x <= 0 || radius.y <= 0)
            continue;
        const bool right = i == 1 || i == 2, bottom = i >= 2;
        const auto cx = right ? rect.x + rect.width - radius.x : rect.x + radius.x;
        const auto cy = bottom ? rect.y + rect.height - radius.y : rect.y + radius.y;
        if ((right ? x > cx : x < cx) && (bottom ? y > cy : y < cy)) {
            const double dx = (double(x) - cx) / radius.x, dy = (double(y) - cy) / radius.y;
            if (dx * dx + dy * dy > 1)
                return false;
        }
    }
    return true;
}
xl_node_bounds RoundedRect::bounds(xl_node_bounds box) const {
    double left = std::max(box.x, rect.x), right = std::min(box.x + box.width, rect.x + rect.width);
    double top = std::max(box.y, rect.y), bottom = std::min(box.y + box.height, rect.y + rect.height);
    if (right <= left || bottom <= top)
        return {static_cast<float>(left), static_cast<float>(top), 0, 0};
    const auto source_left = left, source_right = right, source_top = top, source_bottom = bottom;
    for (size_t i = 0; i < 4; ++i) {
        const auto radius = radii[i];
        if (radius.x <= 0 || radius.y <= 0)
            continue;
        const bool is_right = i == 1 || i == 2, is_bottom = i >= 2;
        const double cx = is_right ? rect.x + rect.width - radius.x : rect.x + radius.x;
        const double cy = is_bottom ? rect.y + rect.height - radius.y : rect.y + radius.y;
        const double near_y = std::clamp(cy, source_top, source_bottom),
                     near_x = std::clamp(cx, source_left, source_right);
        if (is_bottom ? source_top > cy : source_bottom < cy) {
            const auto dy = (near_y - cy) / radius.y;
            const auto extent = radius.x * std::sqrt(std::max(0.0, 1 - dy * dy));
            if (is_right)
                right = std::min(right, cx + extent);
            else
                left = std::max(left, cx - extent);
        }
        if (is_right ? source_left > cx : source_right < cx) {
            const auto dx = (near_x - cx) / radius.x;
            const auto extent = radius.y * std::sqrt(std::max(0.0, 1 - dx * dx));
            if (is_bottom)
                bottom = std::min(bottom, cy + extent);
            else
                top = std::max(top, cy - extent);
        }
    }
    return {static_cast<float>(left), static_cast<float>(top), static_cast<float>(std::max(0.0, right - left)),
            static_cast<float>(std::max(0.0, bottom - top))};
}
RoundedRect RoundedRect::inset(float top, float right, float bottom, float left) const {
    auto result = *this;
    const auto width = std::max(0.0f, rect.width - left - right), height = std::max(0.0f, rect.height - top - bottom);
    result.rect = {rect.x + left, rect.y + top, width, height};
    const float xs[]{left, right, right, left}, ys[]{top, top, bottom, bottom};
    for (size_t i = 0; i < 4; ++i)
        result.radii[i] = radii[i].x && radii[i].y
                              ? CornerRadius{std::max(0.0f, radii[i].x - xs[i]), std::max(0.0f, radii[i].y - ys[i])}
                              : CornerRadius{};
    result.radii = normalize_radii(result.radii, width, height);
    return result;
}
} // namespace xl::detail
