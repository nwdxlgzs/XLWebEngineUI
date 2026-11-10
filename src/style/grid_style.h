#pragma once
#include "document/dom_internal.h"
#include "layout/grid_layout.h"
#include <optional>
#include <string_view>

namespace xl::detail {
struct GridUnits {
    float font = 16, root = 16, width = 0, height = 0;
};
std::optional<GridAxis> grid_tracks(std::string_view value, const GridUnits &units = {},
                                    bool implicit = false);
std::optional<GridLine> grid_line(std::string_view value);
std::optional<std::vector<std::vector<std::string>>> grid_areas(std::string_view value);
std::optional<FlexAlign> grid_alignment(std::string_view value);
// Top-level CSS components, keeping functions/strings/line-name groups intact.
std::vector<std::string> grid_components(std::string_view value, char delimiter = 0);
void expand_grid_shorthands(lxb_css_rule_declaration_list_t *list);
} // namespace xl::detail
