#include "dom_internal.h"
#include <algorithm>
#include <charconv>
#include <limits>

namespace xl::detail {
namespace {
std::string input_type(Document::Impl& doc, xl_node_id node) {
    auto type = doc.attribute(node, "type", false);
    for (auto& c : type) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return type;
}
bool has(lxb_dom_node_t* node, const char* name) {
    return lxb_dom_element_has_attribute(element(node), chars(name), std::char_traits<char>::length(name));
}
std::string_view slice(std::string_view value, const TextIndex& index, uint32_t start, uint32_t end) {
    const auto first = index.byte(start); return value.substr(first, index.byte(end) - first);
}
uint32_t paragraph_offset(const TextSource& source, uint32_t position) {
    auto it = std::lower_bound(source.offsets.begin(), source.offsets.end(), position);
    auto result = uint32_t(std::min<size_t>(it - source.offsets.begin(), source.offsets.size() - 1));
    while (result && source.byte_offsets[result] == TextIndex::invalid) --result;
    return result;
}
}
bool Document::Impl::text_field(lxb_dom_node_t* node) const {
    if (node->local_name == LXB_TAG_TEXTAREA) return true;
    if (node->local_name != LXB_TAG_INPUT) return false;
    const auto type = input_type(*const_cast<Impl*>(this), const_cast<Impl*>(this)->identify(node));
    for (const auto* excluded : {"hidden", "checkbox", "radio", "button", "submit", "reset", "file", "range", "color",
        "date", "datetime-local", "month", "week", "time", "number", "image"}) if (type == excluded) return false;
    return true;
}
Editor& Document::Impl::editor(xl_node_id id) {
    auto* node = resolve(id);
    if (!text_field(node)) throw DocumentFailure{XL_UNSUPPORTED};
    auto& edit = editors[id];
    const bool multiline = node->local_name == LXB_TAG_TEXTAREA;
    const auto authored = normalize_edit_text(multiline ? text(id) : attribute(id, "value", false), multiline);
    auto value = normalize_edit_text(edit.dirty ? std::string_view(edit.value) : std::string_view(authored), multiline);
    if (input_type(*this, id) == "url" || input_type(*this, id) == "email") {
        const auto begin = value.find_first_not_of(" \t\r\n\f");
        value = begin == std::string::npos ? "" : value.substr(begin, value.find_last_not_of(" \t\r\n\f") - begin + 1);
    }
    if (value != edit.value) {
        edit.value = std::move(value); edit.composition.reset(); ++edit.version;
        const TextIndex index(edit.value);
        edit.anchor = std::min(edit.anchor, index.length()); edit.focus = std::min(edit.focus, index.length());
        while (edit.anchor && index.bytes[edit.anchor] == TextIndex::invalid) --edit.anchor;
        while (edit.focus && index.bytes[edit.focus] == TextIndex::invalid) --edit.focus;
    }
    return edit;
}
bool Document::Impl::read_only(xl_node_id id) const {
    auto* node = resolve(id);
    return disabled(node) || inert(node) || has(node, "readonly");
}
xl_edit_state Document::Impl::edit_state(xl_node_id id) {
    auto& edit = editor(id);
    uint32_t flags = resolve(id)->local_name == LXB_TAG_TEXTAREA ? XL_EDIT_MULTILINE : 0;
    if (read_only(id)) flags |= XL_EDIT_READ_ONLY;
    if (input_type(*this, id) == "password") flags |= XL_EDIT_PASSWORD;
    if (edit.composition) flags |= XL_EDIT_COMPOSING;
    return {sizeof(xl_edit_state), flags, id, edit.anchor, edit.focus,
        edit.composition ? edit.composition->start : 0, edit.composition ? edit.composition->end : 0, edit.version};
}
void Document::Impl::edit_value(xl_node_id id, std::string_view text) {
    auto& edit = editor(id);
    auto value = normalize_edit_text(text, resolve(id)->local_name == LXB_TAG_TEXTAREA);
    const auto length = TextIndex(value).length();
    if (value != edit.value) edit.anchor = edit.focus = length;
    edit.value = std::move(value); edit.dirty = true; edit.composition.reset();
    edit.undo.clear(); edit.redo.clear(); edit.goal_x.reset(); ++edit.version; ++revision;
}
void Document::Impl::edit_selection(xl_node_id id, uint32_t anchor, uint32_t focus) {
    auto& edit = editor(id);
    const TextIndex index(edit.value);
    anchor = std::min(anchor, index.length()); focus = std::min(focus, index.length());
    (void)index.byte(anchor); (void)index.byte(focus);
    if (edit.anchor == anchor && edit.focus == focus && !edit.composition) return;
    std::optional<std::string> composition_text;
    if (edit.composition) {
        composition_text = std::string(slice(edit.value, index, edit.composition->start, edit.composition->end));
        edit.remember(edit.composition->original); edit.composition.reset();
    }
    edit.anchor = anchor; edit.focus = focus; edit.composition.reset(); edit.goal_x.reset(); ++edit.version; ++revision;
    if (composition_text) {
        const TextEvent event{*composition_text, {}, false}; dispatch(id, "compositionend", nullptr, 0, true, false, true, &event);
    }
}
bool Document::Impl::replace_edit(xl_node_id id, std::string_view text, std::string_view type, bool composing, bool history,
                                 std::optional<std::pair<uint32_t, uint32_t>> range) {
    auto& edit = editor(id);
    if (read_only(id) || !connected(resolve(id)) || focused != id) return false;
    auto insert = normalize_edit_text(text, resolve(id)->local_name == LXB_TAG_TEXTAREA);
    TextEvent event{type.starts_with("delete") ? std::nullopt : std::optional<std::string_view>(insert), type, composing};
    const auto version = edit.version, epoch = focus_epoch;
    if (!dispatch(id, "beforeinput", nullptr, 0, true, !composing, true, &event)) return true;
    if (edit.version != version || focus_epoch != epoch || focused != id || read_only(id) || !connected(resolve(id))) return true;
    const TextIndex index(edit.value);
    const auto start = range ? range->first : std::min(edit.anchor, edit.focus);
    const auto end = range ? range->second : std::max(edit.anchor, edit.focus);
    if (!composing) {
        const auto limit_text = attribute(id, "maxlength", false);
        uint32_t limit;
        const auto parsed = std::from_chars(limit_text.data(), limit_text.data() + limit_text.size(), limit);
        if (parsed.ec == std::errc{} && parsed.ptr == limit_text.data() + limit_text.size()) {
            const auto available = limit > index.length() - (end - start) ? limit - (index.length() - (end - start)) : 0;
            const TextIndex added(insert);
            auto count = std::min(available, added.length());
            while (count && added.bytes[count] == TextIndex::invalid) --count;
            insert.resize(added.byte(count));
        }
    }
    const auto before = edit.snapshot();
    if (event.data) event.data = insert;
    std::string value(slice(edit.value, index, 0, start)); value += insert; value += slice(edit.value, index, end, index.length());
    if (value.size() > 4 * 1024 * 1024) throw DocumentFailure{XL_OUT_OF_MEMORY};
    if (value == edit.value && start == end) return true;
    const auto caret = start + TextIndex(insert).length();
    if (history) edit.remember(before);
    edit.value = std::move(value); edit.anchor = edit.focus = caret; edit.dirty = edit.user_changed = true;
    edit.goal_x.reset(); ++edit.version; ++revision;
    dispatch(id, "input", nullptr, 0, true, false, true, &event);
    return true;
}
void Document::Impl::edit_focus(xl_node_id id) {
    if (text_field(resolve(id))) { auto& edit = editor(id); edit.focus_value = edit.value; edit.user_changed = false; }
}
void Document::Impl::edit_blur(xl_node_id id) {
    const auto found = editors.find(id);
    if (found == editors.end()) return;
    auto& edit = found->second;
    if (edit.composition) {
        auto original = std::move(edit.composition->original); edit.restore(std::move(original)); ++revision;
        const TextEvent event{std::string_view{}, {}, false}; dispatch(id, "compositionend", nullptr, 0, true, false, true, &event);
    }
    const bool changed = edit.user_changed && edit.value != edit.focus_value;
    edit.user_changed = false;
    if (changed) dispatch(id, "change", nullptr, 0, true, false);
}
std::string Document::value(xl_node_id id) const { return impl_->editor(id).value; }
bool Document::editable_field(xl_node_id id) const { return id && impl_->text_field(impl_->resolve(id)); }
void Document::value(xl_node_id id, std::string_view value) { impl_->edit_value(id, value); }
void Document::accessibility_value(xl_node_id id, std::string_view value) {
    (void)TextIndex(value);
    auto* node = impl_->resolve(id);
    if (!impl_->connected(node) || !impl_->text_field(node) || impl_->read_only(id) ||
        accessibility_disabled(*impl_, node) || impl_->attribute(id, "aria-readonly", false) == "true")
        throw DocumentFailure{XL_UNSUPPORTED};
    if (impl_->editor(id).composition) throw DocumentFailure{XL_BUSY};
    impl_->set_focus(id);
    if (accessibility_disabled(*impl_, node) || impl_->attribute(id, "aria-readonly", false) == "true") throw DocumentFailure{XL_UNSUPPORTED};
    if (impl_->editor(id).composition || !impl_->replace_edit(id, value, "insertReplacementText", false, true,
            std::pair{0u, TextIndex(impl_->editor(id).value).length()})) throw DocumentFailure{XL_UNSUPPORTED};
}
xl_edit_state Document::edit_state(xl_node_id id) const { return impl_->edit_state(id); }
void Document::edit_selection(xl_node_id id, uint32_t anchor, uint32_t focus) { impl_->edit_selection(id, anchor, focus); }
bool Document::text_input(std::string_view text) {
    (void)TextIndex(text);
    const auto id = focused();
    if (!id || !impl_->text_field(impl_->resolve(id))) return false;
    if (impl_->editor(id).composition) {
        const xl_composition commit{sizeof(xl_composition), XL_COMPOSITION_COMMIT, 0, 0, text.data(), text.size()};
        return composition(commit);
    }
    return impl_->replace_edit(id, text, "insertText");
}
bool Document::composition(const xl_composition& input) {
    const std::string_view text(input.text_utf8 ? input.text_utf8 : "", input.text_size);
    const TextIndex incoming(text);
    const auto id = focused();
    if (!id || !impl_->text_field(impl_->resolve(id))) return false;
    auto& edit = impl_->editor(id);
    if (input.type == XL_COMPOSITION_CANCEL) {
        if (!edit.composition) return false;
        auto original = std::move(edit.composition->original); edit.restore(std::move(original)); ++impl_->revision;
        const TextEvent event{std::string_view{}, {}, false}; impl_->dispatch(id, "compositionend", nullptr, 0, true, false, true, &event); return true;
    }
    if (impl_->read_only(id)) return false;
    if (input.type == XL_COMPOSITION_COMMIT) {
        if (!edit.composition) return impl_->replace_edit(id, text, "insertText");
        auto original = std::move(edit.composition->original); edit.restore(std::move(original)); ++impl_->revision;
        const auto changed = impl_->replace_edit(id, text, "insertFromComposition");
        const TextEvent event{text, {}, false}; impl_->dispatch(id, "compositionend", nullptr, 0, true, false, true, &event); return changed;
    }
    (void)incoming.byte(input.selection_start_utf16); (void)incoming.byte(input.selection_end_utf16);
    if (input.selection_start_utf16 > input.selection_end_utf16) throw DocumentFailure{XL_INVALID_ARGUMENT};
    if (!edit.composition) {
        edit.composition = Composition{edit.snapshot(), std::min(edit.anchor, edit.focus), std::max(edit.anchor, edit.focus)};
        ++edit.version; ++impl_->revision;
        const auto version = edit.version;
        const TextEvent event{std::string_view{}, {}, true};
        if (!impl_->dispatch(id, "compositionstart", nullptr, 0, true, true, true, &event)) {
            if (edit.composition && edit.version == version) { auto original = std::move(edit.composition->original); edit.restore(std::move(original)); ++impl_->revision; }
            return true;
        }
        if (!edit.composition || edit.version != version || focused() != id) return true;
    }
    const TextEvent event{text, {}, true};
    const auto version = edit.version;
    impl_->dispatch(id, "compositionupdate", nullptr, 0, true, false, true, &event);
    if (!edit.composition || edit.version != version || focused() != id) return true;
    const auto start = edit.composition->start;
    const auto update_version = edit.version, epoch = impl_->focus_epoch;
    impl_->replace_edit(id, text, "insertCompositionText", true, false, std::pair{start, edit.composition->end});
    if (edit.composition && impl_->focus_epoch == epoch && (edit.version == update_version || edit.version == update_version + 1)) {
        edit.composition->end = edit.focus;
        const auto length = edit.composition->end - start;
        const bool multiline = impl_->resolve(id)->local_name == LXB_TAG_TEXTAREA;
        const auto normalize_offset = [&](uint32_t offset) {
            return std::min(TextIndex(normalize_edit_text(text.substr(0, incoming.byte(offset)), multiline)).length(), length);
        };
        edit.anchor = start + normalize_offset(input.selection_start_utf16);
        edit.focus = start + normalize_offset(input.selection_end_utf16);
        ++edit.version; ++impl_->revision;
    }
    return true;
}
bool Document::edit_key(const xl_key_event& key, const DocumentFrame* frame) {
    if (key.type != XL_KEY_DOWN) return false;
    if (key.modifiers & XL_MOD_ALT) return false;
    const auto id = focused();
    if (!id || !impl_->text_field(impl_->resolve(id))) return false;
    auto& edit = impl_->editor(id);
    const std::string_view name(key.key_utf8);
    const bool shift = key.modifiers & XL_MOD_SHIFT, control = key.modifiers & (XL_MOD_CONTROL | XL_MOD_META);
    if (edit.composition) {
        if (name == "Escape") { xl_composition cancel{sizeof(cancel), XL_COMPOSITION_CANCEL, 0, 0, nullptr, 0}; return composition(cancel); }
        return name != "Tab";
    }
    const TextIndex index(edit.value);
    if (control && (name == "a" || name == "A")) { impl_->edit_selection(id, 0, index.length()); return true; }
    if (control && (name == "z" || name == "Z" || name == "y" || name == "Y")) {
        if (impl_->read_only(id)) return false;
        const bool forward = name == "y" || name == "Y" || shift;
        const TextEvent event{std::nullopt, forward ? "historyRedo" : "historyUndo", false};
        const auto version = edit.version, epoch = impl_->focus_epoch;
        if (!impl_->dispatch(id, "beforeinput", nullptr, 0, true, true, true, &event)) return true;
        if (edit.version != version || impl_->focus_epoch != epoch || impl_->read_only(id) || !impl_->connected(impl_->resolve(id))) return true;
        if (edit.history(forward)) { ++impl_->revision; impl_->dispatch(id, "input", nullptr, 0, true, false, true, &event); }
        return true;
    }
    if (name == "Backspace" || name == "Delete") {
        if (impl_->read_only(id)) return false;
        const auto next = edit.anchor == edit.focus ? edit_boundary(index, edit.focus, name == "Delete", control) : edit.focus;
        return impl_->replace_edit(id, {}, name == "Backspace" ? (control ? "deleteWordBackward" : "deleteContentBackward")
            : (control ? "deleteWordForward" : "deleteContentForward"), false, true,
            std::pair{std::min(edit.anchor, next), std::max(edit.anchor, next)});
    }
    if (name == "Enter") return impl_->resolve(id)->local_name == LXB_TAG_TEXTAREA ? impl_->replace_edit(id, "\n", "insertLineBreak") : true;
    uint32_t next = edit.focus;
    const auto left = name == "ArrowLeft", right = name == "ArrowRight";
    if (left || right) {
        next = !shift && edit.anchor != edit.focus ? (right ? std::max(edit.anchor, edit.focus) : std::min(edit.anchor, edit.focus))
            : edit_boundary(index, edit.focus, right, control);
    } else if (name == "Home" || name == "End" || name == "ArrowUp" || name == "ArrowDown") {
        if (control && (name == "Home" || name == "End")) next = name == "Home" ? 0 : index.length();
        else if (frame && frame->text_nodes.contains(id)) {
            const auto [text_index, source_index] = frame->text_nodes.at(id);
            const auto& text = frame->texts[text_index]; const auto& source = text.sources[source_index];
            const auto current = text.paragraph->caret(source.offsets[edit.focus], true);
            const auto goal = edit.goal_x.value_or(current.x);
            const auto x = name == "Home" ? -1e6f : name == "End" ? 1e6f : goal;
            const auto y = current.y + current.height * .5f + (name == "ArrowUp" ? -current.height : name == "ArrowDown" ? current.height : 0);
            next = paragraph_offset(source, text.paragraph->hit(x, y).first);
            if (name == "ArrowUp" || name == "ArrowDown") edit.goal_x = goal;
        } else {
            const auto start = edit.focus ? index.units.rfind(u'\n', edit.focus - 1) : std::u16string::npos;
            const auto end = index.units.find(u'\n', edit.focus);
            next = name == "Home" || name == "ArrowUp" ? (start == std::u16string::npos ? 0 : uint32_t(start + 1))
                : (end == std::u16string::npos ? index.length() : uint32_t(end));
        }
    } else return false;
    const auto goal = (name == "ArrowUp" || name == "ArrowDown") ? edit.goal_x : std::optional<float>{};
    impl_->edit_selection(id, shift ? edit.anchor : next, next); edit.goal_x = goal; return true;
}
} // namespace xl::detail
