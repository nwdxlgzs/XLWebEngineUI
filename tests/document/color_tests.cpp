#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>
#include <vector>
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            std::cerr << __LINE__ << ": " #c "\n";                                                                     \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)
int main(int argc, char **argv) {
    xl::Runtime runtime;
    xl::Document document(runtime);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN;
    options.width = 120;
    options.height = 80;
    xl::View view(runtime, &options);
    view.document(&document);
    view.ax_updates(true);
    const bool gpu = argc == 2 && std::string_view(argv[1]) == "--gpu";
    if (gpu)
        view.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
    const auto page = [&](const std::string &html) {
        document.html("<style>body{margin:0}#x{width:32px;height:32px}</style>" + html);
    };
    const auto render = [&] {
        uint32_t flags = 0;
        xl_status status;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < end);
            std::this_thread::yield();
        }
        CHECK(status == XL_OK);
        return flags;
    };
    const auto pixels = [&] {
        std::vector<uint8_t> data(120 * 80 * 4);
        CHECK(xl_view_copy_rgba(view.get(), data.data(), data.size(), 120 * 4) == XL_OK);
        return data;
    };
    const auto pixel = [&](int x = 8, int y = 8) {
        const auto data = pixels();
        const auto i = size_t(y * 120 + x) * 4;
        return uint32_t(data[i]) << 24 | uint32_t(data[i + 1]) << 16 | uint32_t(data[i + 2]) << 8 | data[i + 3];
    };
    const auto expected = [&](uint32_t color, const std::source_location &at = std::source_location::current()) {
        render();
        const auto actual = pixel();
        if (actual != color) {
            std::cerr << at.line() << " expected " << std::hex << color << " got " << actual << '\n';
            std::exit(1);
        }
        CHECK(view.render() == 0);
    };
    for (const auto *css :
         {"hsl(0 100% 50%)", "hwb(0 0% 0%)", "lab(54.2917% 80.8125 69.8851)", "oklch(62.7955% .257683 29.2339)",
          "color(srgb 1 0 0)", "color-mix(in srgb,red 100%,blue 0%)"}) {
        page(std::string("<div id=x style='background:") + css + "'></div>");
        expected(0xff0000ff);
    }
    page("<style>html{background:rgb(255 0 0 / 0)}body{background:blue}</style>");
    expected(0x0000ffff);
    page("<style>html{background:rgb(0 0 0 / .001)}body{background:blue}</style>");
    expected(0xffffffff);
    page("<style>#x{background:color-mix(in srgb,red,blue)}</style><div id=x></div>");
    expected(0x800080ff);
    page("<style>#x{background:red;background:light-dark(wrong,blue)}</style><div id=x></div>");
    expected(0xff0000ff);
    page("<style>#x{--bad:light-dark(wrong,blue);background:red;background:var(--bad)}</style><div id=x></div>");
    expected(0xffffffff);
    page("<style>@layer a,b;@layer a{#x{background:hwb(0 0% 0%)!important}}@layer "
         "b{#x{background:blue!important}}</style><div id=x></div>");
    expected(0xff0000ff);
    page("<style>@layer a,b;@layer a{#x{background:hsl(0 100% 50%)}}@layer "
         "b{#x{background:blue;background:revert-layer}}</style><div id=x></div>");
    expected(0xff0000ff);
    xl_media_preferences preferences{};
    preferences.struct_size = sizeof(preferences);
    preferences.color_scheme = XL_MEDIA_COLOR_DARK;
    CHECK(xl_document_set_media_preferences(document.get(), &preferences, sizeof(preferences)) == XL_OK);
    page("<style>:root{color-scheme:light dark}#x{--bg:light-dark(red,blue);background:var(--bg)}</style><div "
         "id=x></div>");
    expected(0x0000ffff);
    preferences.color_scheme = XL_MEDIA_COLOR_LIGHT;
    CHECK(xl_document_set_media_preferences(document.get(), &preferences, sizeof(preferences)) == XL_OK);
    expected(0xff0000ff);
    preferences.color_scheme = XL_MEDIA_COLOR_DARK;
    CHECK(xl_document_set_media_preferences(document.get(), &preferences, sizeof(preferences)) == XL_OK);
    expected(0x0000ffff);
    document.attribute(document.query("#x"), "style", "color-scheme:only light");
    expected(0xff0000ff);
    document.attribute(document.query("#x"), "style", "color-scheme:dark");
    expected(0x0000ffff);
    document.attribute(document.query("#x"), "style", "color-scheme:light;color-scheme:normal dark");
    expected(0xff0000ff);
    document.attribute(document.query("#x"), "style", "--bad:normal dark;color-scheme:light;color-scheme:var(--bad)");
    expected(0x0000ffff);
    page("<style>:root{color-scheme:dark}#x{background:currentColor}</style><div id=x></div>");
    expected(0xffffffff);
    page("<style>:root{color-scheme:dark;color:red}#x{color-scheme:light;color:initial;background:currentColor}</"
         "style><div id=x></div>");
    expected(0x000000ff);
    document.attribute(document.query("#x"), "style", "color:currentcolor");
    expected(0xff0000ff);
    page("<style>#x{--c:light-dark(red,blue);color-scheme:dark;border:4px solid var(--c);background:white;outline:3px "
         "solid color-mix(in srgb,red,blue);margin:8px}</style><div id=x></div>");
    render();
    CHECK(pixel(8, 16) == 0x0000ffff);
    CHECK(pixel(5, 16) == 0x800080ff);
    CHECK(pixel(16, 16) == 0xffffffff);
    page("<style>#x{color-scheme:dark;&::before{content:'';display:block;width:20px;height:20px;background:light-dark("
         "red,blue)}}</style><div id=x role=button aria-label=Target></div>");
    expected(0x0000ffff);
    CHECK(document.pseudo_style(document.query("#x"), "::before", "background-color").find("light-dark") !=
          std::string::npos);
    const auto incremental = pixels();
    CHECK(xl_view_invalidate(view.get(), 0, 0, 120, 80) == XL_OK);
    render();
    CHECK(pixels() == incremental);
    page("<style>#x{background:var(--c)}</style><div id=x></div>");
    for (int i = 0; i < 1060; ++i) {
        document.attribute(document.query("#x"), "style",
                           "--c:rgb(" + std::to_string(i % 256) + " " + std::to_string(i / 256) + " 0)");
        size_t size = 0;
        CHECK(xl_document_get_style(document.get(), document.query("#x"), "background-color", nullptr, 0, &size) ==
              XL_BUFFER_TOO_SMALL);
        if (i == 0 || i == 1059) {
            render();
            CHECK(pixel() == (uint32_t(i % 256) << 24 | uint32_t(i / 256) << 16 | 255));
        }
    }
    xl_resource_stats stats{};
    CHECK(xl_runtime_resource_stats(runtime.get(), &stats, sizeof(stats)) == XL_OK);
    CHECK(stats.decompressions == 0 && stats.worker_threads == 0);
    // A color-only element has an authored inline list and no Lexbor cascade
    // entries: framework-owned colors must not break replacement or removal.
    page("<div id=only style='background:red'><div id=x></div></div>");
    expected(0xff0000ff);
    const auto only = document.query("#only");
    document.attribute(only, "style", "background:blue");
    expected(0x0000ffff);
    CHECK(document.evaluate("document.querySelector('#only').removeAttribute('style')") == XL_OK);
    expected(0xffffffff);
    document.attribute(only, "style", "background:oklch(62.7955% .257683 29.2339)");
    expected(0xff0000ff);
    document.attribute(only, "style", "background:lime");
    expected(0x00ff00ff);
    if (argc == 2 && !gpu) {
        std::ifstream font_file(XL_TEST_FONT, std::ios::binary);
        const std::vector<char> font{std::istreambuf_iterator<char>(font_file), std::istreambuf_iterator<char>()};
        CHECK(!font.empty());
        CHECK(xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
        std::ifstream file(argv[1], std::ios::binary);
        const std::string html{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        CHECK(!html.empty());
        document.html(html);
        render();
        const auto light = pixels();
        CHECK(document.evaluate("document.getElementById('theme-dark').checked=true") == XL_OK);
        render();
        const auto dark = pixels();
        CHECK(light != dark);
        CHECK(view.render() == 0);
    }
    if (gpu)
        CHECK(view.graphics_info().active == XL_GRAPHICS_SWIFTSHADER);
    std::cout
        << "Modern color pixels, cascade, variables, scheme, pseudo, bounded cache and retained rendering passed\n";
}
