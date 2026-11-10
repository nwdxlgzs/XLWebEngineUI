#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
void render(xl::View& view) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    xl_status status;
    uint32_t flags;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    CHECK(status == XL_OK);
}
xl_node_bounds bounds(xl::View& view, xl_node_id node) {
    xl_node_bounds result;
    CHECK(xl_view_node_bounds(view.get(), node, &result) == XL_OK);
    return result;
}
std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats = view.stats();
    std::vector<unsigned char> result(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), result.data(), result.size(), size_t(stats.pixel_width) * 4) == XL_OK);
    return result;
}
bool near(float a, float b) { return std::abs(a - b) < .1f; }
struct Text { xl_node_id node, parent; std::string name; xl_node_bounds bounds; };
std::vector<Text> texts(xl::View& view) {
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    std::vector<Text> result;
    for (size_t i = 0; i < count; ++i) {
        xl_ax_node node;
        CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
        if (node.role != XL_AX_TEXT) continue;
        size_t size;
        const auto status = xl_view_ax_name(view.get(), i, nullptr, 0, &size);
        CHECK(status == XL_OK || status == XL_BUFFER_TOO_SMALL);
        std::string name(size, '\0');
        CHECK(xl_view_ax_name(view.get(), i, name.data(), size, &size) == XL_OK);
        result.push_back({node.node, node.parent, std::move(name), node.bounds});
    }
    return result;
}
xl_text_position position(xl_node_id node, uint32_t offset) {
    return {sizeof(xl_text_position), offset, node, XL_TEXT_DOWNSTREAM, 0};
}
}
int main() {
    xl::Runtime runtime;
    xl::Document document(runtime);
    std::ifstream input(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN;
    options.width = 360; options.height = 260;
    xl::View view(runtime, &options);
    view.document(&document);
    const std::string base = "<!doctype html><style>body,p{margin:0}p{font:20px/24px test;width:300px}</style>";
    const auto page = [&](const std::string& html) { document.html(base + html); render(view); };
    const auto box = [&](const char* selector) { return bounds(view, document.query(selector)); };
    const auto full_matches = [&] {
        xl::View full(runtime, &options);
        full.document(&document); render(full);
        CHECK(pixels(view) == pixels(full));
    };

    // Fresh Runtime with only an empty atomic box still prepares the line strut.
    page("<p><span id=a style='display:inline-block;width:20px;height:30px'></span></p>");
    CHECK(near(box("#a").width, 20) && near(box("#a").height, 30) && box("p").height >= 30);
    CHECK(texts(view).empty() && document.stats().unsupported_layout_nodes == 0);
    page("<p><span id=a>A</span><span id=b style='display:inline-block;width:50px;padding:4px;border:2px solid red'>"
         "B<br><span id=c>C</span></span><span id=d>D</span></p>");
    CHECK(near(box("#b").width, 62) && near(box("#b").height, 60));
    CHECK(std::abs(box("#a").y - box("#c").y) < 1 && near(box("#a").y, box("#d").y));
    CHECK(near(box("#b").x + box("#b").width, box("#d").x));
    page("<p style='width:40px'><span id=a style='display:inline-block'>verylongword</span><span id=b>X</span></p>");
    CHECK(box("#a").width > 100 && near(box("p").height, 48) && box("#b").y > 23);
    page("<p style='width:90px'><span id=a style='display:inline-block;padding:2px'>one two three four five</span></p>");
    CHECK(near(box("#a").width, 90) && box("#a").height > 48);
    page("<p><span id=a style='display:inline flow-root;width:60px;height:40px;box-sizing:border-box;"
         "padding:3px;border:2px solid red'>A</span></p>");
    CHECK(near(box("#a").width, 60) && near(box("#a").height, 40));

    page("<p>A<span id=b style='display:inline-block;padding:3px;background-color:#ff0000'>B"
         "<span id=c style='display:inline-block;padding:2px;border:1px solid blue;background-color:#00ff00'>"
         "C</span>D</span>E</p>");
    auto nodes = texts(view);
    CHECK(nodes.size() == 5);
    CHECK(nodes[0].name == "A" && nodes[1].name == "B" && nodes[2].name == "C" && nodes[3].name == "D" && nodes[4].name == "E");
    CHECK(nodes[1].parent == document.query("#b") && nodes[2].parent == document.query("#c"));
    CHECK(box("#c").width > 18 && box("#b").width > 48);
    for (const auto& node : nodes) {
        const auto rects = view.text_rects(node.node, 0, 1);
        CHECK(rects.size() == 1 && near(node.bounds.x, rects[0].x) && near(node.bounds.y, rects[0].y));
        const auto hit = view.hit_text(rects[0].x + rects[0].width * .5f, rects[0].y + rects[0].height * .5f);
        CHECK(hit.node == node.node);
    }
    const auto plain = pixels(view);
    const auto rasterized = view.stats().rasterized_pixels;
    view.selection(position(nodes[0].node, 0), position(nodes[4].node, 1)); render(view);
    CHECK(view.selected_text() == "ABCDE" && !view.selection_rects().empty());
    CHECK(view.stats().rasterized_pixels - rasterized < 360u * 260u);
    view.selection(position(nodes[4].node, 1), position(nodes[0].node, 0)); render(view);
    CHECK(view.selected_text() == "ABCDE");
    view.selection(position(nodes[1].node, 0), position(nodes[3].node, 1)); render(view);
    CHECK(view.selected_text() == "BCD");
    view.clear_selection(); render(view);
    CHECK(pixels(view) == plain && view.render() == 0);
    full_matches();

    // Overlapping atomic boxes paint between preceding and following glyphs.
    page("<p><span style='color:#00ff00'>M</span><span id=a style='display:inline-block;width:30px;"
         "height:30px;vertical-align:top;margin-left:-20px;background-color:#ff0000'></span>"
         "<span style='margin-left:-20px;color:#0000ff'>M</span></p>");
    const auto image = pixels(view);
    size_t red = 0, blue = 0, green = 0;
    for (size_t i = 0; i < image.size(); i += 4) {
        if (image[i] > 200 && image[i + 1] < 30 && image[i + 2] < 30) ++red;
        if (image[i] < 30 && image[i + 1] < 30 && image[i + 2] > 200) ++blue;
        if (image[i] < 30 && image[i + 1] > 200 && image[i + 2] < 30) ++green;
    }
    CHECK(red > 100 && blue > 10 && green == 0);
    page("<p><span style='display:inline-block;width:30px;height:30px;vertical-align:top;background-color:red'></span>"
         "<span style='margin-left:-30px;padding:0 15px;background-color:blue;color:blue'>M</span></p>");
    const auto backgrounds = pixels(view);
    const auto at = (5u * 360u + 5u) * 4;
    CHECK(backgrounds[at] == 0 && backgrounds[at + 1] == 0 && backgrounds[at + 2] == 255);

    page("<p><span id=a>A</span><span id=b style='vertical-align:top'>B"
         "<span id=c style='display:inline-block;vertical-align:bottom;padding:4px'>C<br>D</span></span>E</p>");
    CHECK(near(box("p").height, 56) && near(box("#c").height, 56) && box("#c").width > 20);
    CHECK(document.stats().unsupported_layout_nodes == 0);
    const auto old = view.stats().rasterized_pixels;
    CHECK(document.evaluate("document.getElementById('c').style.cssText='display:inline-block;width:80px;"
                            "padding:8px;background-color:blue;vertical-align:middle'") == XL_OK);
    render(view);
    CHECK(near(box("#c").width, 96) && view.stats().rasterized_pixels - old < 360u * 260u);
    full_matches();
    CHECK(view.render() == 0);
    document.attribute(document.query("#c"), "style", "display:inline-block;width:20px;vertical-align:50%");
    render(view); full_matches();
    std::cout << "Atomic layout, intrinsic sizing, baselines, nested painting, DOM selection, hits and AX passed\n";
}
