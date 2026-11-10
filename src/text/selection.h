#pragma once
#include "document/document.h"

namespace xl::detail {
xl_text_position hit_text(const DocumentFrame& frame, float x, float y);
void validate_selection(const DocumentFrame& frame, const xl_text_selection& selection);
std::vector<xl_node_bounds> selection_ranges(const DocumentFrame& frame, const xl_text_selection& selection);
xl_node_bounds caret_bounds(const DocumentFrame& frame, const xl_text_position& position);
std::string selection_text(const DocumentFrame& frame, const xl_text_selection& selection, bool skip_controls = false);
}
