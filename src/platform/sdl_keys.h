#pragma once
#include "key_input.h"
#include <SDL3/SDL.h>

namespace xl::detail {
inline std::string dom_code(SDL_Scancode scan) {
    if (scan >= SDL_SCANCODE_A && scan <= SDL_SCANCODE_Z) return std::string("Key") + char('A' + scan - SDL_SCANCODE_A);
    if (scan >= SDL_SCANCODE_1 && scan <= SDL_SCANCODE_9) return std::string("Digit") + char('1' + scan - SDL_SCANCODE_1);
    if (scan >= SDL_SCANCODE_F1 && scan <= SDL_SCANCODE_F12) return "F" + std::to_string(1 + scan - SDL_SCANCODE_F1);
    if (scan >= SDL_SCANCODE_F13 && scan <= SDL_SCANCODE_F24) return "F" + std::to_string(13 + scan - SDL_SCANCODE_F13);
    if (scan >= SDL_SCANCODE_KP_1 && scan <= SDL_SCANCODE_KP_9) return "Numpad" + std::to_string(1 + scan - SDL_SCANCODE_KP_1);
    switch (scan) {
    case SDL_SCANCODE_0: return "Digit0";
    case SDL_SCANCODE_KP_0: return "Numpad0";
    case SDL_SCANCODE_RETURN: return "Enter";
    case SDL_SCANCODE_KP_ENTER: return "NumpadEnter";
    case SDL_SCANCODE_ESCAPE: return "Escape";
    case SDL_SCANCODE_BACKSPACE: return "Backspace";
    case SDL_SCANCODE_TAB: return "Tab";
    case SDL_SCANCODE_SPACE: return "Space";
    case SDL_SCANCODE_INSERT: return "Insert";
    case SDL_SCANCODE_DELETE: return "Delete";
    case SDL_SCANCODE_HOME: return "Home";
    case SDL_SCANCODE_END: return "End";
    case SDL_SCANCODE_PAGEUP: return "PageUp";
    case SDL_SCANCODE_PAGEDOWN: return "PageDown";
    case SDL_SCANCODE_LEFT: return "ArrowLeft";
    case SDL_SCANCODE_RIGHT: return "ArrowRight";
    case SDL_SCANCODE_UP: return "ArrowUp";
    case SDL_SCANCODE_DOWN: return "ArrowDown";
    case SDL_SCANCODE_LSHIFT: return "ShiftLeft";
    case SDL_SCANCODE_RSHIFT: return "ShiftRight";
    case SDL_SCANCODE_LCTRL: return "ControlLeft";
    case SDL_SCANCODE_RCTRL: return "ControlRight";
    case SDL_SCANCODE_LALT: return "AltLeft";
    case SDL_SCANCODE_RALT: return "AltRight";
    case SDL_SCANCODE_LGUI: return "MetaLeft";
    case SDL_SCANCODE_RGUI: return "MetaRight";
    case SDL_SCANCODE_CAPSLOCK: return "CapsLock";
    case SDL_SCANCODE_NUMLOCKCLEAR: return "NumLock";
    case SDL_SCANCODE_SCROLLLOCK: return "ScrollLock";
    case SDL_SCANCODE_PRINTSCREEN: return "PrintScreen";
    case SDL_SCANCODE_PAUSE: return "Pause";
    case SDL_SCANCODE_APPLICATION: return "ContextMenu";
    case SDL_SCANCODE_MINUS: return "Minus";
    case SDL_SCANCODE_EQUALS: return "Equal";
    case SDL_SCANCODE_LEFTBRACKET: return "BracketLeft";
    case SDL_SCANCODE_RIGHTBRACKET: return "BracketRight";
    case SDL_SCANCODE_BACKSLASH: return "Backslash";
    case SDL_SCANCODE_SEMICOLON: return "Semicolon";
    case SDL_SCANCODE_APOSTROPHE: return "Quote";
    case SDL_SCANCODE_GRAVE: return "Backquote";
    case SDL_SCANCODE_COMMA: return "Comma";
    case SDL_SCANCODE_PERIOD: return "Period";
    case SDL_SCANCODE_SLASH: return "Slash";
    case SDL_SCANCODE_KP_PLUS: return "NumpadAdd";
    case SDL_SCANCODE_KP_MINUS: return "NumpadSubtract";
    case SDL_SCANCODE_KP_MULTIPLY: return "NumpadMultiply";
    case SDL_SCANCODE_KP_DIVIDE: return "NumpadDivide";
    case SDL_SCANCODE_KP_PERIOD: return "NumpadDecimal";
    default: return "";
    }
}
inline std::string dom_key(SDL_Keycode key) {
    if (key >= SDLK_F1 && key <= SDLK_F12) return "F" + std::to_string(1 + key - SDLK_F1);
    if (key >= SDLK_F13 && key <= SDLK_F24) return "F" + std::to_string(13 + key - SDLK_F13);
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return std::to_string(1 + key - SDLK_KP_1);
    switch (key) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return "Enter";
    case SDLK_TAB: return "Tab";
    case SDLK_ESCAPE: return "Escape";
    case SDLK_BACKSPACE: return "Backspace";
    case SDLK_DELETE: return "Delete";
    case SDLK_INSERT: return "Insert";
    case SDLK_HOME: return "Home";
    case SDLK_END: return "End";
    case SDLK_PAGEUP: return "PageUp";
    case SDLK_PAGEDOWN: return "PageDown";
    case SDLK_LEFT: return "ArrowLeft";
    case SDLK_RIGHT: return "ArrowRight";
    case SDLK_UP: return "ArrowUp";
    case SDLK_DOWN: return "ArrowDown";
    case SDLK_LSHIFT: case SDLK_RSHIFT: return "Shift";
    case SDLK_LCTRL: case SDLK_RCTRL: return "Control";
    case SDLK_LALT: case SDLK_RALT: return "Alt";
    case SDLK_LGUI: case SDLK_RGUI: return "Meta";
    case SDLK_CAPSLOCK: return "CapsLock";
    case SDLK_NUMLOCKCLEAR: return "NumLock";
    case SDLK_SCROLLLOCK: return "ScrollLock";
    case SDLK_PRINTSCREEN: return "PrintScreen";
    case SDLK_PAUSE: return "Pause";
    case SDLK_APPLICATION: return "ContextMenu";
    case SDLK_KP_0: return "0";
    case SDLK_KP_PLUS: return "+";
    case SDLK_KP_MINUS: return "-";
    case SDLK_KP_MULTIPLY: return "*";
    case SDLK_KP_DIVIDE: return "/";
    case SDLK_KP_PERIOD: return ".";
    default: break;
    }
    if (key >= 0x20 && key <= 0x10ffff && !(key >= 0xd800 && key <= 0xdfff)) {
        std::string result;
        if (key < 0x80) result += char(key);
        else if (key < 0x800) { result += char(0xc0 | (key >> 6)); result += char(0x80 | (key & 63)); }
        else if (key < 0x10000) { result += char(0xe0 | (key >> 12)); result += char(0x80 | ((key >> 6) & 63)); result += char(0x80 | (key & 63)); }
        else { result += char(0xf0 | (key >> 18)); result += char(0x80 | ((key >> 12) & 63)); result += char(0x80 | ((key >> 6) & 63)); result += char(0x80 | (key & 63)); }
        return result;
    }
    return "Unidentified";
}
inline KeyInput keyboard_event(const SDL_KeyboardEvent& event) {
    KeyInput input;
    input.type = event.down ? XL_KEY_DOWN : XL_KEY_UP; input.repeat = event.repeat ? 1u : 0u;
    auto key = SDL_GetKeyFromScancode(event.scancode, event.mod, true);
    if (key == SDLK_UNKNOWN) key = event.key;
    input.key = dom_key(key); input.code = dom_code(event.scancode);
    if (event.mod & SDL_KMOD_SHIFT) input.modifiers |= XL_MOD_SHIFT;
    if (event.mod & SDL_KMOD_CTRL) input.modifiers |= XL_MOD_CONTROL;
    if (event.mod & SDL_KMOD_ALT) input.modifiers |= XL_MOD_ALT;
    if (event.mod & SDL_KMOD_GUI) input.modifiers |= XL_MOD_META;
    if (event.mod & SDL_KMOD_CAPS) input.modifiers |= XL_MOD_CAPS_LOCK;
    if (event.mod & SDL_KMOD_NUM) input.modifiers |= XL_MOD_NUM_LOCK;
    if (event.scancode == SDL_SCANCODE_LSHIFT || event.scancode == SDL_SCANCODE_LCTRL || event.scancode == SDL_SCANCODE_LALT || event.scancode == SDL_SCANCODE_LGUI) input.location = 1;
    else if (event.scancode == SDL_SCANCODE_RSHIFT || event.scancode == SDL_SCANCODE_RCTRL || event.scancode == SDL_SCANCODE_RALT || event.scancode == SDL_SCANCODE_RGUI) input.location = 2;
    else if (input.code.starts_with("Numpad")) input.location = 3;
    return input;
}
} // namespace xl::detail
