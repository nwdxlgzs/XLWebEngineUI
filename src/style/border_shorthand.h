#pragma once
#include "document/dom_internal.h"
namespace xl::detail {
void expand_border_shorthands(lxb_css_rule_declaration_list_t* list, std::string_view authored);
void expand_outline_shorthands(lxb_css_rule_declaration_list_t* list, std::string_view authored);
}
