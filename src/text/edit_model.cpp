#include "edit_model.h"
#include "document/document.h"
#include <algorithm>
#include <unicode/ubrk.h>
#include <unicode/utf8.h>

namespace xl::detail {
TextIndex::TextIndex(std::string_view text) {
    if (text.size() > 4 * 1024 * 1024) throw DocumentFailure{XL_INVALID_ARGUMENT};
    bytes.push_back(0);
    for (int32_t i = 0; i < int32_t(text.size());) {
        UChar32 scalar; U8_NEXT(text.data(), i, int32_t(text.size()), scalar);
        if (scalar < 0 || scalar == 0) throw DocumentFailure{XL_INVALID_ARGUMENT};
        if (scalar > 0xffff) {
            scalar -= 0x10000; units += char16_t(0xd800 + (scalar >> 10)); bytes.push_back(invalid);
            units += char16_t(0xdc00 + (scalar & 1023));
        } else units += char16_t(scalar);
        bytes.push_back(uint32_t(i));
    }
}
size_t TextIndex::byte(uint32_t offset) const {
    if (offset >= bytes.size() || bytes[offset] == invalid) throw DocumentFailure{XL_INVALID_ARGUMENT};
    return bytes[offset];
}
std::string normalize_edit_text(std::string_view text, bool multiline) {
    (void)TextIndex(text);
    std::string value; value.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            if (multiline) value += '\n';
        } else if (text[i] != '\n' || multiline) value += text[i];
    }
    return value;
}
uint32_t edit_boundary(const TextIndex& text, uint32_t offset, bool forward, bool word) {
    (void)text.byte(offset);
    UErrorCode status = U_ZERO_ERROR;
    auto* iterator = ubrk_open(word ? UBRK_WORD : UBRK_CHARACTER, "und",
        reinterpret_cast<const UChar*>(text.units.data()), int32_t(text.units.size()), &status);
    if (U_FAILURE(status) || !iterator) throw DocumentFailure{XL_INTERNAL_ERROR};
    struct Close { UBreakIterator* it; ~Close() { ubrk_close(it); } } close{iterator};
    auto next = forward ? ubrk_following(iterator, int32_t(offset)) : ubrk_preceding(iterator, int32_t(offset));
    if (word) {
        while (next != UBRK_DONE && next > 0 && next < int32_t(text.units.size())) {
            const auto character = text.units[size_t(forward ? next - 1 : next)];
            if (character != ' ' && character != '\n' && character != '\t') break;
            next = forward ? ubrk_next(iterator) : ubrk_previous(iterator);
        }
    }
    return next == UBRK_DONE ? (forward ? text.length() : 0u) : uint32_t(next);
}
void Editor::restore(EditSnapshot state) {
    value = std::move(state.value); anchor = state.anchor; focus = state.focus; dirty = state.dirty;
    composition.reset(); goal_x.reset(); ++version;
}
void Editor::remember(EditSnapshot before) {
    redo.clear(); undo.push_back(std::move(before));
    size_t bytes = 0;
    for (const auto& state : undo) bytes += state.value.size();
    while (undo.size() > 64 || (bytes > 4 * 1024 * 1024 && undo.size() > 1)) {
        bytes -= undo.front().value.size(); undo.pop_front();
    }
}
bool Editor::history(bool forward) {
    auto& from = forward ? redo : undo;
    auto& to = forward ? undo : redo;
    if (from.empty()) return false;
    to.push_back(snapshot()); auto state = std::move(from.back()); from.pop_back(); restore(std::move(state));
    user_changed = true; return true;
}
} // namespace xl::detail
