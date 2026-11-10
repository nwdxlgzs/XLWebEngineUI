#pragma once
#include <XLWebEngineUI/xl.h>
#include <memory>
#include <stdexcept>
#include "key_input.h"
#include "base/view_environment.h"

namespace xl::detail {
struct SelectionPointer {float x=0,y=0;bool extend=false;};
struct ScrollPointer {float x=0,y=0,dx=0,dy=0;};
class DesktopFailure : public std::runtime_error {
public:
    DesktopFailure(xl_status code, const char* message) : std::runtime_error(message), status(code) {}
    xl_status status;
};
class DesktopWindow final {
public:
    explicit DesktopWindow(const xl_view_options& options);
    ~DesktopWindow();
    static xl_status pump(uint32_t flags);
    static bool busy();
    void size(int& width, int& height) const;
    ViewEnvironment environment() const;
    void resize(int width, int height);
    void handle(uint32_t& kind, uintptr_t& window) const;
    bool exposed() const;
    bool close_requested() const;
    bool present(const void* rgba, int x, int y, int width, int height, int pitch);
    void selection_input(bool enabled);
    bool selection_pointer(SelectionPointer& pointer);
    void scroll_input(bool enabled);
    bool scroll_pointer(ScrollPointer& pointer);
    bool input_pending() const;
    void keyboard_input(bool enabled);
    bool key_input(WindowInput& input);
    void text_input(bool active, xl_node_bounds caret);
    std::string clipboard_text() const;
    void clipboard_text(std::string_view text);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
