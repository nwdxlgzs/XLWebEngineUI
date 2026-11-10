#include "dom_internal.h"

namespace xl::detail {
lxb_dom_node_t* Document::Impl::details_summary(lxb_dom_node_t* node) const {
    if (!node || node->local_name != LXB_TAG_DETAILS) return nullptr;
    for (auto* child = node->first_child; child; child = child->next)
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT && child->local_name == LXB_TAG_SUMMARY) return child;
    return nullptr;
}
lxb_dom_node_t* Document::Impl::disclosure_owner(lxb_dom_node_t* node) const {
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT) return nullptr;
    if (node->local_name == LXB_TAG_SUMMARY && node->parent && details_summary(node->parent) == node) return node->parent;
    // A missing author summary uses the details handle for its native legend.
    if (node->local_name == LXB_TAG_DETAILS && !details_summary(node)) return node;
    return nullptr;
}
bool Document::Impl::details_open(lxb_dom_node_t* node) const {
    return node && node->local_name == LXB_TAG_DETAILS && lxb_dom_element_has_attribute(element(node), chars("open"), 4);
}
bool Document::Impl::details_hidden(lxb_dom_node_t* node) const {
    for (auto* child = node; child && child->parent; child = child->parent) {
        auto* parent = child->parent;
        if (parent->local_name == LXB_TAG_DETAILS && !details_open(parent) && details_summary(parent) != child) return true;
    }
    return false;
}
void Document::Impl::details_attribute(lxb_dom_node_t* node, std::string_view name, bool was_open) {
    if (node->local_name != LXB_TAG_DETAILS) return;
    const bool open = details_open(node);
    if (name == "open" && open != was_open) {
        const auto id = identify(node);
        const auto found = details_toggles.find(id);
        const bool initial = found == details_toggles.end() ? was_open : found->second.old_open;
        details_toggles.insert_or_assign(id, DetailsToggle{initial, open, std::chrono::steady_clock::now(), next_task_order++});
    }
    if (name == "name") details_exclusive(node, false);
    else if (name == "open" && open && !was_open) details_exclusive(node, true);
}
void Document::Impl::details_exclusive(lxb_dom_node_t* node, bool close_others) {
    if (!details_open(node)) return;
    const auto name = attribute(identify(node), "name", false);
    if (name.empty()) return;
    auto* root = node;
    while (root->parent) root = root->parent;
    std::vector<lxb_dom_node_t*> pending{root};
    while (!pending.empty()) {
        auto* peer = pending.back(); pending.pop_back();
        if (peer != node && details_open(peer) && attribute(identify(peer), "name", false) == name) {
            remove_attribute(identify(close_others ? peer : node), "open");
            if (!close_others) return;
        }
        for (auto* child = peer->last_child; child; child = child->prev) pending.push_back(child);
    }
}
void Document::Impl::details_inserted(lxb_dom_node_t* root, bool parsed, bool include_root) {
    // A detached subtree may already contain more than one open member from
    // parsing/cloning. Close conflicts in tree order before joining its peers.
    std::vector<lxb_dom_node_t*> pending{root}, opened;
    while (!pending.empty()) {
        auto* node = pending.back(); pending.pop_back();
        if ((include_root || node != root) && details_open(node)) {
            if (parsed) {
                const auto id = identify(node);
                details_toggles.insert_or_assign(id, DetailsToggle{false, true, std::chrono::steady_clock::now(), next_task_order++});
            }
            opened.push_back(node);
        }
        for (auto* child = node->last_child; child; child = child->prev) pending.push_back(child);
    }
    // Earlier parsed members win; existing members outside an inserted subtree
    // win over the newly inserted open member.
    for (auto it = opened.rbegin(); it != opened.rend(); ++it) details_exclusive(*it, false);
}
}
