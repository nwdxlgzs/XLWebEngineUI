#include "focus_selectors.h"
#include <algorithm>

namespace xl::detail {
FocusSelectors::FocusSelectors(Document::Impl& document, lxb_dom_node_t* scope) : document_(document), scope_(scope) {}
FocusSelectors::~FocusSelectors() {
    for (auto it = changed_.rbegin(); it != changed_.rend(); ++it) {
        it->selector->type = it->type; it->selector->u.pseudo = it->pseudo;
    }
    for (auto* list : parsed_) lxb_css_selector_list_destroy_memory(list);
    if (parser_) lxb_css_parser_destroy(parser_, true);
}
lxb_css_selector_t* FocusSelectors::replacement(unsigned kind) {
    if (replacements_[kind]) return replacements_[kind];
    auto& doc = document_;
    std::vector<lxb_dom_node_t*> chain;
    if (kind < 3 && doc.focused && (kind != 2 || doc.focus_visible)) {
        auto* node = doc.resolve(doc.focused);
        if (doc.connected(node) && !doc.disabled(node) && !doc.inert(node))
            for (; node && node->type == LXB_DOM_NODE_TYPE_ELEMENT; node = node->parent) chain.push_back(node);
    }
    std::string selector = ":not(*)";
    if (!chain.empty()) {
        if (chain.size() > 256) throw DocumentFailure{XL_UNSUPPORTED};
        std::reverse(chain.begin(), chain.end());
        std::string path = ":root", alternatives;
        for (size_t i = 0; i < chain.size(); ++i) {
            if (i) {
                size_t index = 1;
                for (auto* previous = chain[i]->prev; previous; previous = previous->prev)
                    if (previous->type == LXB_DOM_NODE_TYPE_ELEMENT) ++index;
                path += ">:nth-child(" + std::to_string(index) + ')';
            }
            if (kind == 1 || i + 1 == chain.size()) {
                if (!alternatives.empty()) alternatives += ',';
                alternatives += path;
            }
        }
        if (alternatives.size() > 1024 * 1024) throw DocumentFailure{XL_UNSUPPORTED};
        selector = ":is(" + alternatives + ')';
    }
    if (kind == 3) {
        auto* root = scope_ ? scope_ : lxb_dom_interface_node(doc.dom);
        while (root->parent) root = root->parent;
        std::string alternatives;
        std::vector<lxb_dom_node_t*> pending{root};
        while (!pending.empty()) {
            auto* node = pending.back(); pending.pop_back();
            const bool selected = node->local_name == LXB_TAG_OPTION &&
                lxb_dom_element_has_attribute(element(node), chars("selected"), 8);
            if (selected || (doc.checkable(node) && doc.checked(doc.identify(node)))) {
                chain.clear();
                for (auto* parent = node; parent && parent->type == LXB_DOM_NODE_TYPE_ELEMENT; parent = parent->parent)
                    chain.push_back(parent);
                if (chain.size() > 256) throw DocumentFailure{XL_UNSUPPORTED};
                std::reverse(chain.begin(), chain.end());
                std::string path = doc.connected(node) ? ":root" : ":not(* > *)";
                for (size_t i = 1; i < chain.size(); ++i) {
                    size_t index = 1;
                    for (auto* previous = chain[i]->prev; previous; previous = previous->prev)
                        if (previous->type == LXB_DOM_NODE_TYPE_ELEMENT) ++index;
                    path += ">:nth-child(" + std::to_string(index) + ')';
                }
                if (!alternatives.empty()) alternatives += ',';
                alternatives += path;
                if (alternatives.size() > 1024 * 1024) throw DocumentFailure{XL_UNSUPPORTED};
            }
            for (auto* child = node->last_child; child; child = child->prev) pending.push_back(child);
        }
        if (!alternatives.empty()) selector = ":is(" + alternatives + ')';
    }
    if (!parser_) {
        parser_ = lxb_css_parser_create();
        if (!parser_) throw std::bad_alloc();
        check_lxb(lxb_css_parser_init(parser_, nullptr));
    }
    auto* list = lxb_css_selectors_parse(parser_, chars(selector), selector.size());
    if (!list || parser_->status != LXB_STATUS_OK) {
        if (list) lxb_css_selector_list_destroy_memory(list);
        throw DocumentFailure{XL_INTERNAL_ERROR};
    }
    parser_->memory = nullptr; // Each replacement list owns its own arena.
    try { parsed_.push_back(list); }
    catch (...) { lxb_css_selector_list_destroy_memory(list); throw; }
    return replacements_[kind] = list->first;
}
void FocusSelectors::apply(lxb_css_selector_list_t* list) {
    std::vector<lxb_css_selector_list_t*> pending{list};
    while (!pending.empty()) {
        auto* head = pending.back(); pending.pop_back();
        for (auto* current = head; current; current = current->next)
            for (auto* selector = current->first; selector; selector = selector->next) {
                if (selector->type != LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS &&
                    selector->type != LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION) continue;
                const auto type = selector->u.pseudo.type;
                if (selector->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS &&
                    (type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS || type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_WITHIN ||
                     type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_VISIBLE || type == LXB_CSS_SELECTOR_PSEUDO_CLASS_CHECKED)) {
                    const unsigned kind = type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_WITHIN ? 1 :
                        type == LXB_CSS_SELECTOR_PSEUDO_CLASS_FOCUS_VISIBLE ? 2 : type == LXB_CSS_SELECTOR_PSEUDO_CLASS_CHECKED ? 3 : 0;
                    auto* source = replacement(kind);
                    changed_.push_back({selector, selector->type, selector->u.pseudo});
                    selector->type = source->type; selector->u.pseudo = source->u.pseudo;
                } else if (selector->type == LXB_CSS_SELECTOR_TYPE_PSEUDO_CLASS_FUNCTION && selector->u.pseudo.data) {
                    switch (type) {
                    case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_IS:
                    case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_WHERE:
                    case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NOT:
                    case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_HAS:
                        pending.push_back(static_cast<lxb_css_selector_list_t*>(selector->u.pseudo.data)); break;
                    case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_CHILD:
                    case LXB_CSS_SELECTOR_PSEUDO_CLASS_FUNCTION_NTH_LAST_CHILD:
                        pending.push_back(static_cast<lxb_css_selector_anb_of_t*>(selector->u.pseudo.data)->of); break;
                    default: break;
                    }
                }
            }
    }
}
}
