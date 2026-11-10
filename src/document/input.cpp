#include "dom_internal.h"
#include "style/computed.h"
#include "style/pseudo_elements.h"
#include <algorithm>
#include <charconv>
#include <optional>
#include <limits>

namespace xl::detail {
namespace {
bool has(lxb_dom_node_t* node, const char* name) {
    return node->type == LXB_DOM_NODE_TYPE_ELEMENT &&
        lxb_dom_element_has_attribute(element(node), chars(name), std::char_traits<char>::length(name));
}
bool control(lxb_dom_node_t* node) {
    return node->local_name == LXB_TAG_INPUT || node->local_name == LXB_TAG_BUTTON ||
           node->local_name == LXB_TAG_SELECT || node->local_name == LXB_TAG_TEXTAREA ||
           node->local_name == LXB_TAG_FIELDSET || node->local_name == LXB_TAG_OPTGROUP || node->local_name == LXB_TAG_OPTION;
}
std::optional<int> tab_index(Document::Impl& doc, lxb_dom_node_t* node) {
    if (!has(node, "tabindex")) return {};
    auto value = doc.attribute(doc.identify(node), "tabindex", false);
    const auto first = value.find_first_not_of(" \t\r\n\f");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n\f");
    auto text = std::string_view(value).substr(first, last - first + 1);
    if (!text.empty() && text.front() == '+') text.remove_prefix(1);
    int result;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size()) return {};
    return result;
}
bool descendant(lxb_dom_node_t* node, lxb_dom_node_t* parent) {
    for (; node; node = node->parent) if (node == parent) return true;
    return false;
}
}
bool Document::Impl::inert(lxb_dom_node_t* node) const {
    for (; node; node = node->parent) if (has(node, "inert")) return true;
    return false;
}
bool Document::Impl::disabled(lxb_dom_node_t* node) const {
    if (node->type != LXB_DOM_NODE_TYPE_ELEMENT || !control(node)) return false;
    if (has(node, "disabled")) return true;
    for (auto* parent = node->parent; parent; parent = parent->parent) {
        if (parent->local_name != LXB_TAG_FIELDSET || !has(parent, "disabled")) continue;
        auto* legend = parent->first_child;
        for (; legend; legend = legend->next) if (legend->local_name == LXB_TAG_LEGEND) break;
        if (!legend || !descendant(node, legend)) return true;
    }
    return false;
}
bool Document::Impl::focusable(lxb_dom_node_t* node, bool sequential) {
    if (!node || node->type != LXB_DOM_NODE_TYPE_ELEMENT || !connected(node) || inert(node) || disabled(node)) return false;
    refresh_styles();
    std::vector<lxb_dom_node_t*> chain;
    for (auto* parent = node; parent && parent->type == LXB_DOM_NODE_TYPE_ELEMENT; parent = parent->parent) chain.push_back(parent);
    BoxStyle inherited;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        inherited = box_style(*this, *it, float(viewport_width), viewport_width, viewport_height, inherited);
        if (inherited.hidden) return false;
    }
    if (node->local_name == LXB_TAG_INPUT && attribute(identify(node), "type", false) == "hidden") return false;
    const auto index = tab_index(*this, node);
    if (index) return !sequential || *index >= 0;
    if (disclosure_owner(node)) return true;
    if (node->local_name == LXB_TAG_BUTTON || node->local_name == LXB_TAG_INPUT || node->local_name == LXB_TAG_SELECT ||
        node->local_name == LXB_TAG_TEXTAREA) return true;
    if ((node->local_name == LXB_TAG_A || node->local_name == LXB_TAG_AREA) && has(node, "href")) return true;
    if (has(node, "contenteditable") && attribute(identify(node), "contenteditable", false) != "false") return true;
    return false;
}
bool Document::Impl::disabled_ancestor(lxb_dom_node_t* node) const {
    // disabled(fieldset) itself must not override its first-legend exception.
    for (; node; node = node->parent) if (node->local_name != LXB_TAG_FIELDSET && disabled(node)) return true;
    return false;
}
bool Document::Impl::set_focus(xl_node_id id) {
    if (id && !focusable(resolve(id))) return false;
    if (focused == id) return false;
    const auto old = focused, epoch = ++focus_epoch;
    focused = 0; ++revision;
    focus_visible = false;
    if (old) {
        edit_blur(old);
        if (epoch != focus_epoch) return true;
        dispatch(old, "blur", nullptr, id, false, false);
        if (epoch != focus_epoch) return true;
        dispatch(old, "focusout", nullptr, id, true, false);
        if (epoch != focus_epoch) return true;
    }
    if (id && focusable(resolve(id))) {
        focused = id; ++revision;
        focus_visible = keyboard_modality || text_field(resolve(id));
        edit_focus(id);
        dispatch(id, "focus", nullptr, old, false, false);
        if (epoch == focus_epoch && focused == id) dispatch(id, "focusin", nullptr, old, true, false);
    }
    return true;
}
bool Document::Impl::checked(xl_node_id id) const {
    auto* node = resolve(id);
    const auto found = checked_values.find(id);
    return found == checked_values.end() ? has(node, "checked") : found->second;
}
void Document::Impl::checked(xl_node_id id, bool value) {
    auto* node = resolve(id);
    if (node->local_name != LXB_TAG_INPUT) return;
    bool changed = checked(id) != value;
    if (value && checkable(node) == 2)
        for (auto* peer : radio_group(node)) {
            const auto peer_id = identify(peer);
            if (peer != node && checked(peer_id)) changed = true;
            checked_values[peer_id] = false;
        }
    checked_values[id] = value;
    if (changed) ++revision;
}
xl_node_id Document::focused() const {
    if (impl_->focused && !impl_->focusable(impl_->resolve(impl_->focused))) impl_->set_focus(0);
    return impl_->focused;
}
uint64_t Document::focus_epoch() const { return impl_->focus_epoch; }
xl_node_id Document::focus_next(bool reverse) {
    struct Candidate { xl_node_id id; int index; size_t order; };
    impl_->refresh_styles();
    std::vector<Candidate> candidates;
    size_t order = 0, focused_order = 0;
    std::vector<lxb_dom_node_t*> pending{lxb_dom_interface_node(impl_->dom)};
    while (!pending.empty()) {
        auto* node = pending.back(); pending.pop_back();
        if (impl_->focused && impl_->identify(node) == impl_->focused) focused_order = order;
        if (impl_->focusable(node, true)) candidates.push_back({impl_->identify(node), tab_index(*impl_, node).value_or(0), order});
        ++order;
        for (auto* child = node->last_child; child; child = child->prev) pending.push_back(child);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        if (a.index == 0 || b.index == 0) return a.index > b.index;
        return a.index < b.index;
    });
    if (candidates.empty()) { impl_->set_focus(0); return 0; }
    const auto current = focused();
    auto found = std::find_if(candidates.begin(), candidates.end(), [&](const auto& item) { return item.id == current; });
    auto index = found == candidates.end() ? (reverse ? candidates.size() - 1 : 0)
        : (size_t(found - candidates.begin()) + (reverse ? candidates.size() - 1 : 1)) % candidates.size();
    if (found == candidates.end() && current) {
        size_t distance = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < candidates.size(); ++i) {
            const auto next = candidates[i].order;
            if ((reverse ? next < focused_order : next > focused_order) &&
                (reverse ? focused_order - next : next - focused_order) < distance) {
                distance = reverse ? focused_order - next : next - focused_order; index = i;
            }
        }
    }
    impl_->set_focus(candidates[index].id);
    return focused();
}
KeyResult Document::key(const xl_key_event& event) {
    auto target = focused();
    const std::string_view key(event.key_utf8);
    if (event.type == XL_KEY_DOWN && key != "Shift" && key != "Control" && key != "Alt" && key != "Meta") {
        impl_->keyboard_modality = true;
        if (target && !impl_->focus_visible) { impl_->focus_visible = true; ++impl_->revision; }
    }
    if (!target) target = impl_->identify(impl_->dom->body ? lxb_dom_interface_node(impl_->dom->body) : lxb_dom_interface_node(impl_->dom));
    return {target, impl_->dispatch(target, event.type == XL_KEY_DOWN ? "keydown" : "keyup", &event)};
}
void Document::pointer_input() {
    impl_->keyboard_modality = false;
    const auto id = focused();
    const bool visible = id && impl_->text_field(impl_->resolve(id));
    if (visible != impl_->focus_visible) { impl_->focus_visible = visible; ++impl_->revision; }
}
xl_node_id Document::pointer_target(const DocumentFrame& frame, float x, float y) const {
    if(!frame.display.empty()) {
        const auto id=frame.hit_paint(x,y,[&](xl_node_id id){auto* node=impl_->resolve(id);return !impl_->inert(node)&&!impl_->details_hidden(node);});
        if(!id)return 0;
        auto* node=impl_->resolve(id);auto* target=impl_->pseudos->origin(node);
        if(!target)target=node->type==LXB_DOM_NODE_TYPE_TEXT?node->parent:node;
        return target&&target->type==LXB_DOM_NODE_TYPE_ELEMENT?impl_->identify(target):0;
    }
    if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) return 0;
    xl_node_id result = 0;
    std::vector<lxb_dom_node_t*> pending{lxb_dom_interface_node(impl_->dom)};
    while (!pending.empty()) {
        auto* node = pending.back(); pending.pop_back();
        if (impl_->inert(node) || impl_->details_hidden(node)) continue;
        const auto found = impl_->identities.find(node);
        if (found != impl_->identities.end()) {
            auto box = frame.visible_bounds(found->second);
            const auto clip=frame.box_clips.find(found->second);
            const bool clipped=clip!=frame.box_clips.end()&&!clip->second.contains(x,y);
            const auto shapes=frame.hit_shapes.find(found->second);
            const bool outside=shapes!=frame.hit_shapes.end()&&!std::any_of(shapes->second.begin(),shapes->second.end(),[&](const auto& shape){return shape.contains(x,y);});
            if (const auto legend = frame.disclosure_legends.find(found->second); legend != frame.disclosure_legends.end()) {
                const auto clip = frame.box_clips.find(found->second);
                box = clip == frame.box_clips.end() ? legend->second : clip->second.bounds(legend->second);
            }
            if (!clipped&&!outside&&box.width > 0 && box.height > 0 && x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height) {
                auto* target = impl_->pseudos->origin(node);
                if (!target) target = node->type == LXB_DOM_NODE_TYPE_TEXT ? node->parent : node;
                if (target && target->type == LXB_DOM_NODE_TYPE_ELEMENT) result = impl_->identify(target);
            }
        }
        const auto children = impl_->render_children(node);
        for (auto it = children.rbegin(); it != children.rend(); ++it) pending.push_back(*it);
    }
    return result;
}
void Document::pointer_focus(xl_node_id id) {
    pointer_input();
    if (id && impl_->disabled_ancestor(impl_->resolve(id))) return;
    if (id) for (auto* node = impl_->resolve(id); node; node = node->parent)
        if (impl_->focusable(node)) { impl_->set_focus(impl_->identify(node)); return; }
    impl_->set_focus(0);
}
void Document::pointer_click(xl_node_id id) {
    if (!id) return;
    auto* node = impl_->resolve(id);
    if (impl_->connected(node) && !impl_->details_hidden(node) && !impl_->disabled_ancestor(node)) impl_->activate(id, true);
}
bool Document::keyboard_activate(xl_node_id id, bool space, bool invoke) {
    auto* node = impl_->resolve(id);
    if (!impl_->connected(node) || impl_->inert(node) || impl_->disabled(node)) return false;
    const auto type = impl_->attribute(id, "type", false);
    const auto input = node->local_name == LXB_TAG_INPUT;
    const auto checkbox = impl_->checkable(node) != 0;
    const auto button = node->local_name == LXB_TAG_BUTTON || (input && (type == "button" || type == "submit" || type == "reset"));
    const auto link = node->local_name == LXB_TAG_A && has(node, "href");
    // ARIA roles do not acquire native HTML activation behavior.
    if (!(button || impl_->disclosure_owner(node) || (space ? checkbox : link))) return false;
    if (invoke) impl_->activate(id, true);
    return true;
}
bool Document::text_control(xl_node_id id) const {
    auto* node = impl_->resolve(id);
    if (node->local_name == LXB_TAG_TEXTAREA) return true;
    if (node->local_name == LXB_TAG_INPUT) {
        const auto type = impl_->attribute(id, "type", false);
        return type.empty() || type == "text" || type == "search" || type == "email" || type == "url" || type == "password" || type == "tel" || type == "number";
    }
    for (; node; node = node->parent)
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            const auto value = impl_->attribute(impl_->identify(node), "contenteditable", false);
            if (value == "false") return false;
            if (value == "true" || has(node, "contenteditable")) return true;
        }
    return false;
}
std::vector<xl_node_id> Document::ancestors(xl_node_id id) const {
    std::vector<xl_node_id> result;
    for (auto* node = impl_->resolve(id); node; node = node->parent) result.push_back(impl_->identify(node));
    return result;
}
} // namespace xl::detail
