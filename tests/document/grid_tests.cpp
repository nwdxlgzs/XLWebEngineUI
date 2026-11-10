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
        const auto status = xl_view_ax_name(view.get(), i, nullptr, 0, &size);
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
        "<!doctype html><style>body,p{margin:0}p{font:20px/24px test;width:240px;display:grid}</style>";
    const auto page = [&](const std::string &html) {
        document.html(base + html);
        render(view);
    };
    const auto box = [&](const char *selector) {
        xl_node_bounds bounds;
        CHECK(xl_view_node_bounds(view.get(), document.query(selector), &bounds) == XL_OK);
        return bounds;
    };
    const auto full_matches = [&] {
        xl::View full(runtime, &options);
        full.document(&document);
        render(full);
        CHECK(pixels(view) == pixels(full));
    };
    page("<p style='grid-template-columns:1fr 2fr;grid-auto-rows:30px;gap:6px'>"
         "<span id=a style='background-color:red'></span><span id=b "
         "style='background-color:blue'></span></p>");
    CHECK(near(box("#a").width, 78) && near(box("#b").width, 156) && near(box("p").height, 30) &&
          texts(view).empty());
    page("<p style='grid-template-columns:repeat(3,1fr);grid-auto-flow:dense;gap:5px'>"
         "<span id=a style='grid-column:span 2'>A</span><span id=b style='grid-column:span 2'>B</span><span "
         "id=c>C</span></p>");
    CHECK(near(box("#c").y, 0) && near(box("#b").y, 29) && near(box("#a").width, (240.0f - 10) / 3 * 2 + 5));
    page("<p style='grid-template:\"head head\" 40px \"side main\" auto / 80px 1fr;gap:5px'>"
         "<span id=a style='grid-area:head'>A</span><span id=b style='grid-area:side'>B</span><span id=c "
         "style='grid-area:main'>C</span></p>");
    CHECK(near(box("#a").width, 240) && near(box("#a").height, 40) && near(box("#b").y, 45) &&
          near(box("#c").x, 85));
    page("<p style='grid-template-columns:repeat(auto-fit,minmax(50px,1fr));gap:10px'><span "
         "id=a>A</span></p>");
    CHECK(near(box("#a").width, 240));
    page("<p style='grid-template-columns:repeat(auto-fill,minmax(50px,1fr));gap:10px'><span "
         "id=a>A</span></p>");
    CHECK(near(box("#a").width, 52.5f));
    page(
        "<p style='grid-template-columns:[start] 50px [middle] 80px [finish];grid-auto-columns:30px;gap:5px'>"
        "<span id=a style='grid-column:middle / finish'>A</span><span id=b style='grid-column:start / "
        "middle'>B</span>"
        "<span id=c style='grid-column:4'>C</span></p>");
    CHECK(near(box("#a").x, 55) && near(box("#b").x, 0) && near(box("#c").width, 30));
    page("<p style='width:60px;grid-template-columns:1fr 1fr'><span id=a style='grid-column:span "
         "2'>verylongword</span><span>B</span><span>C</span></p>");
    CHECK(near(box("#a").width, 60) && near(box("p").height, 48));
    page("<p style='grid-template-columns:1fr 1fr;grid-template-rows:50% "
         "25%'><span>A</span><span>B</span><span id=c>C</span></p>");
    CHECK(near(box("p").height, 48) && near(box("#c").height, 12));

    page("<p style='grid-template-columns:1fr 1fr;gap:5px'>AA<span id=a style='order:-1;display:flex'>BB"
         "<span id=b style='display:inline-block'>CC</span></span>DD<span id=c>EE</span></p>");
    auto nodes = texts(view);
    CHECK(nodes.size() == 5 && nodes[0].name == "AA" && nodes[1].name == "BB" && nodes[2].name == "CC" &&
          nodes[3].name == "DD" && nodes[4].name == "EE");
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
    const auto rasterized = view.stats().rasterized_pixels;
    CHECK(document.evaluate(
              "document.querySelector('p').style.setProperty('grid-template-columns','40px 80px 1fr')") ==
          XL_OK);
    render(view);
    full_matches();
    CHECK(view.stats().rasterized_pixels - rasterized < 360u * 260u && view.render() == 0);
    CHECK(document.stats().unsupported_layout_nodes == 0);

    page("<p style='grid-template:40px / 40px'><span id=a style='grid-area:1 / "
         "1;background-color:red'>A</span>"
         "<span id=b style='grid-area:1 / 1;order:-1;background-color:blue'>B</span></p>");
    auto image = pixels(view);
    const auto corner = (2u * 360u + 2u) * 4;
    CHECK(image[corner] == 255 && image[corner + 2] == 0);
    nodes = texts(view);
    CHECK(view.hit_text(5, 10).node == nodes[0].id);
    CHECK(document.evaluate("document.getElementById('a').style.setProperty('order','-2')") == XL_OK);
    render(view);
    full_matches();
    image = pixels(view);
    CHECK(image[corner] == 0 && image[corner + 2] == 255);
    CHECK(view.hit_text(5, 10).node == nodes[1].id);
    page("<p style='grid-template-columns:40px 1fr;grid-template-columns:1fr -5px;gap:5px'>"
         "<span id=a style='grid-column:2!important;grid-area:1 / 1'>A</span><span id=b>B</span></p>");
    CHECK(near(box("#a").x, 45) && near(box("#b").x, 0));
    std::cout << "Grid tracks, placement, repeat, intrinsic content, DOM selection, AX and retained pixels "
                 "passed\n";
}
