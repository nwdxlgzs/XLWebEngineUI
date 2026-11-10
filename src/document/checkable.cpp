#include "dom_internal.h"
#include <algorithm>

namespace xl::detail {
namespace {
bool present(lxb_dom_node_t* node, const char* name) {
    return node->type == LXB_DOM_NODE_TYPE_ELEMENT &&
        lxb_dom_element_has_attribute(element(node), chars(name), std::char_traits<char>::length(name));
}
bool labelable(Document::Impl& doc, lxb_dom_node_t* node) {
    if (node->local_name == LXB_TAG_INPUT) return doc.attribute(doc.identify(node), "type", false) != "hidden";
    return node->local_name == LXB_TAG_BUTTON || node->local_name == LXB_TAG_SELECT || node->local_name == LXB_TAG_TEXTAREA ||
        node->local_name == LXB_TAG_METER || node->local_name == LXB_TAG_OUTPUT || node->local_name == LXB_TAG_PROGRESS;
}
bool interactive(Document::Impl& doc, lxb_dom_node_t* node) {
    if (labelable(doc, node) || node->local_name == LXB_TAG_LABEL || node->local_name == LXB_TAG_DETAILS ||
        node->local_name == LXB_TAG_IFRAME || node->local_name == LXB_TAG_EMBED) return true;
    if ((node->local_name == LXB_TAG_A || node->local_name == LXB_TAG_AREA) && present(node, "href")) return true;
    if ((node->local_name == LXB_TAG_AUDIO || node->local_name == LXB_TAG_VIDEO) && present(node, "controls")) return true;
    return present(node, "contenteditable");
}
}
unsigned Document::Impl::checkable(lxb_dom_node_t* node) const {
    if (node->local_name != LXB_TAG_INPUT) return 0;
    size_t size = 0;
    const auto* bytes = lxb_dom_element_get_attribute(element(node), chars("type"), 4, &size);
    std::string type(bytes ? reinterpret_cast<const char*>(bytes) : "", size);
    for (auto& c : type) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return type == "checkbox" ? 1 : type == "radio" ? 2 : 0;
}
std::vector<lxb_dom_node_t*> Document::Impl::radio_group(lxb_dom_node_t* node) {
    const auto name = attribute(identify(node), "name", false);
    if (name.empty()) return {node};
    auto* root = node;
    while (root->parent) root = root->parent;
    const auto owner = [&](lxb_dom_node_t* candidate) -> lxb_dom_node_t* {
        if (present(candidate, "form")) {
            const auto target = by_id(attribute(identify(candidate), "form", false));
            return target && resolve(target)->local_name == LXB_TAG_FORM ? resolve(target) : nullptr;
        }
        for (auto* parent = candidate->parent; parent; parent = parent->parent)
            if (parent->local_name == LXB_TAG_FORM) return parent;
        return nullptr;
    };
    const auto* form = owner(node);
    std::vector<lxb_dom_node_t*> peers, pending{root};
    while (!pending.empty()) {
        auto* candidate = pending.back(); pending.pop_back();
        if (candidate->local_name == LXB_TAG_INPUT) {
            const auto id = identify(candidate);
            if (checkable(candidate) == 2 && attribute(id, "name", false) == name && owner(candidate) == form) peers.push_back(candidate);
        }
        for (auto* child = candidate->last_child; child; child = child->prev) pending.push_back(child);
    }
    return peers;
}
void Document::Impl::activate(xl_node_id id, bool trusted) {
    auto* node = resolve(id);
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT || disabled(node) || inert(node) ||
        attribute(id, "aria-disabled", false) == "true" || !activating.insert(id).second) return;
    struct Guard { Impl& doc; xl_node_id id; ~Guard() { doc.activating.erase(id); } } guard{*this, id};
    const auto kind = checkable(node);
    const bool previous = kind && checked(id);
    xl_node_id previous_radio = 0;
    if (kind == 2)
        for (auto* peer : radio_group(node)) if (peer != node && checked(identify(peer))) previous_radio = identify(peer);
    const auto restore = [&] {
        if (kind == 1) checked(id, previous);
        if (kind == 2) {
            checked(id, previous);
            if (previous_radio && checkable(resolve(previous_radio)) == 2) {
                const auto group = radio_group(node);
                if (std::find(group.begin(), group.end(), resolve(previous_radio)) != group.end()) checked(previous_radio, true);
            }
        }
    };
    if (kind) checked(id, kind == 1 ? !previous : true);
    bool allowed;
    try { allowed = dispatch(id, "click", nullptr, 0, true, true, trusted); }
    catch (...) { restore(); throw; }
    if (!allowed) { restore(); return; }
    if (kind) {
        if (kind == 1 || !previous) {
            dispatch(id, "input", nullptr, 0, true, false);
            dispatch(id, "change", nullptr, 0, true, false);
        }
        return;
    }
    for (auto* candidate = node; candidate && candidate->type == LXB_DOM_NODE_TYPE_ELEMENT; candidate = candidate->parent) {
        auto* owner = disclosure_owner(candidate);
        if (owner && (candidate == node || candidate->local_name == LXB_TAG_SUMMARY) &&
            (candidate->local_name != LXB_TAG_DETAILS || trusted)) {
            const auto owner_id = identify(owner);
            if (details_open(owner)) remove_attribute(owner_id, "open");
            else set_attribute(owner_id, "open", "");
            return;
        }
        if (interactive(*this, candidate)) break;
    }
    // A label forwards activation, including to display:none checkable inputs.
    // Interactive descendants retain their own activation behavior.
    auto* label = node;
    for (; label && label->type == LXB_DOM_NODE_TYPE_ELEMENT; label = label->parent) {
        if (label->local_name == LXB_TAG_LABEL) break;
        if (labelable(*this, label) || (label->local_name == LXB_TAG_A && present(label, "href"))) return;
    }
    if (!label || label->local_name != LXB_TAG_LABEL || !connected(label)) return;
    lxb_dom_node_t* target = nullptr;
    const auto label_id = identify(label);
    if (present(label, "for")) {
        if (const auto candidate = by_id(attribute(label_id, "for", false))) target = resolve(candidate);
    } else {
        std::vector<lxb_dom_node_t*> pending;
        for (auto* child = label->last_child; child; child = child->prev) pending.push_back(child);
        while (!pending.empty()) {
            auto* child = pending.back(); pending.pop_back();
            if (child->type == LXB_DOM_NODE_TYPE_ELEMENT && labelable(*this, child)) { target = child; break; }
            for (auto* nested = child->last_child; nested; nested = nested->prev) pending.push_back(nested);
        }
    }
    if (target && labelable(*this, target) && !disabled(target) && !inert(target)) {
        const auto control = identify(target);
        set_focus(control);
        // Focus listeners may change the association or disconnect the target.
        if (connected(label) && connected(target)) activate(control, trusted);
    }
}
}
