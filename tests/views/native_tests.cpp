#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #value); std::abort(); } } while (false)
static uintptr_t handle(xl_view* view) {
    uint32_t kind; uintptr_t window;
    CHECK(xl_view_native_handle(view, &kind, &window) == XL_OK && window);
#ifdef _WIN32
    CHECK(kind == XL_NATIVE_WIN32);
#else
    CHECK(kind == XL_NATIVE_X11);
#endif
    return window;
}
#ifndef _WIN32
static Display* connection;
#endif
static void pixel(uintptr_t window, bool green) {
    bool correct = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
#ifdef _WIN32
        auto dc = GetDC(reinterpret_cast<HWND>(window));
        const auto actual = GetPixel(dc, 8, 8);
        ReleaseDC(reinterpret_cast<HWND>(window), dc);
        correct = actual == (green ? RGB(0, 255, 0) : RGB(255, 0, 0));
#else
        XSync(connection, False);
        auto* image = XGetImage(connection, window, 8, 8, 1, 1, AllPlanes, ZPixmap);
        CHECK(image);
        const auto actual = XGetPixel(image, 0, 0);
        correct = (actual & image->red_mask) == (green ? 0 : image->red_mask)
               && (actual & image->green_mask) == (green ? image->green_mask : 0) && !(actual & image->blue_mask);
        XDestroyImage(image);
#endif
        if (!correct) std::this_thread::yield();
    } while (!correct && std::chrono::steady_clock::now() < deadline);
    CHECK(correct);
}
int main() {
#ifdef _WIN32
    _putenv_s("SDL_VIDEODRIVER", "windows");
#else
    setenv("SDL_VIDEODRIVER", "x11", 1);
    connection = XOpenDisplay(nullptr); CHECK(connection);
#endif
    xl::Runtime runtime;
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.width = 192; options.height = 128;
    {
        xl::View view(runtime, &options);
        CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK);
        xl_paint_rect rectangle{0, 0, 24, 24, 0xffff0000, 0};
        view.rects({&rectangle, 1});
        CHECK(view.render() == (XL_FRAME_REDRAWN | XL_FRAME_PRESENTED));
        const auto native = handle(view.get());
        pixel(native, false);
        CHECK(view.render() == 0);
        CHECK(xl_view_request_present(view.get()) == XL_OK);
        auto before = view.stats();
        CHECK(view.render() == XL_FRAME_PRESENTED);
        CHECK(view.stats().raster_frames == before.raster_frames);
        before = view.stats();
        rectangle.argb = 0xff00ff00;
        view.rects({&rectangle, 1});
        CHECK(view.render() == (XL_FRAME_REDRAWN | XL_FRAME_PRESENTED));
        pixel(native, true);
        CHECK(view.stats().uploaded_pixels - before.uploaded_pixels < uint64_t(before.pixel_width) * before.pixel_height);
#ifdef _WIN32
        const auto parent = reinterpret_cast<HWND>(native);
        const auto child = CreateWindowExW(0, L"STATIC", L"host child", WS_CHILD | WS_VISIBLE, 40, 40, 64, 64,
                                           parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        CHECK(child);
        const auto original = GetWindowLongPtrW(child, GWLP_WNDPROC);
        options.native_kind = XL_NATIVE_WIN32;
        options.native_window = reinterpret_cast<uintptr_t>(child);
#else
        const auto child = XCreateSimpleWindow(connection, native, 40, 40, 64, 64, 0, 0, 0);
        CHECK(child); XMapWindow(connection, child); XSync(connection, False);
        options.native_kind = XL_NATIVE_X11; options.native_window = child;
#endif
        options.kind = XL_VIEW_NATIVE;
        {
            xl::Runtime second;
            xl::View borrowed(second, &options);
            rectangle.argb = 0xffff0000;
            borrowed.rects({&rectangle, 1});
            CHECK(xl_runtime_pump_events(second.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK);
            borrowed.render();
            pixel(handle(borrowed.get()), false);
            CHECK(xl_view_resize(borrowed.get(), 80, 80) == XL_UNSUPPORTED);
        }
#ifdef _WIN32
        CHECK(IsWindow(child) && IsWindow(parent) && GetParent(child) == parent);
        CHECK(GetWindowLongPtrW(child, GWLP_WNDPROC) == original);
        CHECK(DestroyWindow(child));
#else
        XWindowAttributes attributes{};
        CHECK(XGetWindowAttributes(connection, child, &attributes));
        XDestroyWindow(connection, child); XSync(connection, False);
#endif
        pixel(native, true);
    }
#ifndef _WIN32
    XCloseDisplay(connection);
#endif
    std::puts("SDK native views: real pixels, partial presentation, retained expose and borrowed ownership passed");
}
