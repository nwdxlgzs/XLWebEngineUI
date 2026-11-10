#pragma once
#include "document/dom_internal.h"
#include "layout/flex_layout.h"
#include "layout/grid_layout.h"
#include "size_value.h"
#include "color.h"
#include "background.h"
#include "position.h"
#include <cmath>

namespace xl::detail {
enum class Overflow { visible, hidden, scroll, automatic, clip };
inline bool scrollable(Overflow value) { return value != Overflow::visible && value != Overflow::clip; }
inline bool user_scrollable(Overflow value) { return value == Overflow::scroll || value == Overflow::automatic; }
struct BoxStyle {
    xl_node_id paint_owner = 0;
    xl_node_id absolute_container = 0;
    cssposition::Style position;
    float relative_x = 0, relative_y = 0;
    // Accumulated offsets of inline ancestors in this formatting context.
    float inline_x = 0, inline_y = 0;
    float width = NAN, height = NAN;
    std::array<SizeValue, 6> sizes{}; // Computed lengths/percentages, independent of their current containing block.
    float top = 0, right = 0, bottom = 0, left = 0;
    float padding_top = 0, padding_right = 0, padding_bottom = 0, padding_left = 0;
    std::array<BorderSide, 4> borders{};
    RadiusStyle radii{};
    OutlineStyle outline;
    float font_size = 16, root_font_size = 16;
    uint32_t color = 0xff000000, background = 0;
    csscolor::Value foreground;
    csscolor::Value background_color{csscolor::Space::srgb,{0,0,0},0,0};
    std::array<csscolor::Value,4> border_colors{};
    csscolor::Value outline_color;
    std::shared_ptr<const cssbackground::Style> background_style;
    std::shared_ptr<const cssbackground::Paint> background_paint;
    bool dark = false;
    int weight = 400;
    bool italic = false, inline_flow = false, rtl = false;
    bool inline_atomic = false, width_percentage = false;
    bool flex_container = false;
    bool grid_container = false;
    bool text_control = false;
    bool replaced = false;
    std::shared_ptr<class ImageContent> image;
    GridStyle grid;
    GridPlacement grid_columns, grid_rows;
    FlexAlign justify_self = FlexAlign::automatic;
    FlexStyle flex;
    float grow = 0, shrink = 1, basis = NAN;
    bool basis_percentage = false, basis_content = false;
    FlexAlign align_self = FlexAlign::automatic;
    int order = 0;
    float min_width = NAN, min_height = NAN, max_width = INFINITY, max_height = INFINITY;
    float height_ratio = NAN;
    bool height_definite = false;
    float row_gap_ratio = NAN, column_gap_ratio = NAN;
    bool oblique = false;
    int stretch = 5;
    uint32_t text_align = 0;
    std::vector<std::string> families;
    WhiteSpace whitespace = WhiteSpace::normal;
    float line_height = -1, tab_size = 8;
    bool line_height_factor = true, tab_is_length = false;
    bool hidden = false, border_box = false;
    bool disclosure_marker = false;
    Overflow overflow_x = Overflow::visible, overflow_y = Overflow::visible;
    float vertical_shift = 0, baseline_shift = 0;
    float vertical_percentage = NAN;
    uint32_t vertical_keyword = 0;
    uint64_t alignment_group = 0;
    uint64_t alignment_parent = 0;
    float alignment_offset = 0;
    uint64_t unsupported = 0;
};
BoxStyle box_style(Document::Impl& doc, lxb_dom_node_t* node, float containing_width, int viewport_width,
                   int viewport_height, const BoxStyle& inherited, float containing_height = NAN);
WhiteSpace white_space(lxb_dom_node_t* node, WhiteSpace inherited);
} // namespace xl::detail
