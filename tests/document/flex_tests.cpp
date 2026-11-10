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
void render(xl::View &view) {
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
struct Text {
    xl_node_id id;
    std::string name;
    xl_node_bounds bounds;
};
std::vector<Text> texts(xl::View &view) {
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    std::vector<Text> result;
    for (size_t i = 0; i < count; ++i) {
        xl_ax_node node;
        CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
        if (node.role != XL_AX_TEXT)
            continue;
        size_t size;
        auto status = xl_view_ax_name(view.get(), i, nullptr, 0, &size);
        CHECK(status == XL_OK || status == XL_BUFFER_TOO_SMALL);
        std::string name(size, '\0');
        CHECK(xl_view_ax_name(view.get(), i, name.data(), size, &size) == XL_OK);
        result.push_back({node.node, std::move(name), node.bounds});
    }
    return result;
}
std::vector<unsigned char> pixels(xl::View &view) {
    const auto stats = view.stats();
    std::vector<unsigned char> result(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), result.data(), result.size(), size_t(stats.pixel_width) * 4) ==
          XL_OK);
    return result;
}
xl_text_position position(xl_node_id id, uint32_t offset) {
    return {sizeof(xl_text_position), offset, id, XL_TEXT_DOWNSTREAM, 0};
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
        "<!doctype html><style>body,p{margin:0}p{font:20px/24px test;width:240px;display:flex}</style>";
    const auto page = [&](const std::string &html) {
        document.html(base + html);
        render(view);
    };
    const auto box = [&](const char *selector) {
        xl_node_bounds value;
        CHECK(xl_view_node_bounds(view.get(), document.query(selector), &value) == XL_OK);
        return value;
    };
    const auto full_matches = [&] {
        xl::View full(runtime, &options);
        full.document(&document);
        render(full);
        CHECK(pixels(view) == pixels(full));
    };
    page("<p><span id=a style='flex:1;height:30px;background-color:red'></span>"
         "<span id=b style='flex:2;height:30px;background-color:blue'></span></p>");
    CHECK(near(box("#a").width, 80) && near(box("#b").width, 160) && near(box("p").height, 30));
    CHECK(texts(view).empty());
    page("<p style='width:180px'><span id=a style='flex:1;min-width:100px'>A</span>"
         "<span id=b style='flex:1'>B</span><span id=c style='flex:1'>C</span></p>");
    CHECK(near(box("#a").width, 100) && near(box("#b").width, 40) && near(box("#c").x, 140));
    page("<p><span id=a style='flex:1;max-width:30px'>A</span><span id=b "
         "style='flex:1;max-width:50px'>B</span>"
         "<span id=c style='flex:1'>C</span></p>");
    CHECK(near(box("#a").width, 30) && near(box("#b").width, 50) && near(box("#c").width, 160));
    page("<p style='width:100px;gap:5px 10px;flex-wrap:wrap'><span id=a style='width:40px'>A</span>"
         "<span id=b style='width:40px'>B</span><span id=c style='width:40px'>C</span></p>");
    CHECK(near(box("#b").x, 50) && near(box("#c").y, 29) && near(box("p").height, 53));
    page("<p style='height:180px;flex-flow:column nowrap'><span id=a style='flex:1'>A</span>"
         "<span id=b style='flex:2'>B</span><span id=c style='flex:1'>C</span></p>");
    CHECK(near(box("#a").height, 45) && near(box("#b").height, 90) && near(box("#c").y, 135));
    page("<p style='justify-content:space-evenly'><span id=a style='width:30px'>A</span>"
         "<span id=b style='width:40px'>B</span><span id=c style='width:50px'>C</span></p>");
    CHECK(near(box("#a").x, 30) && near(box("#b").x, 90) && near(box("#c").x, 160));
    size_t size;
    CHECK(xl_document_get_style(document.get(), document.query("p"), "justify-content", nullptr, 0, &size) ==
          XL_BUFFER_TOO_SMALL);
    std::string value(size, '\0');
    CHECK(xl_document_get_style(document.get(), document.query("p"), "justify-content", value.data(), size,
                                &size) == XL_OK);
    CHECK(value == "space-evenly");
    CHECK(document.stats().unsupported_layout_nodes == 0);

    page("<p>AA<span id=a style='display:flex;order:-1;flex:1;background-color:red'>BB"
         "<span id=b style='display:inline-block;background-color:green'>CC</span></span>DD"
         "<span id=c style='order:2;width:40px'>EE</span></p>");
    auto nodes = texts(view);
    CHECK(nodes.size() == 5);
    CHECK(nodes[0].name == "AA" && nodes[1].name == "BB" && nodes[2].name == "CC" && nodes[3].name == "DD" &&
          nodes[4].name == "EE");
    CHECK(nodes[1].bounds.x < nodes[0].bounds.x);
    for (const auto &node : nodes) {
        const auto rects = view.text_rects(node.id, 0, 2);
        CHECK(!rects.empty());
        CHECK(near(rects[0].x, node.bounds.x) && near(rects[0].y, node.bounds.y));
        const auto hit = view.hit_text(rects[0].x + rects[0].width * .5f, rects[0].y + rects[0].height * .5f);
        CHECK(hit.node == node.id);
    }
    const auto plain = pixels(view);
    view.selection(position(nodes.front().id, 0), position(nodes.back().id, 2));
    render(view);
    CHECK(view.selected_text() == "AABBCCDDEE");
    view.selection(position(nodes.back().id, 2), position(nodes.front().id, 0));
    render(view);
    CHECK(view.selected_text() == "AABBCCDDEE");
    view.clear_selection();
    render(view);
    CHECK(pixels(view) == plain && view.render() == 0);
    full_matches();
    const auto before = view.stats().rasterized_pixels;
    CHECK(document.evaluate("document.getElementById('a').style.setProperty('order','3')") == XL_OK);
    render(view);
    full_matches();
    CHECK(view.stats().rasterized_pixels - before < 360u * 260u);
    CHECK(document.stats().unsupported_layout_nodes == 0 && view.render() == 0);

    page("<p><span id=a style='flex-grow:10;flex:1!important;flex-grow:2'>A</span>"
         "<span id=b style='flex:1'>B</span></p>");
    CHECK(near(box("#a").width, 120) && near(box("#b").width, 120));
    page("<p style='gap:10px;gap:-5px'><span id=a style='width:40px'>A</span><span id=b "
         "style='width:40px'>B</span></p>");
    CHECK(near(box("#b").x, 50));
    page("<p style='align-items:baseline'><span id=a>A</span><span id=b style='padding:5px;width:60px'>one "
         "two three</span></p>");
    nodes = texts(view);
    const auto first = view.text_rects(nodes[1].id, 0, 1);
    CHECK(std::abs(nodes[0].bounds.y - first.front().y) < 1);
    document.attribute(document.query("p"), "style",
                       "display:flex;flex-direction:column;width:70px;height:180px");
    render(view);
    full_matches();
    CHECK(view.render() == 0);
    std::cout << "Flex distribution, constraints, wrapping, baselines, cascade, ordered painting and DOM "
                 "selection passed\n";
}
