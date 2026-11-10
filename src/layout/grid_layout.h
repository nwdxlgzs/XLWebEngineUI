#pragma once
#include "flex_layout.h"
#include <array>
#include <functional>
#include <string>
#include <vector>

namespace xl::detail {
inline constexpr int grid_track_limit = 256;
enum class GridBreadthKind { fixed, percentage, fraction, automatic, min_content, max_content };
struct GridBreadth {
    GridBreadthKind kind = GridBreadthKind::automatic;
    float value = 0;
};
struct GridTrack {
    GridBreadth minimum, maximum;
    bool fit_content = false;
};
struct GridAxis {
    std::vector<GridTrack> tracks;
    std::vector<std::vector<std::string>> names{{}};
    int repeat_start = -1, repeat_count = 0; // One parsed pattern, expanded for the available size.
    bool auto_fit = false;
};
struct GridLine {
    int number = 0;
    bool span = false;
    std::string name;
};
struct GridPlacement {
    GridLine start, end;
};
struct GridStyle {
    GridAxis columns, rows;
    std::vector<GridTrack> auto_columns{{}}, auto_rows{{}};
    bool column_flow = false, dense = false;
    std::vector<std::vector<std::string>> areas;
    FlexAlign justify_items = FlexAlign::stretch;
};
struct GridMeasure {
    float height = 0, baseline = 0;
};
struct GridItem {
    GridPlacement columns, rows;
    float minimum = 0, min_content = 0, max_content = 0;
    bool baseline = false;
    bool automatic_minimum = true;
    std::function<GridMeasure(float)> measure;
    float x = 0, y = 0, width = 0, height = 0;
    int column = 0, row = 0, column_span = 1, row_span = 1;
};
struct GridResult {
    float width = 0, height = 0;
    GridAxis columns, rows;
    int column_before = 0, row_before = 0;
    std::vector<float> column_starts, column_ends, row_starts, row_ends;
};
std::array<float,4> grid_positioned_area(const GridResult& grid,GridPlacement columns,GridPlacement rows,float width,float height,bool rtl);
GridResult grid_layout(const GridStyle &style, std::vector<GridItem> &items, float width, float height,
                       float column_gap, float row_gap, float row_gap_ratio, FlexAlign justify_content,
                       FlexAlign align_content, int intrinsic = 0);
} // namespace xl::detail
