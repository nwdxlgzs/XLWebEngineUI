#pragma once
#include "document/dom_internal.h"

namespace xl::detail {
// Expand recognized font declarations in the parser AST before cascade.
// Authored HTML/style attributes remain unchanged. Lists and generated nodes
// share the document CSS allocator and retain their original source position.
void expand_font_shorthands(lxb_css_rule_declaration_list_t* list, std::string_view authored);
std::optional<TextFormat> canvas_font(std::string_view source);
}
