#pragma once
#include "document/dom_internal.h"
#include <functional>

namespace xl::detail {
enum class PseudoKind : unsigned { none, before, after };
class SelectorCandidates {
public:
    SelectorCandidates(Document::Impl& document,const std::vector<lxb_dom_element_t*>& elements);
    const std::vector<lxb_dom_node_t*>* find(lxb_css_selector_list_t* list) const;
private:
    Document::Impl& document_;
    std::unordered_map<std::string,std::vector<lxb_dom_node_t*>> nodes_;
    const std::vector<lxb_dom_node_t*> empty_;
};
// Private render nodes have stable identities but never join the author tree.
class PseudoElements {
    struct Impl;
    std::unique_ptr<Impl> impl_;
public:
    explicit PseudoElements(Document::Impl& document);
    ~PseudoElements();
    lxb_dom_node_t* ensure(lxb_dom_node_t* origin, PseudoKind kind);
    lxb_dom_node_t* get(lxb_dom_node_t* origin, PseudoKind kind) const;
    lxb_dom_node_t* origin(lxb_dom_node_t* node) const;
    bool generated(lxb_dom_node_t* node) const;
    std::vector<lxb_dom_element_t*> elements() const;
    void reset();
    void compute();
    uint64_t unsupported(lxb_dom_node_t* origin) const;
    std::optional<std::string> alternative(lxb_dom_node_t* node) const;
    std::string content(lxb_dom_node_t* node) const;
    static bool valid_content(std::string_view value);
    static bool supported_selector(lxb_css_selector_list_t* list);
    static void match(Document::Impl& document, lxb_css_selector_list_t* list,
        const std::function<void(lxb_dom_node_t*, PseudoKind, lxb_css_selector_specificity_t)>& callback,
        const SelectorCandidates* candidates=nullptr);
};
PseudoKind pseudo_kind(std::string_view name);
}
