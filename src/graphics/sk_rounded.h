#pragma once
#include "include/core/SkRRect.h"
#include "rounded_rect.h"
namespace xl::detail {
inline SkRRect sk_rounded(const RoundedRect &value) {
    SkVector radii[4];
    for (size_t i = 0; i < 4; ++i)
        radii[i] = {value.radii[i].x, value.radii[i].y};
    SkRRect result;
    result.setRectRadii(SkRect::MakeXYWH(value.rect.x, value.rect.y, value.rect.width, value.rect.height), radii);
    return result;
}
} // namespace xl::detail
