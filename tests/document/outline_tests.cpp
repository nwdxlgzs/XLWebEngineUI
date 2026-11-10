#include <XLWebEngineUI/xl.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <source_location>
#include <thread>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
void render(xl::View& view) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    xl_status status; uint32_t flags;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < deadline); std::this_thread::yield();
    }
    CHECK(status == XL_OK);
}
std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats = view.stats(); std::vector<unsigned char> result(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), result.data(), result.size(), size_t(stats.pixel_width) * 4) == XL_OK);
    return result;
}
uint32_t pixel(const std::vector<unsigned char>& data, int x, int y) {
    const auto i = size_t(y * 320 + x) * 4;
    return uint32_t(data[i + 3]) << 24 | uint32_t(data[i]) << 16 | uint32_t(data[i + 1]) << 8 | data[i + 2];
}
bool same(xl_node_bounds a, xl_node_bounds b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }
}
int main() {
    xl::Runtime runtime; xl::Document document(runtime);
    std::ifstream stream(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 320; options.height = 240;
    xl::View view(runtime, &options); view.document(&document);
    const auto eval = [&](const std::string& source, const char* expected = "ok",
                          const std::source_location& at = std::source_location::current()) {
        const auto status = document.evaluate(source.c_str());
        if (status != XL_OK || document.result() != expected)
            std::cerr << "script at " << at.line() << ": " << xl_status_string(status) << ' ' << document.result() << '\n';
        CHECK(status == XL_OK && document.result() == expected);
    };
    const std::string base = "<!doctype html><style>body{margin:0;background-color:white;font:20px/24px test}"
        "#box{margin:40px;width:60px;height:40px;background-color:#eeeeee}</style>";
    const auto page = [&](const std::string& html) { document.html(base + html); render(view); };
    const auto bounds = [&](const char* selector) {
        xl_node_bounds box{}; CHECK(xl_view_node_bounds(view.get(), document.query(selector), &box) == XL_OK); return box;
    };
    const auto full_matches = [&] {
        render(view); const auto incremental = pixels(view);
        CHECK(xl_view_invalidate(view.get(), 0, 0, 320, 240) == XL_OK); render(view);
        CHECK(incremental == pixels(view)); CHECK(view.render() == 0);
    };
    const auto ax_bounds = [&](xl_node_id node) {
        size_t count = 0; CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
        for (size_t i = 0; i < count; ++i) {
            xl_ax_node entry{}; CHECK(xl_view_ax_get(view.get(), i, &entry, sizeof(entry)) == XL_OK);
            if (entry.node == node) return entry.bounds;
        }
        CHECK(false); return xl_node_bounds{};
    };
    page("<div id=box role=button></div><div id=after style='height:10px'></div>");
    const auto before = bounds("#box"), after = bounds("#after");
    const auto before_ax = ax_bounds(document.query("#box"));
    const auto scroll = view.scroll();
    const auto rasterized = view.stats().rasterized_pixels;
    eval("let b=document.getElementById('box');b.setAttribute('style','outline:4px solid red;outline-offset:3px');'ok'");
    render(view); CHECK(same(before, bounds("#box")) && same(after, bounds("#after")));
    CHECK(same(before_ax, ax_bounds(document.query("#box"))));
    CHECK(view.stats().rasterized_pixels - rasterized < 320 * 240);
    CHECK(view.scroll().max_y == scroll.max_y && view.scroll().max_x == scroll.max_x);
    auto image = pixels(view);
    CHECK(pixel(image, 34, 50) == 0xffff0000 && pixel(image, 38, 50) == 0xffffffff && pixel(image, 50, 34) == 0xffff0000);
    full_matches();
    eval("b.setAttribute('style','outline:4px solid red;outline-offset:-6px');'ok'");
    render(view); image = pixels(view);
    CHECK(pixel(image, 43, 50) == 0xffff0000 && pixel(image, 34, 50) == 0xffffffff);
    full_matches();
    eval("b.setAttribute('style','outline:6px double red');'ok'"); render(view); image = pixels(view);
    CHECK(pixel(image, 35, 50) == 0xffff0000 && pixel(image, 37, 50) == 0xffffffff && pixel(image, 39, 50) == 0xffff0000);
    full_matches();
    std::set<std::vector<unsigned char>> appearances;
    for (const char* style : {"solid", "dotted", "dashed", "double", "groove", "ridge", "inset", "outset", "auto"}) {
        eval("b.setAttribute('style','outline:6px " + std::string(style) + " #808080');'ok'"); render(view);
        appearances.insert(pixels(view)); full_matches();
    }
    CHECK(appearances.size() == 9);
    for (const char* style : {"inset", "outset"}) {
        eval("b.setAttribute('style','outline:6px " + std::string(style) + " #808080');'ok'"); render(view);
        image = pixels(view);
        const auto top = pixel(image, 60, 36), bottom = pixel(image, 60, 83);
        CHECK((style[0] == 'i' ? top < bottom : top > bottom));
        CHECK(pixel(image, 36, 55) == top && pixel(image, 103, 55) == bottom);
    }
    for (const char* style : {"outline:0 solid red", "outline:none", "outline:5px solid transparent", "outline:initial", "outline:unset"}) {
        eval("b.setAttribute('style','" + std::string(style) + "');'ok'"); full_matches();
        CHECK(pixel(pixels(view), 38, 50) == 0xffffffff);
    }
    eval("b.setAttribute('style','outline:4px solid red;outline-offset:0');'ok'"); render(view);
    CHECK(pixel(pixels(view), 38, 50) == 0xffff0000);
    eval("b.setAttribute('style','outline:4px solid red;outline:2px hidden blue;outline-width:-1px;outline-offset:3px;outline-offset:2%;outline-color:garbage');"
         "getComputedStyle(b).getPropertyValue('outline-width')+':'+getComputedStyle(b).getPropertyValue('outline-style')+':'+getComputedStyle(b).getPropertyValue('outline-color')+':'+getComputedStyle(b).getPropertyValue('outline-offset')", "4px:solid:red:3px");
    render(view); CHECK(pixel(pixels(view), 34, 50) == 0xffff0000);
    eval("b.setAttribute('style','outline:solid currentColor;outline-width:.2em;outline-offset:1pt;color:blue');'ok'"); render(view);
    image = pixels(view); CHECK(pixel(image, 36, 50) == 0xff0000ff); full_matches();
    page("<style>#box{outline:4px solid red!important;outline-offset:3px}#box{outline:1px solid blue}</style>"
         "<div id=box style='outline:2px solid lime'></div>");
    CHECK(pixel(pixels(view), 34, 50) == 0xffff0000);
    eval("let b=document.getElementById('box');b.setAttribute('style','outline:5px solid blue!important');'ok'"); render(view);
    CHECK(pixel(pixels(view), 34, 50) == 0xff0000ff); full_matches();
    // Outline shorthand resets only width/style/color; offset is independent.
    eval("b.setAttribute('style','outline-offset:-1000px;outline:6px solid red!important');'ok'"); full_matches();
    image = pixels(view); CHECK(pixel(image, 70, 60) == 0xffff0000);
    // Inherit copies computed color, while currentColor uses the child's color.
    page("<section style='outline:4px solid red;outline-offset:2px'><div id=box style='outline:inherit;color:blue'></div></section>");
    CHECK(pixel(pixels(view), 37, 50) == 0xffff0000);
    eval("let b=document.getElementById('box');b.setAttribute('style','outline:initial');'ok'"); full_matches();
    CHECK(pixel(pixels(view), 37, 50) == 0xffffffff);
    page("<section style='outline:none;outline-width:8px'><div id=box style='outline-style:solid;outline-width:inherit;outline-color:red'></div></section>");
    CHECK(pixel(pixels(view), 38, 50) == 0xffffffff);
    // Own overflow clips contents but not the element's own outline. Ancestors clip both.
    page("<div id=box style='overflow:hidden;outline:4px solid red'></div>");
    CHECK(pixel(pixels(view), 37, 50) == 0xffff0000);
    page("<div style='margin:30px;width:100px;height:80px;overflow:hidden'><div id=box style='margin:0;outline:8px solid red'></div></div>");
    CHECK(pixel(pixels(view), 26, 45) == 0xffffffff && pixel(pixels(view), 93, 45) == 0xffff0000);
    full_matches();
    page("<div id=port style='margin:20px;width:110px;height:75px;overflow:auto'><div id=box style='margin:20px;height:150px;outline:4px solid red'></div></div>");
    const auto port = document.query("#port"); CHECK(view.scroll(port).max_y > 0);
    view.scroll_to(port, 0, 25); full_matches();
    CHECK(pixel(pixels(view), 37, 16) == 0xffffffff);
    // Multiline outlines union actual fragments. Alpha must be applied once even at joined corners.
    page("<p style='margin:30px;width:120px;color:white'><span id=multi style='outline:4px solid rgba(255,0,0,.5)'>one two three four five six</span></p>");
    image = pixels(view); int colored = 0;
    for (size_t i = 0; i < image.size(); i += 4)
        if (image[i] == 255 && image[i + 1] < 230 && image[i + 2] < 230) { CHECK(image[i + 1] >= 126); ++colored; }
    CHECK(colored > 200); full_matches();
    // A short last line must not gain an outline across its empty right side.
    const auto multi = bounds("#multi");
    CHECK(pixel(pixels(view), int(multi.x + multi.width) - 1, int(multi.y + multi.height) - 3) == 0xffffffff);
    eval("document.getElementById('multi').setAttribute('style','outline:none');'ok'"); full_matches();
    CHECK(pixel(pixels(view), 27, 40) == 0xffffffff);
    // Atomic scenes own their outline and ink. Do not paint it again in the outer frame.
    page("<p style='margin:30px;color:white'><span id=atomic style='display:inline-block;width:60px;height:30px;outline:4px solid rgba(255,0,0,.5)'>x</span></p>");
    image = pixels(view); const auto atomic = bounds("#atomic");
    const auto sample = pixel(image, int(atomic.x) - 2, int(atomic.y) + 8);
    CHECK(((sample >> 8) & 255) >= 126 && ((sample >> 8) & 255) <= 129); full_matches();
    eval("document.getElementById('atomic').setAttribute('style','display:inline-block;width:60px;height:30px;outline:6px solid blue');'ok'");
    full_matches();
    // One inline can be split around a block child: collect those fragments into
    // one retained outline, rather than replacing them with its overall bounds.
    page("<div style='margin:30px;width:150px;color:white'><span id=split style='outline:4px solid rgba(255,0,0,.5)'>one two<div style='width:70px;height:30px'></div>three four</span></div>");
    full_matches(); image = pixels(view);
    for (size_t i = 0; i < image.size(); i += 4)
        if (image[i] == 255 && image[i + 1] < 230 && image[i + 2] < 230) CHECK(image[i + 1] >= 126);
    for (const char* display : {"flex", "grid"}) {
        page("<div style='margin:30px;display:" + std::string(display) + ";width:150px;gap:10px'><div id=child style='width:50px;height:30px;outline:4px solid red'></div></div>");
        const auto child = bounds("#child"); CHECK(pixel(pixels(view), int(child.x) - 2, int(child.y) + 10) == 0xffff0000);
        full_matches();
    }
    page("<button id=box>focus</button>"); eval("document.getElementById('box').focus();'ok'"); render(view);
    image = pixels(view); CHECK(pixel(image, 39, 50) == 0xff175bc2);
    eval("document.getElementById('box').setAttribute('style','outline:none');'ok'"); full_matches();
    CHECK(pixel(pixels(view), 39, 50) == 0xffffffff);
    view.document(nullptr); render(view); CHECK(view.render() == 0);
    std::cout << "outline cascade, fragments, ink damage, alpha, clipping, scroll and atomic scenes passed\n";
}
