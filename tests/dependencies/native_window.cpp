#include <SDL3/SDL.h>
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSurface.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "%s:%d: %s; SDL: %s\n", __FILE__, __LINE__, #condition, SDL_GetError()); \
    std::exit(1); } } while (false)

static void pump() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {}
}

// Rasterize with Skia, convert into the platform window surface, and submit.
static void paint(SDL_Window* window) {
    auto* destination = SDL_GetWindowSurface(window);
    CHECK(destination && destination->w >= 32 && destination->h >= 32);
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(destination->w, destination->h));
    CHECK(surface);
    surface->getCanvas()->clear(SK_ColorWHITE);
    SkPaint brush;
    brush.setColor(SK_ColorRED);
    surface->getCanvas()->drawRect(SkRect::MakeXYWH(0, 0, 24, 24), brush);
    const int pitch = destination->w * 4;
    std::vector<unsigned char> rgba(static_cast<size_t>(pitch) * destination->h);
    const auto info = SkImageInfo::Make(destination->w, destination->h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    CHECK(surface->readPixels(info, rgba.data(), pitch, 0, 0));
    CHECK(SDL_LockSurface(destination));
    CHECK(SDL_ConvertPixels(destination->w, destination->h, SDL_PIXELFORMAT_RGBA32, rgba.data(), pitch,
                            destination->format, destination->pixels, destination->pitch));
    SDL_UnlockSurface(destination);
    CHECK(SDL_UpdateWindowSurface(window));
    Uint8 r = 0, g = 0, b = 0, a = 0;
    CHECK(SDL_ReadSurfacePixel(destination, 8, 8, &r, &g, &b, &a));
    CHECK(r == 255 && g == 0 && b == 0);
}

#ifdef _WIN32
static HWND hwnd(SDL_Window* window) {
    return static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
}

static void read_presented_pixel(SDL_Window* window) {
    auto dc = GetDC(hwnd(window));
    CHECK(dc);
    const auto color = GetPixel(dc, 8, 8);
    ReleaseDC(hwnd(window), dc);
    CHECK(color == RGB(255, 0, 0));
}

static void embedded_child(SDL_Window* host) {
    auto parent = hwnd(host);
    CHECK(parent);
    auto child = CreateWindowExW(0, L"STATIC", L"XL embedded view", WS_CHILD | WS_VISIBLE,
                                 32, 32, 96, 64, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    CHECK(child);
    const auto original_proc = GetWindowLongPtrW(child, GWLP_WNDPROC);
    auto properties = SDL_CreateProperties();
    CHECK(properties && SDL_SetPointerProperty(properties, SDL_PROP_WINDOW_CREATE_WIN32_HWND_POINTER, child));
    auto* view = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    CHECK(view && hwnd(view) == child && GetParent(child) == parent);
    pump();
    paint(view);
    read_presented_pixel(view);
    SDL_DestroyWindow(view);
    CHECK(IsWindow(child) && IsWindow(parent) && GetParent(child) == parent);
    CHECK(GetWindowLongPtrW(child, GWLP_WNDPROC) == original_proc);
    CHECK(DestroyWindow(child));
    CHECK(IsWindow(parent));
}
#else
static Display* display(SDL_Window* window) {
    return static_cast<Display*>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
}

static Window xid(SDL_Window* window) {
    return static_cast<Window>(SDL_GetNumberProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
}

static void read_presented_pixel(SDL_Window* window) {
    auto* connection = display(window);
    XSync(connection, False);
    auto* pixels = XGetImage(connection, xid(window), 8, 8, 1, 1, AllPlanes, ZPixmap);
    CHECK(pixels);
    const auto pixel = XGetPixel(pixels, 0, 0);
    CHECK((pixel & pixels->red_mask) == pixels->red_mask);
    CHECK((pixel & pixels->green_mask) == 0 && (pixel & pixels->blue_mask) == 0);
    XDestroyImage(pixels);
}

static void embedded_child(SDL_Window* host) {
    auto* connection = display(host);
    const auto parent = xid(host);
    CHECK(connection && parent);
    auto child = XCreateSimpleWindow(connection, parent, 32, 32, 96, 64, 0, 0, 0);
    CHECK(child);
    XMapWindow(connection, child);
    XSync(connection, False);
    auto properties = SDL_CreateProperties();
    CHECK(properties && SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X11_WINDOW_NUMBER, child));
    auto* view = SDL_CreateWindowWithProperties(properties);
    SDL_DestroyProperties(properties);
    CHECK(view && xid(view) == child);
    pump();
    paint(view);
    read_presented_pixel(view);
    SDL_DestroyWindow(view);
    XWindowAttributes attributes{};
    CHECK(XGetWindowAttributes(connection, child, &attributes));
    CHECK(XGetWindowAttributes(connection, parent, &attributes));
    Window root = 0, actual_parent = 0, *children = nullptr;
    unsigned int count = 0;
    CHECK(XQueryTree(connection, child, &root, &actual_parent, &children, &count));
    if (children) XFree(children);
    CHECK(actual_parent == parent);
    XDestroyWindow(connection, child);
    XSync(connection, False);
    CHECK(XGetWindowAttributes(connection, parent, &attributes));
}
#endif

int main() {
#ifdef _WIN32
    constexpr auto driver = "windows";
#else
    constexpr auto driver = "x11";
#endif
    CHECK(SDL_SetHint(SDL_HINT_VIDEO_DRIVER, driver));
    CHECK(SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0"));
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    CHECK(std::strcmp(SDL_GetCurrentVideoDriver(), driver) == 0);
    auto* host = SDL_CreateWindow("XL native dependency verification", 192, 128, 0);
    CHECK(host);
    pump();
    paint(host);
    read_presented_pixel(host);
    embedded_child(host);
    paint(host);
    read_presented_pixel(host);
    SDL_DestroyWindow(host);
    SDL_Quit();
    std::printf("%s: native Skia/SDL presentation and external child ownership passed\n", driver);
}
