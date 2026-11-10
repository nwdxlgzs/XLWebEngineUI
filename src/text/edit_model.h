#pragma once
#include <XLWebEngineUI/xl.h>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace xl::detail {
struct TextIndex {
    static constexpr uint32_t invalid = 0xffffffffu;
    std::u16string units;
    std::vector<uint32_t> bytes;
    explicit TextIndex(std::string_view text);
    uint32_t length() const { return uint32_t(units.size()); }
    size_t byte(uint32_t offset) const;
};
std::string normalize_edit_text(std::string_view text, bool multiline);
uint32_t edit_boundary(const TextIndex& text, uint32_t offset, bool forward, bool word);
struct EditSnapshot { std::string value; uint32_t anchor = 0, focus = 0; bool dirty = false; };
struct Composition { EditSnapshot original; uint32_t start = 0, end = 0; };
struct Editor {
    std::string value;
    uint32_t anchor = 0, focus = 0;
    uint64_t version = 1;
    bool dirty = false, user_changed = false;
    std::string focus_value;
    std::optional<Composition> composition;
    std::optional<float> goal_x;
    std::deque<EditSnapshot> undo, redo;
    EditSnapshot snapshot() const { return {value, anchor, focus, dirty}; }
    void restore(EditSnapshot state);
    void remember(EditSnapshot before);
    bool history(bool forward);
};
struct TextEvent {
    std::optional<std::string_view> data;
    std::string_view input_type;
    bool composing = false;
};
struct ClipboardEvent {
    bool paste = false;
    std::optional<std::string> text;
};
} // namespace xl::detail
