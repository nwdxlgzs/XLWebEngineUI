#include "graphics/clip.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value)) {                                                                                                \
            std::cerr << __LINE__ << ": " #value "\n";                                                                 \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)
int main() {
    using namespace xl::detail;
    RoundedRect circle{{0, 0, 80, 80}, {{{40, 40}, {40, 40}, {40, 40}, {40, 40}}}};
    CHECK(!circle.contains(0, 0) && !circle.contains(10, 10) && circle.contains(20, 20) && circle.contains(40, 1));
    const auto removed = circle.bounds({0, 0, 5, 5});
    CHECK(removed.width == 0 && removed.height == 0);
    const auto quadrant = circle.bounds({0, 0, 20, 20});
    CHECK(std::abs(quadrant.x - (40 - std::sqrt(1200.f))) < .001f);
    CHECK(quadrant.x == quadrant.y);
    const auto inner = circle.inset(8, 8, 8, 8);
    CHECK(inner.rect.x == 8 && inner.rect.width == 64 && inner.radii[0].x == 32);
    const auto moved = circle.moved(20, 10);
    CHECK(moved.contains(60, 50) && !moved.contains(21, 11));
    Clip clip;
    clip = clip.rounded(circle);
    CHECK(clip.contains(40, 40) && !clip.contains(2, 2));
    const auto translated = clip.moved(20, 10);
    CHECK(translated.contains(60, 50) && !translated.contains(22, 12));
    CHECK(clip != translated && clip == clip.moved(0, 0));
    const auto intersection = clip.intersect(Clip{}.rounded(moved));
    CHECK(intersection.contains(50, 40) && !intersection.contains(10, 40));
    CornerRadii overlapping{{{80, 20}, {80, 20}, {80, 20}, {80, 20}}};
    const auto fit = normalize_radii(overlapping, 80, 60);
    CHECK(fit[0].x == 40 && fit[0].y == 10);
    CHECK(normalize_radii({{{40, 0}, {0, 0}, {0, 0}, {0, 0}}}, 80, 80)[0] == CornerRadius{});
    auto deep = clip;
    for (int i = 0; i < 255; ++i)
        deep = deep.intersect(clip);
    CHECK(deep.curves->size() == 1); // Identity shares the immutable set.
    deep = clip;
    for (int i = 0; i < 255; ++i)
        deep = deep.intersect(Clip{}.rounded(circle));
    CHECK(deep.curves->size() == 256);
    bool limited = false;
    try {
        deep = deep.intersect(Clip{}.rounded(circle));
    } catch (const std::length_error &) {
        limited = true;
    }
    CHECK(limited);
    std::cout
        << "rounded ellipse geometry, overlap scaling, empty/tight bounds, inset, move and clip intersection passed\n";
}
