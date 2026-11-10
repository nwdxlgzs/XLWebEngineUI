#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
void render(xl::View& view) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags;
    xl_status status;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < deadline); std::this_thread::yield();
    }
    CHECK(status == XL_OK);
}
bool close(float a, float b) { return std::abs(a - b) < .1f; }
std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats = view.stats();
    std::vector<unsigned char> result(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), result.data(), result.size(), size_t(stats.pixel_width) * 4) == XL_OK);
    return result;
}
std::vector<xl_ax_node> ax(xl::View& view) {
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    std::vector<xl_ax_node> result(count);
    for (size_t i = 0; i < count; ++i) CHECK(xl_view_ax_get(view.get(), i, &result[i], sizeof(result[i])) == XL_OK);
    return result;
}
xl_text_position position(xl_node_id id, uint32_t offset) { return {sizeof(xl_text_position), offset, id, XL_TEXT_DOWNSTREAM, 0}; }
}
int main() {
    xl::Runtime runtime;
    xl::Document document(runtime);
    std::ifstream input(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 360; options.height = 240;
    xl::View view(runtime, &options);
    view.document(&document);
    const std::string base = "<!doctype html><style>body,p{margin:0}body{font:20px/24px test}p{width:100px}</style>";
    const auto page = [&](const std::string& html) { document.html(base + html); render(view); };
    const auto bounds = [&](const char* selector) {
        xl_node_bounds result;
        CHECK(xl_view_node_bounds(view.get(), document.query(selector), &result) == XL_OK); return result;
    };
    const auto full_matches = [&](xl_node_id node) {
        xl::View full(runtime, &options); full.document(&document); render(full);
        const auto info = view.scroll(node);
        full.scroll_to(node, info.x, info.y); render(full);
        xl_text_selection selection;
        if (xl_view_get_selection(view.get(), &selection, sizeof(selection)) == XL_OK) {
            full.selection(selection.anchor, selection.focus); render(full);
        }
        CHECK(pixels(full) == pixels(view));
    };
    page("<p style='height:40px;padding:4px;border:2px solid blue;overflow:hidden;background-color:green'>"
         "<span id=a style='display:block;width:200px;height:80px;background-color:red'></span></p>");
    auto id = document.query("p");
    auto info = view.scroll(id);
    CHECK(close(info.client_width, 108) && close(info.client_height, 48));
    CHECK(close(info.max_x, 100) && close(info.max_y, 40));
    auto image = pixels(view);
    CHECK(image[(10u * 360 + 20) * 4] == 255 && image[(10u * 360 + 20) * 4 + 1] == 0 && image[(10u * 360 + 115) * 4 + 1] == 255);
    view.scroll_at(20, 20, 90, 90); render(view);
    CHECK(view.scroll(id).x == 0 && view.scroll(id).y == 0); // hidden accepts only programmatic scroll.
    view.scroll_to(id, 1000, 1000);
    CHECK(view.stats().needs_frame);
    xl_node_bounds stale;
    CHECK(xl_view_node_bounds(view.get(), id, &stale) == XL_BUSY);
    render(view);
    CHECK(close(view.scroll(id).x, 100) && close(view.scroll(id).y, 40));
    CHECK(close(bounds("#a").x, -94) && close(bounds("#a").y, -34));
    full_matches(id);
    CHECK(view.render() == 0);

    page("<p style='height:30px;overflow:clip'><span style='display:block;width:200px;height:100px;background-color:red'></span></p>");
    id = document.query("p");
    CHECK(xl_view_get_scroll(view.get(), id, &info, sizeof(info)) == XL_NOT_FOUND);
    CHECK(xl_view_set_scroll(view.get(), id, 0, 10) == XL_NOT_FOUND);
    CHECK(close(view.scroll().max_y, 0));
    image = pixels(view); CHECK(image[(40u * 360 + 10) * 4 + 1] == 255);

    const std::string original = "one two three four five six";
    page("<p style='width:65px;height:30px;overflow:hidden'><span id=a style='white-space:nowrap'>" + original +
         "</span><span id=b style='display:block;margin-top:60px'>hidden</span></p>");
    id = document.query("p");
    std::vector<xl_ax_node> text;
    for (const auto& node : ax(view)) if (node.role == XL_AX_TEXT) text.push_back(node);
    CHECK(text.size() == 2 && !(text[0].states & XL_AX_OFFSCREEN) && (text[1].states & XL_AX_OFFSCREEN));
    auto rects = view.text_rects(text[0].node, 0, uint32_t(original.size()));
    CHECK(rects.size() == 1 && rects[0].width <= 65.01f);
    CHECK(view.text_rects(text[1].node, 0, 6).empty());
    xl_text_position hit;
    CHECK(xl_view_hit_test_text(view.get(), 120, 10, &hit, sizeof(hit)) == XL_NOT_FOUND);
    CHECK(view.hit_text(30, 12).node == text[0].node);
    view.selection(position(text[0].node, 0), position(text[0].node, uint32_t(original.size()))); render(view);
    CHECK(view.selected_text() == original && view.selection_rects().size() == 1);
    const auto before = view.stats().rasterized_pixels;
    view.scroll_to(id, 75, 0); render(view);
    CHECK(view.selected_text() == original && view.hit_text(30, 12).offset_utf16 > 3);
    CHECK(view.stats().rasterized_pixels - before < 360u * 240u);
    CHECK(view.caret(position(text[0].node, 0)).width == 0);
    full_matches(id);
    xl::View other(runtime, &options); other.document(&document); render(other);
    CHECK(other.scroll(id).x == 0 && close(view.scroll(id).x, 75));
    CHECK(xl_view_set_scroll(view.get(), id, std::numeric_limits<float>::quiet_NaN(), 0) == XL_INVALID_ARGUMENT);
    CHECK(xl_view_get_scroll(view.get(), id, &info, 1) == XL_ABI_MISMATCH);
    xl_status thread_status = XL_OK;
    std::thread wrong([&] { thread_status = xl_view_set_scroll(view.get(), id, 0, 0); }); wrong.join();
    CHECK(thread_status == XL_WRONG_THREAD);
    CHECK(document.evaluate("document.querySelector('p').style.setProperty('width','500px')") == XL_OK); render(view);
    CHECK(view.scroll(id).x == 0 && view.selected_text() == original);

    page("<p style='width:80px;height:40px;overflow:auto'><span id=i style='display:block;height:30px;overflow:auto'>"
         "<span style='display:block;height:120px'></span></span><span style='display:block;height:80px'></span></p>");
    id = document.query("p"); const auto inner = document.query("#i");
    CHECK(close(view.scroll(inner).max_y, 90) && close(view.scroll(id).max_y, 70));
    view.scroll_at(10, 10, 0, 200); render(view);
    CHECK(close(view.scroll(inner).y, 90) && close(view.scroll(id).y, 70) && view.scroll().y == 0);
    view.scroll_to(id, 0, 0); view.scroll_to(inner, 0, 0); render(view);
    CHECK(view.scroll(inner).y == 0 && view.scroll(id).y == 0);

    page("<p style='width:80px;height:30px;overflow-x:clip;overflow-y:visible'>"
         "<span style='display:block;width:160px;height:70px;background-color:red'></span></p>");
    image = pixels(view);
    const auto partial = image;
    CHECK(xl_view_invalidate(view.get(), 0, 0, 360, 240) == XL_OK); render(view);
    image = pixels(view);
    CHECK(image == partial);
    CHECK(image[(50u * 360 + 10) * 4 + 1] == 0 && image[(50u * 360 + 90) * 4 + 1] == 255);
    CHECK(document.evaluate("document.querySelector('p').style.setProperty('overflow-x','visible')") == XL_OK); render(view);
    image = pixels(view); CHECK(image[(50u * 360 + 90) * 4 + 1] == 0);
    CHECK(document.evaluate("document.querySelector('p').style.setProperty('overflow','hidden')") == XL_OK); render(view);
    image = pixels(view); CHECK(image[(50u * 360 + 10) * 4 + 1] == 255);
    page("<p style='width:80px;height:30px;overflow-x:visible;overflow-y:hidden'>"
         "<span style='display:block;width:160px;height:70px'></span></p>");
    info = view.scroll(document.query("p")); CHECK(info.flags & XL_SCROLL_X);

    page("<p style='width:200px'><span id=a style='display:inline-block;width:60px;height:20px;overflow:hidden;background-color:green'>"
         "<span style='display:block;width:120px;height:80px;background-color:red'>A</span></span><span>B</span></p>");
    id = document.query("#a"); view.scroll_to(id, 30, 30); render(view); full_matches(id);
    page("<p style='display:flex;width:120px'><span id=a style='overflow:auto'>averyveryverylongword</span><span>B</span></p>");
    CHECK(bounds("#a").width < 120 && view.scroll(document.query("#a")).max_x > 0);
    page("<p style='display:grid;width:120px;height:30px;grid-template-columns:60px 60px;overflow:hidden'>"
         "<span id=a style='height:80px;background-color:red'>A</span><span>B</span></p>");
    id = document.query("p"); view.scroll_to(id, 0, 30); render(view); full_matches(id);
    page("<p style='display:grid;width:120px;grid-template-columns:1fr 1fr'>"
         "<span id=a style='overflow:hidden'>averyveryverylongword</span><span>B</span></p>");
    CHECK(close(bounds("#a").width, 60) && view.scroll(document.query("#a")).max_x > 0);

    page("<div style='width:700px;height:600px;background-color:red'>viewport</div>");
    info = view.scroll(); CHECK(info.max_x >= 340 && info.max_y >= 360);
    view.scroll_to(0, 200, 150); render(view);
    CHECK(close(view.scroll().x, 200) && close(bounds("div").y, -150)); full_matches(0);
    page("<div style='height:600px'>new navigation</div>");
    CHECK(view.scroll().x == 0 && view.scroll().y == 0);
#ifdef _WIN32
    options.kind = XL_VIEW_WINDOW; options.flags = XL_VIEW_HIDDEN;
    xl::View window(runtime, &options); window.document(&document); render(window); window.scroll_input(true);
    uint32_t native_kind; uintptr_t handle;
    CHECK(xl_view_native_handle(window.get(), &native_kind, &handle) == XL_OK && native_kind == XL_NATIVE_WIN32);
    const auto hwnd = reinterpret_cast<HWND>(handle);
    POINT point{20, 20}; CHECK(ClientToScreen(hwnd, &point));
    CHECK(PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(20,20)));
    CHECK(PostMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, -WHEEL_DELTA), MAKELPARAM(point.x,point.y)));
    CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK);
    CHECK(window.stats().needs_frame); render(window);
    CHECK(window.scroll().y > 0);
#endif
    std::cout << "overflow clipping, scroll, queries, retained paint passed\n";
}
