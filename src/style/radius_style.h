#pragma once
#include "document/dom_internal.h"
#include "graphics/rounded_rect.h"
namespace xl::detail {
inline constexpr const char *radius_names[]{"border-top-left-radius", "border-top-right-radius",
                                            "border-bottom-right-radius", "border-bottom-left-radius"};
bool radius_property(std::string_view name);
bool valid_radius(std::string_view value, bool shorthand);
void expand_radius_shorthands(lxb_css_rule_declaration_list_t *list, std::string_view authored);
RadiusStyle radius_style(lxb_dom_node_t *node, const RadiusStyle &inherited, float em, float rem, int vw, int vh,
                         uint64_t &unsupported);
} // namespace xl::detail
