#include "dom_internal.h"
#include <algorithm>

namespace xl::detail {
bool Document::clipboard(uint32_t command, const std::function<std::string()>& read,
                         const std::function<void(std::string_view)>& write,
                         std::string_view selection, xl_node_id selection_node) {
    auto& doc = *impl_;
    const auto focused = this->focused();
    const bool field = focused && doc.text_field(doc.resolve(focused));
    std::optional<xl_edit_state> state;
    std::string selected;
    if (field) {
        state = doc.edit_state(focused);
        // Let the host IME finish its transaction before editing the clipboard.
        if (state->flags & XL_EDIT_COMPOSING) return false;
        if (!(state->flags & XL_EDIT_PASSWORD)) {
            const auto& value = doc.editor(focused).value;
            const TextIndex index(value);
            const auto start = index.byte(std::min(state->anchor_utf16, state->focus_utf16));
            const auto end = index.byte(std::max(state->anchor_utf16, state->focus_utf16));
            selected = value.substr(start, end - start);
        }
    } else selected = selection;
    const auto epoch = doc.focus_epoch, revision = doc.revision;
    auto target = field ? focused : (command != XL_CLIPBOARD_PASTE && selection_node ? selection_node : focused);
    if (!target) target = doc.identify(lxb_dom_interface_node(doc.dom->body));
    if (!target) target = doc.identify(lxb_dom_interface_node(doc.dom));
    auto* node = doc.resolve(target);
    if (node->type == LXB_DOM_NODE_TYPE_TEXT && node->parent) target = doc.identify(node->parent);
    ClipboardEvent event{command == XL_CLIPBOARD_PASTE, {}};
    if (event.paste) { event.text = read(); (void)TextIndex(*event.text); }
    const auto unchanged = [&] {
        if (doc.focus_epoch != epoch) return false;
        if (state) return doc.focused == focused && doc.connected(doc.resolve(focused)) &&
            doc.editor(focused).version == state->revision;
        return doc.revision == revision;
    };
    const char* type = event.paste ? "paste" : command == XL_CLIPBOARD_CUT ? "cut" : "copy";
    const bool allowed = doc.dispatch(target, type, nullptr, 0, true, true, true, nullptr, &event);
    if (!allowed) {
        if (!event.paste && event.text) { (void)TextIndex(*event.text); write(*event.text); }
        return true;
    }
    if (!unchanged()) return true;
    if (event.paste) {
        if (!field || (state->flags & XL_EDIT_READ_ONLY)) return false;
        return doc.replace_edit(focused, *event.text, "insertFromPaste");
    }
    if (state && (state->flags & XL_EDIT_PASSWORD)) return true;
    if (selected.empty()) return false;
    if (command == XL_CLIPBOARD_CUT && (!field || (state->flags & XL_EDIT_READ_ONLY))) return false;
    (void)TextIndex(selected);
    write(selected);
    if (command == XL_CLIPBOARD_CUT && unchanged()) doc.replace_edit(focused, {}, "deleteByCut");
    return true;
}
} // namespace xl::detail
