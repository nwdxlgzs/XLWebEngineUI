#pragma once
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

namespace xl::detail {
enum class FlexAlign {
    automatic,
    start,
    end,
    center,
    between,
    around,
    evenly,
    stretch,
    baseline,
    flow_start,
    flow_end
};
struct FlexStyle {
    bool column = false, reverse = false;
    int wrap = 0; // 0 nowrap, 1 wrap, 2 wrap-reverse
    FlexAlign justify = FlexAlign::start, items = FlexAlign::stretch, content = FlexAlign::stretch;
    float row_gap = 0, column_gap = 0;
};
struct FlexMeasure {
    float cross = 0, baseline = 0;
};
struct FlexItem {
    float base = 0, minimum = 0, maximum = std::numeric_limits<float>::infinity();
    float cross_min = 0, cross_max = std::numeric_limits<float>::infinity();
    float grow = 0, shrink = 1;
    float before = 0, after = 0, cross_before = 0, cross_after = 0; // NaN = auto margin.
    bool cross_auto = true;
    FlexAlign align = FlexAlign::automatic;
    // Natural cross measurement after flexing, then an optional stretched cross
    // size. The host keeps the matching final child layout for placement.
    std::function<FlexMeasure(float, float)> measure;
    float main = 0, cross = 0, main_position = 0, cross_position = 0, baseline = 0;
};
struct FlexResult {
    float main = 0, cross = 0;
};
FlexResult flex_layout(const FlexStyle &style, std::vector<FlexItem> &items, float main, float cross);
} // namespace xl::detail
