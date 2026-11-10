#pragma once
#include <XLWebEngineUI/xl.h>
#include <string>

namespace xl::detail {
struct KeyInput {
    std::string key, code;
    uint32_t type = 0, modifiers = 0, repeat = 0, location = 0;
    xl_key_event event() const { return {sizeof(xl_key_event), type, modifiers, repeat, location, 0, key.c_str(), code.c_str()}; }
};
struct WindowInput {
    enum class Kind { reset, key, text, composition, pointer } kind = Kind::reset;
    KeyInput key;
    std::string text;
    int32_t start = 0, length = 0; // SDL scalar counts, converted at the core boundary.
    float x = 0, y = 0;
    bool extend = false;
    uint32_t pointer_type = XL_POINTER_MOVE, pointer_modifiers = 0;
};
} // namespace xl::detail
