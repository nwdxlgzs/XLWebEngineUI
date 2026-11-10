#include <XLWebEngineUI/xl.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <thread>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
struct TextNode { xl_node_id node, parent; std::string name; };
std::vector<TextNode> texts(xl::View& view) {
    size_t count = 0;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    std::vector<TextNode> result;
    for (size_t index = 0; index < count; ++index) {
        xl_ax_node node;
        CHECK(xl_view_ax_get(view.get(), index, &node, sizeof(node)) == XL_OK);
        if (node.role != XL_AX_TEXT) continue;
        size_t size = 0;
        auto status = xl_view_ax_name(view.get(), index, nullptr, 0, &size);
        CHECK(status == XL_OK || status == XL_BUFFER_TOO_SMALL);
        std::string name(size, '\0');
        CHECK(xl_view_ax_name(view.get(), index, name.data(), name.size(), &size) == XL_OK);
        result.push_back({node.node, node.parent, std::move(name)});
    }
    return result;
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
bool same(const xl_node_bounds& a, const xl_node_bounds& b) {
    return std::abs(a.x-b.x) < .01f && std::abs(a.y-b.y) < .01f && std::abs(a.width-b.width) < .01f && std::abs(a.height-b.height) < .01f;
}
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
void font(xl::Document& document, const std::string& path, const char* name) {
    std::ifstream input(path, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!bytes.empty());
    CHECK(xl_document_add_font(document.get(), name, bytes.data(), bytes.size()) == XL_OK);
}
}
int main() {
    xl::Runtime runtime;
    xl::Document document(runtime);
    font(document, XL_TEST_FONT, "test");
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 320; options.height = 240;
    xl::View view(runtime, &options);
    view.document(&document);
    const std::string base = "<style>body,p,div {margin:0} p {width:260px;font-size:20px}</style>";
    const auto page = [&](const std::string& content) { document.html(base + content); render(view); };

    page("<p><br></p>"); // Fresh Runtime: forced lines must not remain busy forever.
    const auto empty_line = bounds(view, document.query("p")).height;
    CHECK(empty_line > 0 && texts(view).empty());
    page("<p>A</p>");
    const auto line = bounds(view, document.query("p")).height;
    CHECK(empty_line == line);
    page("<p>A<br></p>");
    CHECK(bounds(view, document.query("p")).height == line);
    page("<p>A<br><br></p>");
    CHECK(bounds(view, document.query("p")).height == line * 2);

    // Equivalent inline fragmentation must preserve shaping, ligatures, accents
    // and line wrapping, including a combining mark in a separate DOM node.
    page("<p>office e\xcc\x81 and repeated words that wrap onto another line.</p>");
    const auto unfragmented = pixels(view);
    page("<p>of<span>f</span>ice <span>e</span><span>\xcc\x81</span> and repeated <span>words that wrap onto another</span> line.</p>");
    CHECK(pixels(view) == unfragmented);
    CHECK(document.stats().unsupported_layout_nodes == 0);
    const auto frames = document.stats().layout_frames;
    CHECK(view.render() == 0 && document.stats().layout_frames == frames);

    page("<p>Inherited styles</p>");
    const auto inherited = pixels(view);
    page("<p><b style='font-weight:inherit'><i style='font-style:normal'>Inherited styles</i></b></p>");
    CHECK(pixels(view) == inherited);
    page("<p><span style='background-color:#00ccff'>AA<span style='background-color:#ffff00'>BB</span>CC</span></p>");
    const auto decorated = pixels(view);
    size_t cyan = 0, yellow = 0;
    for (size_t i = 0; i < decorated.size(); i += 4) {
        if (decorated[i] == 0 && decorated[i+1] == 204 && decorated[i+2] == 255) ++cyan;
        if (decorated[i] == 255 && decorated[i+1] == 255 && decorated[i+2] == 0) ++yellow;
    }
    CHECK(cyan > 10 && yellow > 10);

    page("<p><span id=a>  A \t </span><span id=b> \n  B  </span></p>");
    const auto nodes = texts(view);
    CHECK(nodes.size() == 2 && nodes[0].name == "A " && nodes[1].name == "B");
    const auto a = nodes[0].node, b = nodes[1].node;
    CHECK(nodes[0].parent == document.query("#a") && nodes[1].parent == document.query("#b"));
    CHECK(view.text_rects(a, 0, 2).empty());
    const auto letter_a = view.text_rects(a, 2, 3);
    const auto letter_b = view.text_rects(b, 4, 5);
    CHECK(letter_a.size() == 1 && letter_b.size() == 1);
    CHECK(same(letter_a[0], view.text_rects(a, 0, 1, false)[0]));
    CHECK(same(letter_b[0], view.text_rects(b, 0, 1, false)[0]));
    CHECK(view.text_rects(b, 5, 7).empty());
    CHECK(std::abs(letter_a[0].y - letter_b[0].y) < .01f && letter_b[0].x > letter_a[0].x + letter_a[0].width);
    auto hit = view.hit_text(letter_a[0].x + letter_a[0].width * .1f, letter_a[0].y + letter_a[0].height * .5f);
    CHECK(hit.node == a && hit.offset_utf16 == 2);
    hit = view.hit_text(letter_b[0].x + letter_b[0].width * .1f, letter_b[0].y + letter_b[0].height * .5f);
    CHECK(hit.node == b && hit.offset_utf16 == 4);
    char original[16]{}; size_t required = 0;
    CHECK(xl_document_get_text(document.get(), a, original, sizeof(original), &required) == XL_OK);
    CHECK(std::string(original, required) == "  A \t ");
    xl_node_bounds sentinel{1,2,3,4};
    CHECK(xl_view_dom_text_rects(view.get(), b, 0, 7, &sentinel, 0, &required) == XL_BUFFER_TOO_SMALL);
    CHECK(sentinel.x == 1 && sentinel.height == 4);
    CHECK(xl_view_dom_text_rects(view.get(), b, 0, 8, &sentinel, 1, &required) == XL_INVALID_ARGUMENT);
    xl_text_position untouched{111,222,333,444,555};
    CHECK(xl_view_hit_test_text(view.get(), 0, 0, &untouched, 1) == XL_ABI_MISMATCH && untouched.node == 333);
    CHECK(xl_view_hit_test_text(view.get(), std::numeric_limits<float>::quiet_NaN(), 0, &untouched, sizeof(untouched)) == XL_INVALID_ARGUMENT);
    std::thread wrong([&] { CHECK(xl_view_hit_test_text(view.get(), 0, 0, &untouched, sizeof(untouched)) == XL_WRONG_THREAD); });
    wrong.join();

    page("<p><span id=red style='color:#ff0000'>Red</span> <b id=bold>bold</b> <span id=big style='font-size:30px;color:#0000ff'>Blue</span></p>");
    const auto red = bounds(view, document.query("#red")), big = bounds(view, document.query("#big"));
    CHECK(big.height > red.height && big.x > red.x + red.width);
    auto image = pixels(view);
    size_t red_ink = 0, blue_ink = 0;
    for (size_t i = 0; i < image.size(); i += 4) {
        if (image[i] > 160 && image[i+1] < 80 && image[i+2] < 80) ++red_ink;
        if (image[i] < 80 && image[i+1] < 80 && image[i+2] > 160) ++blue_ink;
    }
    CHECK(red_ink > 10 && blue_ink > 10);
    document.attribute(document.query("#red"), "style", "color:#008000");
    CHECK(xl_view_hit_test_text(view.get(), 0, 0, &untouched, sizeof(untouched)) == XL_BUSY && untouched.node == 333);
    const auto before = view.stats().rasterized_pixels;
    render(view);
    CHECK(view.stats().rasterized_pixels - before < 320u * 240u);
    {
        xl::View full(runtime, &options); full.document(&document); render(full);
        CHECK(pixels(view) == pixels(full));
    }

    page("<p><span id=outer>Before <span style='display:block' id=block>Block</span> After</span></p>");
    const auto mixed = texts(view);
    CHECK(mixed.size() == 3);
    CHECK(bounds(view, mixed[0].node).y < bounds(view, mixed[1].node).y);
    CHECK(bounds(view, mixed[1].node).y < bounds(view, mixed[2].node).y);
    CHECK(bounds(view, document.query("#outer")).height >= bounds(view, document.query("#block")).height);
    page("<p>Before<br><span id=after>After</span></p>");
    const auto broken = texts(view);
    CHECK(broken.size() == 2 && bounds(view, broken[1].node).y > bounds(view, broken[0].node).y);

    page("<p style='width:100px'><span id=wrap>one two three four five six seven</span></p>");
    const auto wrapped = texts(view);
    const auto ranges = view.text_rects(wrapped[0].node, 0, 33);
    CHECK(ranges.size() > 1 && ranges.back().y > ranges.front().y);
    CHECK(bounds(view, document.query("#wrap")).height > ranges.front().height);

    // Default RTL alignment uses the same paragraph positions as source lookup.
    page("<p dir=rtl><span id=rtl>ABC</span></p>");
    CHECK(bounds(view, document.query("#rtl")).x > 180);
    page("<p style='text-align:center'><span id=center>ABC</span></p>");
    const auto centered = bounds(view, document.query("#center"));
    CHECK(centered.x > 60 && centered.x + centered.width < 200);

    // An uncovered scalar still has a missing-glyph box and original AX text.
    page("<p><span id=missing>A&#x10fffd;B</span></p>");
    const auto missing = texts(view);
    CHECK(missing.size() == 1 && missing[0].name == "A\xf4\x8f\xbf\xbd" "B");
    const auto missing_rects = view.text_rects(missing[0].node, 1, 3);
    CHECK(!missing_rects.empty() && missing_rects[0].width > 0);

    const std::string font_path = XL_TEST_FONT;
    font(document, font_path.substr(0, font_path.find_last_of("/\\") + 1) + "colr.ttf", "symbols");
    page("<p><span id=emoji>  &#x1f600;X </span></p>");
    const auto emoji = texts(view);
    CHECK(emoji.size() == 1);
    const auto symbol = view.text_rects(emoji[0].node, 2, 4);
    CHECK(!symbol.empty() && symbol[0].width > 0);
    CHECK(same(symbol[0], view.text_rects(emoji[0].node, 0, 2, false)[0]));
    hit = view.hit_text(symbol[0].x + symbol[0].width * .1f, symbol[0].y + symbol[0].height * .5f);
    CHECK(hit.node == emoji[0].node && hit.offset_utf16 == 2);
    const auto x = view.text_rects(emoji[0].node, 4, 5);
    CHECK(!x.empty() && x[0].x >= symbol[0].x + symbol[0].width - .1f);

    page("<div></div>");
    CHECK(xl_view_hit_test_text(view.get(), 0, 0, &untouched, sizeof(untouched)) == XL_NOT_FOUND && untouched.node == 333);
    std::cout << "Cross-node shaping/wrapping, styles, DOM UTF-16 ranges, hit testing, AX and retained pixels passed\n";
}
