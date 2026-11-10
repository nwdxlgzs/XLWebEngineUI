#include "desktop.h"
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "sdl_keys.h"
#include <cstring>
#include <algorithm>
#include <cmath>
#include <deque>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
#ifdef __linux__
#include <X11/Xlib.h>
#endif

namespace xl::detail {
namespace {
std::mutex video_mutex;
std::thread::id video_owner;
size_t video_users = 0;
bool transitioning = false;
unsigned operation_depth = 0;

struct VideoSession {
    bool active = false;
    ~VideoSession() {
        if (!active) return;
        bool last;
        {
            std::lock_guard lock(video_mutex);
            last = --video_users == 0;
            if (last) transitioning = true;
        }
        if (last) {
            SDL_Quit();
            std::lock_guard lock(video_mutex);
            transitioning = false;
        }
    }
};
std::shared_ptr<VideoSession> acquire_video() {
    // Allocate before changing global ownership; failed bookkeeping changes no state.
    auto session = std::make_shared<VideoSession>();
    {
        std::lock_guard lock(video_mutex);
        if (video_owner != std::thread::id{} && video_owner != std::this_thread::get_id()) {
            throw DesktopFailure(XL_WRONG_THREAD, "native views use the host main/video thread");
        }
        if (transitioning) {
            throw DesktopFailure(XL_BUSY, "private video subsystem is changing state");
        }
        if (video_users) {
            ++video_users;
            session->active = true;
            return session;
        }
        video_owner = std::this_thread::get_id();
        transitioning = true;
    }
    // No SDK mutex is held while platform code can dispatch host messages.
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
    const bool initialized = SDL_Init(SDL_INIT_VIDEO);
    char error[512]{};
    if (!initialized) {
        std::snprintf(error, sizeof(error), "%s", SDL_GetError());
        SDL_Quit();
    }
    {
        std::lock_guard lock(video_mutex);
        transitioning = false;
        if (initialized) { video_users = 1; session->active = true; }
    }
    if (!initialized) {
        throw DesktopFailure(XL_INTERNAL_ERROR, error);
    }
    return session;
}
struct Operation {
    Operation() {
        std::lock_guard lock(video_mutex);
        if (video_owner != std::this_thread::get_id()) throw DesktopFailure(XL_WRONG_THREAD, "wrong video thread");
        if (transitioning || operation_depth) throw DesktopFailure(XL_BUSY, "reentrant platform operation");
        ++operation_depth;
    }
    ~Operation() { std::lock_guard lock(video_mutex); --operation_depth; }
};
}

struct DesktopWindow::Impl {
    static std::unordered_map<SDL_WindowID, Impl*> windows;
    std::shared_ptr<VideoSession> session;
    SDL_Window* window = nullptr;
    bool borrowed = false, exposed = true, closed = false;
    bool selection_enabled = false, selecting = false;
    std::deque<SelectionPointer> selection_pointers;
    bool scroll_enabled = false;
    std::deque<ScrollPointer> scroll_pointers;
    bool keyboard_enabled = false;
    std::deque<WindowInput> keys;
    size_t input_bytes = 0;
    xl_node_bounds text_area{};
    bool text_area_ready = false;
    uint32_t native_kind = XL_NATIVE_NONE;
    uintptr_t native_value = 0;
#ifdef __linux__
    Display* connection = nullptr;
#endif
    ~Impl() {
        if (window) {
            windows.erase(SDL_GetWindowID(window));
            // Creation rollback can already be inside its platform Operation.
            auto destroy = [&] {
                SDL_DestroyWindow(window);
#ifdef __linux__
                // The host may use another X connection and destroy its window
                // immediately after return. Complete queued detach requests first.
                if (borrowed && connection) XSync(connection, False);
#endif
            };
            if (DesktopWindow::busy()) destroy();
            else { Operation operation; destroy(); }
        }
    }
};
std::unordered_map<SDL_WindowID, DesktopWindow::Impl*> DesktopWindow::Impl::windows;

bool DesktopWindow::busy() {
    std::lock_guard lock(video_mutex);
    return video_owner == std::this_thread::get_id() && (transitioning || operation_depth);
}
DesktopWindow::DesktopWindow(const xl_view_options& options) : impl_(std::make_unique<Impl>()) {
    if (options.kind == XL_VIEW_NATIVE) {
#ifdef _WIN32
        if (options.native_kind != XL_NATIVE_WIN32) throw DesktopFailure(XL_UNSUPPORTED, "expected a Win32 native window");
#elif defined(__linux__)
        if (options.native_kind != XL_NATIVE_X11) throw DesktopFailure(XL_UNSUPPORTED, "expected an X11 native window");
#else
        throw DesktopFailure(XL_UNSUPPORTED, "native wrapping is not implemented on this platform");
#endif
    }
    impl_->session = acquire_video();
    Operation operation;
    if (options.kind == XL_VIEW_NATIVE) {
        for (const auto& [id, window] : Impl::windows) {
            (void)id;
            if (window->native_kind == options.native_kind && window->native_value == options.native_window)
                throw DesktopFailure(XL_BUSY, "native window already has an SDK view");
        }
        const auto properties = SDL_CreateProperties();
        if (!properties) throw DesktopFailure(XL_OUT_OF_MEMORY, SDL_GetError());
        bool configured = false;
#ifdef _WIN32
        if (options.native_kind == XL_NATIVE_WIN32)
            configured = SDL_SetPointerProperty(properties, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, reinterpret_cast<void*>(options.native_window));
#elif defined(__linux__)
        if (options.native_kind == XL_NATIVE_X11)
            configured = SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER, static_cast<Sint64>(options.native_window));
#endif
        if (configured) impl_->window = SDL_CreateWindowWithProperties(properties);
        SDL_DestroyProperties(properties);
        if (!configured) throw DesktopFailure(XL_UNSUPPORTED, "native handle type does not match this platform");
        impl_->borrowed = true;
    } else {
        SDL_WindowFlags flags = 0;
        if (options.flags & XL_VIEW_HIDDEN) flags |= SDL_WINDOW_HIDDEN;
        if (options.flags & XL_VIEW_RESIZABLE) flags |= SDL_WINDOW_RESIZABLE;
        impl_->window = SDL_CreateWindow(options.title_utf8 ? options.title_utf8 : "XLWebEngineUI", options.width, options.height, flags);
    }
    if (!impl_->window) throw DesktopFailure(XL_INTERNAL_ERROR, SDL_GetError());
    const auto native_properties = SDL_GetWindowProperties(impl_->window);
#ifdef _WIN32
    impl_->native_kind = XL_NATIVE_WIN32;
    impl_->native_value = reinterpret_cast<uintptr_t>(SDL_GetPointerProperty(native_properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#elif defined(__linux__)
    impl_->native_kind = XL_NATIVE_X11;
    impl_->native_value = static_cast<uintptr_t>(SDL_GetNumberProperty(native_properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    impl_->connection = static_cast<Display*>(SDL_GetPointerProperty(native_properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
#endif
    Impl::windows.emplace(SDL_GetWindowID(impl_->window), impl_.get());
}
DesktopWindow::~DesktopWindow() = default;
xl_status DesktopWindow::pump(uint32_t flags) {
    if (flags & ~XL_PUMP_NATIVE_EVENTS) return XL_INVALID_ARGUMENT;
    {
        std::lock_guard lock(video_mutex);
        if (!video_users) return XL_OK;
    }
    Operation operation;
    if (flags & XL_PUMP_NATIVE_EVENTS) SDL_PumpEvents();
    SDL_Event event;
    int result;
    while ((result = SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST)) > 0) {
        if (event.type == SDL_EVENT_QUIT) {
            for (auto& [id, window] : Impl::windows) { (void)id; if (!window->borrowed) window->closed = true; }
        } else if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST) {
            const auto found = Impl::windows.find(event.window.windowID);
            if (found == Impl::windows.end()) continue;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) found->second->closed = true;
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                found->second->selecting = false;
                found->second->keys.clear(); found->second->input_bytes = 0; found->second->keys.emplace_back();
            }
            if (event.type == SDL_EVENT_WINDOW_EXPOSED || event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
                    || event.type == SDL_EVENT_WINDOW_RESTORED) found->second->exposed = true;
        } else if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            const auto found = Impl::windows.find(event.key.windowID);
            if (found == Impl::windows.end() || !found->second->keyboard_enabled) continue;
            auto& keys = found->second->keys;
            if (keys.size() >= 128) { keys.clear(); found->second->input_bytes = 0; keys.emplace_back(); }
            WindowInput input; input.kind = WindowInput::Kind::key; input.key = keyboard_event(event.key);
            keys.push_back(std::move(input));
        } else if (event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING) {
            const bool composing = event.type == SDL_EVENT_TEXT_EDITING;
            const auto found = Impl::windows.find(composing ? event.edit.windowID : event.text.windowID);
            if (found == Impl::windows.end() || !found->second->keyboard_enabled) continue;
            WindowInput input; input.kind = composing ? WindowInput::Kind::composition : WindowInput::Kind::text;
            const auto* text = composing ? event.edit.text : event.text.text;
            if (text) {
                const auto length = std::strlen(text);
                if (length > 4 * 1024 * 1024) continue;
                input.text.assign(text, length);
            }
            if (composing) { input.start = event.edit.start; input.length = event.edit.length; }
            auto& keys = found->second->keys;
            if (keys.size() >= 128 || found->second->input_bytes + input.text.size() > 4 * 1024 * 1024) {
                keys.clear(); found->second->input_bytes = 0; keys.emplace_back();
            }
            found->second->input_bytes += input.text.size();
            keys.push_back(std::move(input));
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            const auto found = Impl::windows.find(event.wheel.windowID);
            if (found == Impl::windows.end() || !found->second->scroll_enabled) continue;
            auto& window = *found->second;
            int lw = 0, lh = 0, pw = 0, ph = 0;
            if (!SDL_GetWindowSize(window.window, &lw, &lh) || !SDL_GetWindowSizeInPixels(window.window, &pw, &ph) || lw <= 0 || lh <= 0) continue;
            const auto direction = event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? 1.0f : -1.0f;
            ScrollPointer pointer{event.wheel.mouse_x * pw / lw, event.wheel.mouse_y * ph / lh,
                event.wheel.x * direction * 48 * pw / lw, event.wheel.y * direction * 48 * ph / lh};
            if (!std::isfinite(pointer.x) || !std::isfinite(pointer.y) || !std::isfinite(pointer.dx) || !std::isfinite(pointer.dy)) continue;
            if (!window.scroll_pointers.empty() && window.scroll_pointers.back().x == pointer.x && window.scroll_pointers.back().y == pointer.y) {
                auto& previous = window.scroll_pointers.back();
                previous.dx = float(std::clamp(double(previous.dx) + pointer.dx, -1e9, 1e9));
                previous.dy = float(std::clamp(double(previous.dy) + pointer.dy, -1e9, 1e9));
            } else {
                if (window.scroll_pointers.size() >= 64) window.scroll_pointers.pop_front();
                window.scroll_pointers.push_back(pointer);
            }
        } else if(event.type==SDL_EVENT_MOUSE_BUTTON_DOWN || event.type==SDL_EVENT_MOUSE_BUTTON_UP || event.type==SDL_EVENT_MOUSE_MOTION) {
            const bool motion=event.type==SDL_EVENT_MOUSE_MOTION;
            const auto id=motion?event.motion.windowID:event.button.windowID;
            const auto found=Impl::windows.find(id);
            if(found==Impl::windows.end())continue;
            auto& window=*found->second;
            if(!window.selection_enabled)continue;
            if(!motion && event.button.button!=SDL_BUTTON_LEFT)continue;
            const bool down=event.type==SDL_EVENT_MOUSE_BUTTON_DOWN;
            if(!down && !window.selecting)continue;
            const auto x=motion?event.motion.x:event.button.x,y=motion?event.motion.y:event.button.y;
            int logical_width=0,logical_height=0,pixel_width=0,pixel_height=0;
            if(!SDL_GetWindowSize(window.window,&logical_width,&logical_height) ||
               !SDL_GetWindowSizeInPixels(window.window,&pixel_width,&pixel_height) || logical_width<=0 || logical_height<=0)continue;
            SelectionPointer pointer{x*pixel_width/logical_width,y*pixel_height/logical_height,
                                     !down || bool(SDL_GetModState()&SDL_KMOD_SHIFT)};
            if (window.keyboard_enabled) {
                auto& inputs = window.keys;
                WindowInput input; input.kind = WindowInput::Kind::pointer;
                input.x = pointer.x; input.y = pointer.y; input.extend = pointer.extend;
                input.pointer_type = motion ? XL_POINTER_MOVE : down ? XL_POINTER_DOWN : XL_POINTER_UP;
                input.pointer_modifiers = (SDL_GetModState() & SDL_KMOD_SHIFT) ? XL_MOD_SHIFT : 0;
                if (motion && !inputs.empty() && inputs.back().kind == WindowInput::Kind::pointer && inputs.back().pointer_type == XL_POINTER_MOVE)
                    inputs.back() = std::move(input);
                else {
                    if (inputs.size() >= 128) { inputs.clear(); window.input_bytes = 0; inputs.emplace_back(); input.extend = false; }
                    inputs.push_back(std::move(input));
                }
            } else if(motion && !window.selection_pointers.empty() && window.selection_pointers.back().extend)
                window.selection_pointers.back()=pointer;
            else {
                if(window.selection_pointers.size()>=64){window.selection_pointers.clear();pointer.extend=false;}
                window.selection_pointers.push_back(pointer);
            }
            if(!motion)window.selecting=down;
        }
    }
    return result < 0 ? XL_INTERNAL_ERROR : XL_OK;
}
void DesktopWindow::size(int& width, int& height) const {
    Operation operation;
    if (!SDL_GetWindowSizeInPixels(impl_->window, &width, &height)) throw DesktopFailure(XL_INTERNAL_ERROR, SDL_GetError());
}
ViewEnvironment DesktopWindow::environment() const {
    Operation operation;
    ViewEnvironment result;
    result.hidden=(SDL_GetWindowFlags(impl_->window)&(SDL_WINDOW_HIDDEN|SDL_WINDOW_MINIMIZED))!=0;
    result.color_scheme=SDL_GetSystemTheme()==SDL_SYSTEM_THEME_DARK?1u:0u;
    const bool mouse=SDL_HasMouse();int touches=0;auto* devices=SDL_GetTouchDevices(&touches);SDL_free(devices);
    result.pointer=mouse?2u:touches?1u:0u;result.hover=mouse?1u:0u;
    result.any_pointer=(mouse?2u:0u)|(touches?1u:0u);result.any_hover=result.hover;
#ifdef _WIN32
    BOOL animations=TRUE;if(SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&animations,0))result.reduced_motion=animations?0u:1u;
    HIGHCONTRASTW contrast{sizeof(contrast),0,nullptr};
    if(SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0)&&bool(contrast.dwFlags&HCF_HIGHCONTRASTON)){result.contrast=1;result.forced_colors=1;}
