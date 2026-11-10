#include "document/dom_internal.h"
#include "style/pseudo_elements.h"
#include <algorithm>
#include <functional>
#include <sstream>
#include <unordered_map>

namespace xl::detail {
namespace {
std::string lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return value;
}
bool has(lxb_dom_node_t* node, const char* name) {
    return lxb_dom_element_has_attribute(element(node), chars(name), std::char_traits<char>::length(name));
}
struct Context {
    xl_node_id region = 0, atomic = 0;
    uint32_t relevant = 3;
    bool busy = false, disabled = false;
};
}
uint32_t accessibility_role(Document::Impl& doc, lxb_dom_node_t* node) {
    const auto id = doc.identify(node);
    uint32_t role = XL_AX_GENERIC;
    switch (node->local_name) {
    case LXB_TAG_BUTTON: role = XL_AX_BUTTON; break;
    case LXB_TAG_A: role = XL_AX_LINK; break;
    case LXB_TAG_INPUT: case LXB_TAG_TEXTAREA: role = XL_AX_TEXT_FIELD; break;
    case LXB_TAG_H1: case LXB_TAG_H2: case LXB_TAG_H3: case LXB_TAG_H4: case LXB_TAG_H5: case LXB_TAG_H6: role = XL_AX_HEADING; break;
    case LXB_TAG_P: role = XL_AX_PARAGRAPH; break;
    case LXB_TAG_IMG: role = XL_AX_IMAGE; break;
    default: break;
    }
    if (doc.disclosure_owner(node)) role = XL_AX_BUTTON;
    if(node->local_name==LXB_TAG_SVG&&node->ns==LXB_NS_SVG)role=XL_AX_IMAGE;
    constexpr std::pair<const char*, uint32_t> roles[]{
        {"button", XL_AX_BUTTON}, {"link", XL_AX_LINK}, {"checkbox", XL_AX_CHECKBOX}, {"radio", XL_AX_RADIO}, {"textbox", XL_AX_TEXT_FIELD},
        {"status", XL_AX_STATUS}, {"alert", XL_AX_ALERT}, {"log", XL_AX_LOG}, {"timer", XL_AX_TIMER}, {"marquee", XL_AX_MARQUEE},{"img",XL_AX_IMAGE}};
    std::istringstream tokens(lower(doc.attribute(id, "role", false))); std::string token;
    while (tokens >> token) {
        const auto found = std::find_if(std::begin(roles), std::end(roles), [&](const auto& entry) { return token == entry.first; });
        if (found != std::end(roles)) { role = found->second; break; }
    }
    if (node->local_name == LXB_TAG_INPUT && lower(doc.attribute(id, "type", false)) == "checkbox") role = XL_AX_CHECKBOX;
    if (node->local_name == LXB_TAG_INPUT && lower(doc.attribute(id, "type", false)) == "radio") role = XL_AX_RADIO;
    return role;
}
bool accessibility_disabled(Document::Impl& doc, lxb_dom_node_t* node) {
    if (doc.disabled(node)) return true;
    for (auto* current = node; current && current->type == LXB_DOM_NODE_TYPE_ELEMENT; current = current->parent)
        if (has(current, "aria-disabled")) return lower(doc.attribute(doc.identify(current), "aria-disabled", false)) == "true";
    return false;
}
void build_accessibility(Document::Impl& doc, DocumentFrame& frame) {
    const auto root = doc.identify(lxb_dom_interface_node(doc.dom));
    frame.accessibility.push_back({{sizeof(xl_ax_node), XL_AX_DOCUMENT, root, 0, {0, 0, float(frame.width), float(frame.height)}, 0, 0}, ""});
    std::unordered_map<xl_node_id, size_t> regions;
    std::unordered_map<std::string, std::string> labels;
    // HTML labels supplement ARIA names, without creating a synthetic DOM node.
    for (auto id : doc.query_all("label")) {
        const auto target = doc.attribute(id, "for", false);
        if (target.empty()) continue;
        auto& value = labels[target];
        const auto text = doc.accessible_text(doc.resolve(id));
        if (!value.empty() && !text.empty()) value.push_back(' ');
        value += text;
    }
    const auto live_part = [&](const Context& context, xl_node_id node, std::string_view text) {
        if (!context.region || text.empty()) return;
        frame.live_regions[regions.at(context.region)].parts.push_back(
            {node, context.atomic, context.relevant, context.busy, std::string(text)});
    };
    std::function<void(lxb_dom_node_t*, xl_node_id, size_t, Context)> visit;
    visit = [&](lxb_dom_node_t* node, xl_node_id parent, size_t depth, Context context) {
        if (depth > 256) throw DocumentFailure{XL_UNSUPPORTED};
        if (node->type != LXB_DOM_NODE_TYPE_ELEMENT && node->type != LXB_DOM_NODE_TYPE_TEXT) return;
        const auto id = doc.identify(node);
        if (node->type == LXB_DOM_NODE_TYPE_TEXT) {
            const auto source = frame.text_nodes.find(id);
            if (source == frame.text_nodes.end()) return;
            auto name = frame.texts[source->second.first].sources[source->second.second].rendered;
            const auto box = frame.boxes.find(id);
            if (box != frame.boxes.end() && !name.empty()) {
                const auto visible = frame.visible_bounds(id);
                live_part(context, id, name);
                frame.accessibility.push_back({{sizeof(xl_ax_node), XL_AX_TEXT, id, parent, visible,
                    nonempty(visible) ? 0u : XL_AX_OFFSCREEN, 0}, std::move(name)});
            }
            return;
        }
        const auto box = frame.boxes.find(id);
        if (box == frame.boxes.end() || doc.inert(node) || lower(doc.attribute(id, "aria-hidden", false)) == "true") return;
        if (doc.pseudos->generated(node)) {
            auto name = doc.pseudos->alternative(node).value_or(doc.text(id));
            const auto visible = frame.visible_bounds(id);
            if (!name.empty()) {
                live_part(context, id, name);
                frame.accessibility.push_back({{sizeof(xl_ax_node), XL_AX_TEXT, id, parent, visible, nonempty(visible) ? 0u : XL_AX_OFFSCREEN, 0}, std::move(name)});
            }
            return;
        }
        const auto role = accessibility_role(doc, node);
        context.busy = context.busy || lower(doc.attribute(id, "aria-busy", false)) == "true";
        if (has(node, "aria-disabled")) context.disabled = lower(doc.attribute(id, "aria-disabled", false)) == "true";
        const auto live = lower(doc.attribute(id, "aria-live", false));
        const auto implicit = role == XL_AX_ALERT ? XL_AX_LIVE_ASSERTIVE
            : role == XL_AX_STATUS || role == XL_AX_LOG ? XL_AX_LIVE_POLITE : 0u;
        if (live == "off" || (!has(node, "aria-live") && (role == XL_AX_TIMER || role == XL_AX_MARQUEE))) {
            context.region = context.atomic = 0;
        } else if (live == "polite" || live == "assertive" || (live.empty() && implicit)) {
            context.region = id; context.atomic = (role == XL_AX_STATUS || role == XL_AX_ALERT) ? id : 0;
            regions.emplace(id, frame.live_regions.size());
            frame.live_regions.push_back({id, live == "assertive" ? XL_AX_LIVE_ASSERTIVE : live == "polite" ? XL_AX_LIVE_POLITE : implicit,
                                          context.busy, role == XL_AX_ALERT, {}});
        }
        if (has(node, "aria-atomic")) {
            const auto atomic = lower(doc.attribute(id, "aria-atomic", false));
            if (atomic == "true") context.atomic = id;
            else if (atomic == "false") context.atomic = 0;
        }
        if (has(node, "aria-relevant")) {
            std::istringstream values(lower(doc.attribute(id, "aria-relevant", false)));
            std::string token;
            uint32_t relevant = 0;
            while (values >> token) {
                if (token == "all") relevant |= 7;
                else if (token == "additions") relevant |= 1;
                else if (token == "text") relevant |= 2;
                else if (token == "removals") relevant |= 4;
            }
            if (relevant) context.relevant = relevant;
        }
        if (context.region && context.busy) frame.live_regions[regions.at(context.region)].busy = true;
        uint32_t states = doc.focused == id ? XL_AX_FOCUSED : 0;
        if (context.region == id) states |= frame.live_regions[regions.at(id)].politeness == XL_AX_LIVE_ASSERTIVE
            ? XL_AX_LIVE_ASSERTIVE_REGION : XL_AX_LIVE_POLITE_REGION;
        auto visible = frame.visible_bounds(id);
        if (const auto legend = frame.disclosure_legends.find(id); legend != frame.disclosure_legends.end()) {
            const auto clip = frame.box_clips.find(id);
            visible = clip == frame.box_clips.end() ? legend->second : clip->second.bounds(legend->second);
        }
        if (!nonempty(visible)) states |= XL_AX_OFFSCREEN;
        const bool disabled = doc.disabled(node) || context.disabled;
        if (disabled) states |= XL_AX_DISABLED;
        const auto checked = lower(doc.attribute(id, "aria-checked", false));
        if (role == XL_AX_CHECKBOX || role == XL_AX_RADIO) states |= XL_AX_CHECKABLE;
        if (checked == "mixed") states |= XL_AX_MIXED;
        else if (checked == "true" || (checked != "false" && (role == XL_AX_CHECKBOX || role == XL_AX_RADIO) && doc.checked(id))) states |= XL_AX_CHECKED;
        if (lower(doc.attribute(id, "aria-selected", false)) == "true") states |= XL_AX_SELECTED;
        auto* disclosure = doc.disclosure_owner(node);
        const auto expanded = lower(doc.attribute(id, "aria-expanded", false));
        if (disclosure) states |= doc.details_open(disclosure) ? XL_AX_EXPANDED : XL_AX_COLLAPSED;
        else if (expanded == "true") states |= XL_AX_EXPANDED;
        else if (expanded == "false") states |= XL_AX_COLLAPSED;
        if (context.busy) states |= XL_AX_BUSY;
        if (has(node, "required") || lower(doc.attribute(id, "aria-required", false)) == "true") states |= XL_AX_REQUIRED;
        const auto invalid = lower(doc.attribute(id, "aria-invalid", false));
        if (!invalid.empty() && invalid != "false") states |= XL_AX_INVALID;
        if (node->local_name == LXB_TAG_TEXTAREA || lower(doc.attribute(id, "aria-multiline", false)) == "true") states |= XL_AX_MULTILINE;
        if (lower(doc.attribute(id, "aria-readonly", false)) == "true") states |= XL_AX_READ_ONLY;
        auto name = doc.attribute(id, "aria-label", false);
        const auto labelledby = doc.attribute(id, "aria-labelledby", false);
        if (!labelledby.empty()) {
            std::istringstream references(labelledby);
            std::string reference, labelled; bool found_reference = false;
            while (references >> reference) {
                const auto target = doc.by_id(reference);
                if (!target) continue;
                found_reference = true; const auto text = doc.text(target);
                if (!labelled.empty() && !text.empty()) labelled.push_back(' ');
                labelled += text;
            }
            if (found_reference) name = std::move(labelled);
        }
        const bool live_name = !name.empty() || role == XL_AX_IMAGE;
        if (name.empty() && role == XL_AX_IMAGE) name = doc.attribute(id, "alt", false);
        if(name.empty()&&role==XL_AX_IMAGE&&node->local_name==LXB_TAG_SVG) {
            for(auto* child=node->first_child;child;child=child->next)if(child->type==LXB_DOM_NODE_TYPE_ELEMENT&&child->local_name==LXB_TAG_TITLE){name=doc.text(doc.identify(child));break;}
        }
        if (name.empty() && (role == XL_AX_TEXT_FIELD || role == XL_AX_CHECKBOX || role == XL_AX_RADIO)) {
            const auto found = labels.find(doc.attribute(id, "id", false));
            if (found != labels.end()) name = found->second;
            if (name.empty())
                for (auto* ancestor = node->parent; ancestor && ancestor->type == LXB_DOM_NODE_TYPE_ELEMENT; ancestor = ancestor->parent)
                    if (ancestor->local_name == LXB_TAG_LABEL) { name = doc.accessible_text(ancestor); break; }
        }
        if (name.empty() && disclosure == node) name = "Details";
        if (name.empty() && role != XL_AX_GENERIC && role != XL_AX_TEXT_FIELD) name = doc.accessible_text(node);
        if (name.empty()) name = doc.attribute(id, "title", false);
        uint32_t actions = doc.focusable(node) ? XL_AX_ACTION_FOCUS : 0;
        if (!disabled && (role == XL_AX_BUTTON || role == XL_AX_LINK || role == XL_AX_CHECKBOX || role == XL_AX_RADIO)) actions |= XL_AX_ACTION_ACTIVATE;
        if (!disabled && disclosure) actions |= XL_AX_ACTION_EXPAND | XL_AX_ACTION_COLLAPSE;
        std::string value;
        if (doc.text_field(node)) {
            const auto edit = doc.edit_state(id); value = doc.editor(id).value;
            if (edit.flags & XL_EDIT_READ_ONLY) states |= XL_AX_READ_ONLY;
            if (edit.flags & XL_EDIT_PASSWORD) {
                states |= XL_AX_PROTECTED; const TextIndex index(value); value.clear();
                for (size_t i = 1; i < index.bytes.size(); ++i) if (index.bytes[i] != TextIndex::invalid) value += "\xe2\x80\xa2";
            }
            live_part(context, id, value);
        } else if (live_name) live_part(context, id, name);
        frame.accessibility.push_back({{sizeof(xl_ax_node), role, id, parent, visible, states, actions}, std::move(name), std::move(value)});
        if (doc.text_field(node)||doc.image_node(node)) return;
        if (live_name) context.region = 0; // Avoid repeating the named element's descendant text in announcements.
        for (auto* child : doc.render_children(node)) visit(child, id, depth + 1, context);
    };
    for (auto* child = doc.dom->dom_document.node.first_child; child; child = child->next) visit(child, root, 0, {});
}
}
