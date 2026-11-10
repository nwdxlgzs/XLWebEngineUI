#pragma once
#include "document/dom_internal.h"
namespace xl::detail {
// Adapter-only value absent from the locked Lexbor keyword table. Kept in a
// native alignment declaration so the normal cascade still decides its winner.
inline constexpr lxb_css_value_type_t css_space_evenly = 0x7f000001;
inline constexpr lxb_css_value_type_t css_fit_content = 0x7f000002;
void expand_layout_shorthands(lxb_css_rule_declaration_list_t *list);
} // namespace xl::detail