#endif
    if (!SDL_GetWindowSizeInPixels(impl_->window, &result.width, &result.height)) throw DesktopFailure(XL_INTERNAL_ERROR, SDL_GetError());
    const auto* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(impl_->window));
    if (mode) {
        result.screen_width = mode->w; result.screen_height = mode->h;
        result.color_depth = SDL_BITSPERPIXEL(mode->format);
    }
    return result;
}
void DesktopWindow::resize(int width, int height) {
    if (impl_->borrowed) throw DesktopFailure(XL_UNSUPPORTED, "the host resizes its borrowed native window");
    Operation operation;
    if (!SDL_SetWindowSize(impl_->window, width, height)) throw DesktopFailure(XL_INTERNAL_ERROR, SDL_GetError());
    impl_->exposed = true;
}
void DesktopWindow::handle(uint32_t& kind, uintptr_t& window) const {
    kind = impl_->native_kind;
    window = impl_->native_value;
}
bool DesktopWindow::exposed() const { return impl_->exposed; }
bool DesktopWindow::close_requested() const { return impl_->closed; }
void DesktopWindow::selection_input(bool enabled) {
    if(impl_->borrowed)throw DesktopFailure(XL_UNSUPPORTED,"borrowed windows forward host selection coordinates");
    impl_->selection_enabled=enabled;impl_->selecting=false;impl_->selection_pointers.clear();
}
bool DesktopWindow::selection_pointer(SelectionPointer& pointer) {
    if(impl_->selection_pointers.empty())return false;
    pointer=impl_->selection_pointers.front();impl_->selection_pointers.pop_front();return true;
}
void DesktopWindow::scroll_input(bool enabled) {
    if (impl_->borrowed) throw DesktopFailure(XL_UNSUPPORTED, "borrowed windows forward host scroll coordinates");
    impl_->scroll_enabled = enabled; impl_->scroll_pointers.clear();
}
bool DesktopWindow::scroll_pointer(ScrollPointer& pointer) {
    if (impl_->scroll_pointers.empty()) return false;
    pointer = impl_->scroll_pointers.front(); impl_->scroll_pointers.pop_front(); return true;
}
bool DesktopWindow::input_pending() const { return !impl_->scroll_pointers.empty() || !impl_->selection_pointers.empty() || !impl_->keys.empty(); }
void DesktopWindow::keyboard_input(bool enabled) {
    if (impl_->borrowed) throw DesktopFailure(XL_UNSUPPORTED, "borrowed windows forward host key events");
    Operation operation;
    impl_->keyboard_enabled = enabled; impl_->keys.clear(); impl_->input_bytes = 0;
    if (!enabled) SDL_StopTextInput(impl_->window);
}
bool DesktopWindow::key_input(WindowInput& input) {
    if (impl_->keys.empty()) return false;
    impl_->input_bytes -= impl_->keys.front().text.size();
    input = std::move(impl_->keys.front()); impl_->keys.pop_front(); return true;
}
std::string DesktopWindow::clipboard_text() const {
    if (impl_->borrowed) throw DesktopFailure(XL_UNSUPPORTED, "borrowed windows use the host clipboard handler");
    Operation operation;
    char* bytes = SDL_GetClipboardText();
    if (!bytes) throw DesktopFailure(XL_INTERNAL_ERROR, SDL_GetError());
    struct Free { char* bytes; ~Free() { SDL_free(bytes); } } free{bytes};
    size_t size = 0;
    while (size <= 4 * 1024 * 1024 && bytes[size]) ++size;
    if (size > 4 * 1024 * 1024) throw DesktopFailure(XL_INVALID_ARGUMENT, "clipboard text exceeds 4MiB");
    return {bytes, size};
}
void DesktopWindow::clipboard_text(std::string_view text) {
    if (impl_->borrowed) throw DesktopFailure(XL_UNSUPPORTED, "borrowed windows use the host clipboard handler");
    const std::string terminated(text);
    Operation operation;
    if (!SDL_SetClipboardText(terminated.c_str())) throw DesktopFailure(XL_INTERNAL_ERROR, SDL_GetError());
}
void DesktopWindow::text_input(bool active, xl_node_bounds caret) {
    if (impl_->borrowed) return;
    Operation operation;
    active = active && impl_->keyboard_enabled;
    if (!active) {
        if (SDL_TextInputActive(impl_->window)) SDL_StopTextInput(impl_->window);
        impl_->text_area_ready = false; return;
    }
    if (!SDL_TextInputActive(impl_->window)) {
        if (!SDL_StartTextInput(impl_->window)) throw DesktopFailure(XL_INTERNAL_ERROR, SDL_GetError());
        impl_->text_area_ready = false;
    }
    int lw = 0, lh = 0, pw = 0, ph = 0;
    if (!SDL_GetWindowSize(impl_->window, &lw, &lh) || !SDL_GetWindowSizeInPixels(impl_->window, &pw, &ph) || pw <= 0 || ph <= 0) return;
    const auto coordinate = [](double value) { return int(std::clamp(value, -1000000000.0, 1000000000.0)); };
    SDL_Rect area{coordinate(std::floor(double(caret.x) * lw / pw)), coordinate(std::floor(double(caret.y) * lh / ph)),
        std::max(1, coordinate(std::ceil(double(caret.width) * lw / pw))), std::max(1, coordinate(std::ceil(double(caret.height) * lh / ph)))};
    const xl_node_bounds next{float(area.x), float(area.y), float(area.w), float(area.h)};
    const auto& old = impl_->text_area;
    if (impl_->text_area_ready && old.x == next.x && old.y == next.y && old.width == next.width && old.height == next.height) return;
    if (!SDL_SetTextInputArea(impl_->window, &area, 0)) throw DesktopFailure(XL_INTERNAL_ERROR, SDL_GetError());
    impl_->text_area = next; impl_->text_area_ready = true;
}
bool DesktopWindow::present(const void* rgba, int x, int y, int width, int height, int pitch) {
    Operation operation;
    auto* surface = SDL_GetWindowSurface(impl_->window);
    if (!surface || x < 0 || y < 0 || width > surface->w - x || height > surface->h - y) return false;
    if (!SDL_LockSurface(surface)) return false;
    const auto bytes = SDL_BYTESPERPIXEL(surface->format);
    auto* destination = static_cast<Uint8*>(surface->pixels) + size_t(y) * surface->pitch + size_t(x) * bytes;
    const bool converted = SDL_ConvertPixels(width, height, SDL_PIXELFORMAT_RGBA32, rgba, pitch, surface->format, destination, surface->pitch);
    SDL_UnlockSurface(surface);
    SDL_Rect rectangle{x, y, width, height};
    if (!converted || !SDL_UpdateWindowSurfaceRects(impl_->window, &rectangle, 1)) return false;
    impl_->exposed = false;
    return true;
}
}
