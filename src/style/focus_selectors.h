#pragma once
#include "document/dom_internal.h"

namespace xl::detail {
// Adapt dynamic pseudo-classes in a parsed selector without altering the DOM
// or the authored selector's specificity. Restore the AST after matching.
class FocusSelectors final {
public:
    explicit FocusSelectors(Document::Impl& document, lxb_dom_node_t* scope = nullptr);
    ~FocusSelectors();
    FocusSelectors(const FocusSelectors&) = delete;
    FocusSelectors& operator=(const FocusSelectors&) = delete;
    void apply(lxb_css_selector_list_t* list);
private:
    struct Change { lxb_css_selector_t* selector; lxb_css_selector_type_t type; lxb_css_selector_pseudo_t pseudo; };
    Document::Impl& document_;
    lxb_dom_node_t* scope_;
    lxb_css_parser_t* parser_ = nullptr;
    std::vector<lxb_css_selector_list_t*> parsed_;
    std::vector<Change> changed_;
    lxb_css_selector_t* replacements_[4]{};
    lxb_css_selector_t* replacement(unsigned kind);
};
}
