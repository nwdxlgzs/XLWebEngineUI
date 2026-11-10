#pragma once
#include "document/dom_internal.h"
#include "color.h"
#include "background.h"
#include "position.h"
#include <optional>

namespace xl::detail {
struct ActiveStyles;
struct ColorDeclaration { std::string value; csscolor::Color expression; };
// Element-specific computed declarations live separately from author ASTs.
// Clear the element cascade before destroying this generation's storage.
class VariableStyles {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    VariableStyles();
    ~VariableStyles();
    bool apply(Document::Impl& document, const std::vector<lxb_dom_element_t*>& elements, const ActiveStyles& active);
    std::optional<std::string> custom(lxb_dom_element_t* element, std::string_view name) const;
    const ColorDeclaration* color(lxb_dom_element_t* element, std::string_view name) const;
    std::shared_ptr<const cssbackground::Declaration> background(lxb_dom_element_t* element, std::string_view name) const;
    std::shared_ptr<const cssposition::Declaration> position(lxb_dom_element_t* element, std::string_view name) const;
};
}
