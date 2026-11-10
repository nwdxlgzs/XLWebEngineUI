#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

#define CHECK(value)                                                                                         \
    do {                                                                                                     \
        if (!(value)) {                                                                                      \
            std::cerr << __LINE__ << ": " #value "\n";                                                       \
            std::exit(1);                                                                                    \
        }                                                                                                    \
    } while (false)
namespace {
void render(xl::View& view) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags;
    xl_status status;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    CHECK(status == XL_OK);
}
bool near(float a, float b) { return std::abs(a - b) < .1f; }
std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats = view.stats();
    std::vector<unsigned char> result(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), result.data(), result.size(), size_t(stats.pixel_width) * 4) ==
          XL_OK);
    return result;
}
std::vector<xl_ax_node> texts(xl::View& view) {
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    std::vector<xl_ax_node> result;
    for (size_t i = 0; i < count; ++i) {
        xl_ax_node node;
        CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
        if (node.role == XL_AX_TEXT)
            result.push_back(node);
    }
    return result;
}
xl_text_position position(xl_node_id node, uint32_t offset) {
    return {sizeof(xl_text_position), offset, node, XL_TEXT_DOWNSTREAM, 0};
}
} // namespace
int main() {
    xl::Runtime runtime;
    xl::Document document(runtime);
    std::ifstream input(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN;
    options.width = 360;
    options.height = 260;
    xl::View view(runtime, &options);
    view.document(&document);
    const std::string base =
        "<!doctype html><style>body,p{margin:0}p{font:20px/24px test;width:240px}</style>";
    const auto page = [&](const std::string& html) {
        document.html(base + html);
        render(view);
    };
    const auto box = [&](const char* selector) {
        xl_node_bounds value;
        CHECK(xl_view_node_bounds(view.get(), document.query(selector), &value) == XL_OK);
        return value;
    };
    const auto full_matches = [&] {
        xl::View full(runtime, &options);
        full.document(&document);
        render(full);
        xl_text_selection selection;
        if (xl_view_get_selection(view.get(), &selection, sizeof(selection)) == XL_OK) {
            full.selection(selection.anchor, selection.focus);
            render(full);
        }
        CHECK(pixels(view) == pixels(full));
    };
    page("<p style='width:auto;max-width:100px;margin-left:auto;margin-right:auto'>A</p>");
    CHECK(near(box("p").x, 130) && near(box("p").width, 100));
    page("<p style='width:20px;min-width:100px;max-width:50px;padding:5px;border:2px solid "
         "red;box-sizing:border-box'>A</p>");
    CHECK(near(box("p").width, 100));
    page("<p style='height:200px;max-height:100px'><span id=a style='display:block;height:50%'>"
         "<span id=b style='display:block;height:inherit'>B</span></span></p>");
    CHECK(near(box("p").height, 100) && near(box("#a").height, 50) && near(box("#b").height, 25));
    page("<p style='min-height:120px'><span id=a style='display:block;height:50%'>A</span></p>");
    CHECK(near(box("p").height, 120) && near(box("#a").height, 24));
    page("<style>html,body{height:100%}</style><p style='height:50%'>A</p>");
    CHECK(near(box("p").height, 130));
    page("<p><span id=a style='display:block;height:50%;max-height:50%'>A</span></p>");
    CHECK(near(box("p").height, 24) && near(box("#a").height, 24));
    page("<p style='width:50%'><span id=a style='display:block;width:inherit'><span id=b "
         "style='display:block;width:inherit'>B</span></span></p>");
    CHECK(near(box("p").width, 180) && near(box("#a").width, 90) && near(box("#b").width, 45));
    page(
        "<p><span id=a style='display:inline-block;min-width:100px;max-width:50px;padding:3px'>A</span><span "
        "id=b>B</span></p>");
    CHECK(near(box("#a").width, 106) && near(box("#b").x, 106));
    page("<p style='display:flex;min-height:100px'><span id=a style='height:50%'>A</span><span "
         "id=b>B</span></p>");
    CHECK(near(box("#a").height, 24) && near(box("#b").height, 100));
    page("<p style='display:flex;flex-direction:column;min-height:100px'><span id=a "
         "style='flex:1'>A<br>A<br>A</span><span id=b style='flex:1'>B</span></p>");
    CHECK(near(box("#a").height, 74) && near(box("#b").height, 26));
    page("<p style='display:grid;min-height:100px;grid-template-columns:1fr 1fr'><span id=a "
         "style='height:50%'>A</span><span id=b>B</span></p>");
    CHECK(near(box("#a").height, 50) && near(box("#b").height, 100));
    page("<p style='width:min-content;padding:5px;border:2px solid red;box-sizing:border-box'>one two "
         "three</p>");
    CHECK(box("p").width > 50 && box("p").width < 70 && near(box("p").height, 86));
    page("<p style='width:fit-content'>one two</p>");
    size_t required;
    CHECK(xl_document_get_style(document.get(), document.query("p"), "width", nullptr, 0, &required) ==
          XL_BUFFER_TOO_SMALL);
    std::string value(required, '\0');
    CHECK(xl_document_get_style(document.get(), document.query("p"), "width", value.data(), value.size(),
                                &required) == XL_OK &&
          value == "fit-content");

    const std::string original = "one two three four five six";
    page("<p><span id=a style='display:block;max-width:180px;padding:3px;border:2px solid red'>" + original +
         "</span></p>");
    auto nodes = texts(view);
    CHECK(nodes.size() == 1);
    view.selection(position(nodes[0].node, 0), position(nodes[0].node, uint32_t(original.size())));
    render(view);
    CHECK(view.selected_text() == original);
    const auto rasterized = view.stats().rasterized_pixels;
    CHECK(document.evaluate("document.getElementById('a').style.setProperty('max-width','65px')") == XL_OK);
    render(view);
    CHECK(near(box("#a").width, 75));
    CHECK(view.selected_text() == original && view.stats().rasterized_pixels - rasterized < 360u * 260u);
    const auto rects = view.text_rects(nodes[0].node, 0, uint32_t(original.size()));
    CHECK(rects.size() >= 4);
    for (const auto& rect : rects)
        CHECK(view.hit_text(rect.x + rect.width * .5f, rect.y + rect.height * .5f).node == nodes[0].node);
    nodes = texts(view);
    CHECK(nodes.size() == 1 && near(nodes[0].bounds.y, rects.front().y));
    full_matches();
    view.clear_selection();
    render(view);
    full_matches();
    const auto frames = document.stats().layout_frames;
    CHECK(view.render() == 0 && document.stats().layout_frames == frames);
    CHECK(document.stats().unsupported_layout_nodes == 0);
    std::cout << "Unified constraints, definite percentages, inheritance, intrinsic sizes, AX and retained "
                 "pixels passed\n";
}
