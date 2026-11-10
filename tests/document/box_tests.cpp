#include <XLWebEngineUI/xl.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value)) {                                                                                                \
            std::cerr << __LINE__ << ": " #value "\n";                                                                 \
            std::exit(1);                                                                                              \
        }                                                                                                              \
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
struct Text {
    xl_node_id id, parent;
    std::string name;
};
std::vector<Text> texts(xl::View& view) {
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    std::vector<Text> result;
    for (size_t i = 0; i < count; ++i) {
        xl_ax_node node;
        CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
        if (node.role != XL_AX_TEXT) continue;
        size_t size;
        auto status = xl_view_ax_name(view.get(), i, nullptr, 0, &size);
        CHECK(status == XL_OK || status == XL_BUFFER_TOO_SMALL);
        std::string name(size, '\0');
        CHECK(xl_view_ax_name(view.get(), i, name.data(), name.size(), &size) == XL_OK);
        result.push_back({node.node, node.parent, std::move(name)});
    }
    return result;
}
bool near(float a, float b) {
    return std::abs(a - b) < .1f;
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
    options.height = 240;
    xl::View view(runtime, &options);
    view.document(&document);
    const std::string base = "<!doctype html><style>body,p,div{margin:0}p{font:20px/24px test;width:300px}</style>";
    const auto page = [&](const std::string& content) {
        document.html(base + content);
        render(view);
    };
    const auto box = [&](const char* selector) { return bounds(view, document.query(selector)); };
    const auto equivalent = [&](const std::string& first, const std::string& second) {
        page(first);
        const auto image = pixels(view);
        page(second);
        if (image != pixels(view)) {
            std::cerr << "Unequal pages:\n" << first << '\n' << second << '\n';
            std::exit(1);
        }
    };

    page("<p><span id=a style='margin:90px 7px;padding:8px 5px;border:2px solid red'>AB</span><span id=b>C</span></p>");
    const auto a = box("#a"), b = box("#b");
    CHECK(near(a.x, 7) && near(b.x, a.x + a.width + 7));
    CHECK(near(box("p").height, 24) && a.height > 40); // Vertical decoration bleeds without increasing line height.
    CHECK(document.stats().unsupported_layout_nodes == 0);
    auto nodes = texts(view);
    CHECK(nodes.size() == 2 && nodes[0].name == "AB" && nodes[1].name == "C");
    auto ranges = view.text_rects(nodes[0].id, 0, 2);
    CHECK(ranges.size() == 1 && near(ranges[0].x, a.x + 7));
    auto hit = view.hit_text(ranges[0].x + ranges[0].width * .1f, ranges[0].y + ranges[0].height * .5f);
    CHECK(hit.node == nodes[0].id && hit.offset_utf16 == 0);
    CHECK(view.text_rects(nodes[0].id, 0, 2, false).size() == 1);
    equivalent("<p>office</p>", "<p>o<span style='padding-top:5px;padding-bottom:8px'>ff</span>ice</p>");

    page("<p>A<span id=a style='padding:4px 9px;border:2px solid blue'></span><span id=b>B</span></p>");
    CHECK(near(box("#a").width, 22) && near(box("#b").x, box("#a").x + 22) && near(box("p").height, 24));
    CHECK(texts(view).size() == 2);
    page("<p><span id=a style='padding:2px 5px'>A<br></span></p>");
    CHECK(near(box("p").height, 24) && box("#a").width > 23 && box("#a").width < 24);
    page("<p><span id=a style='padding:4px 9px;border:2px solid blue'></span></p>");
    if (!near(box("#a").width, 22) || !near(box("p").height, 24) || !texts(view).empty())
        std::cerr << "Empty inline width=" << box("#a").width << " line height=" << box("p").height
                  << " AX texts=" << texts(view).size() << '\n';
    CHECK(near(box("#a").width, 22) && near(box("p").height, 24) && texts(view).empty());

    page("<p>A<span id=a style='margin-left:-6px;margin-right:-4px;padding:2px'>B</span><span id=b>C</span></p>");
    nodes = texts(view);
    CHECK(nodes.size() == 3);
    const auto prefix = bounds(view, nodes[0].id);
    CHECK(near(box("#a").x, prefix.width - 6) && near(box("#b").x, box("#a").x + box("#a").width - 4));

    page("<p style='width:70px'>avery<span id=a style='padding:2px 9px'>long</span>word</p>");
    CHECK(near(box("p").height, 24) && box("#a").x + box("#a").width > 70);
    page("<p style='width:80px'><span id=a style='padding:3px 9px;border-left:1px solid red'>one two three four "
         "five</span></p>");
    CHECK(near(box("p").height, 120));
    nodes = texts(view);
    ranges = view.text_rects(nodes[0].id, 0, 23);
    CHECK(ranges.size() == 5);
    CHECK(ranges.back().x < ranges.front().x && near(ranges.back().y - ranges.front().y, 96));

    page("<p>A<span id=a style='padding:2px 8px'> B </span></p>");
    nodes = texts(view);
    CHECK(nodes.size() == 2 && nodes[0].name == "A" && nodes[1].name == " B");
    CHECK(view.text_rects(nodes[1].id, 2, 3).empty()); // Trimming crosses the closing edge.
    page("<p><span style='padding:2px 8px'>A </span><span style='padding:2px 8px'> B</span></p>");
    nodes = texts(view);
    CHECK(nodes.size() == 2 && nodes[0].name == "A " && nodes[1].name == "B");

    // AST expansion must preserve cascade order, importance, validity and resets.
    equivalent("<p style='border:2px solid red'>A</p>",
               "<p style='border-width:2px;border-style:solid;border-color:red'>A</p>");
    equivalent("<p style='border:2px solid red;border-width:1px 2px 3px 4px'>A</p>",
               "<p style='border-top:1px solid red;border-right:2px solid red;border-bottom:3px solid "
               "red;border-left:4px solid red'>A</p>");
    equivalent("<p style='border:4px solid red;border-left:2px double blue'>A</p>",
               "<p style='border-width:4px 4px 4px 2px;border-style:solid solid solid double;border-color:red red red "
               "blue'>A</p>");
    equivalent("<p style='border:2px solid red!important;border-left-width:8px'>A</p>",
               "<p style='border:2px solid red'>A</p>");
    equivalent("<p style='border-left:8px solid blue!important;border:2px solid red'>A</p>",
               "<p style='border:2px solid red;border-left:8px solid blue'>A</p>");
    equivalent(
        "<p style='border:2px solid red;border-left-width:-2px;border-style:nonsense;border-color:badcolor'>A</p>",
        "<p style='border:2px solid red'>A</p>");
    equivalent("<p style='border:4px dashed blue;border:solid'>A</p>",
               "<p style='border:3px solid currentcolor'>A</p>");
    equivalent("<div style='border:2px solid red'><p style='border:inherit'>A</p></div>",
               "<div style='border:2px solid red'><p style='border:2px solid red'>A</p></div>");
    equivalent("<p style='border:4px solid blue;border:initial'>A</p>", "<p>A</p>");
    equivalent("<p style='border:4px solid blue;border:unset'>A</p>", "<p>A</p>");
    equivalent("<div style='border:5px none red'><p style='border-width:inherit;border-style:solid'>A</p></div>",
               "<div><p>A</p></div>");
    equivalent("<p style='border:2px solid rgba(255, 0, 0, .5)'>A</p>", "<p style='border:2px solid #ff000080'>A</p>");
    equivalent("<style>#a{border:2px solid red}p{border-left-width:9px}</style><p id=a>A</p>",
               "<p style='border:2px solid red'>A</p>");
    equivalent("<style>p{border:2px solid red}</style><style>p{border-left-width:9px}</style><p>A</p>",
               "<p style='border:2px solid red;border-left-width:9px'>A</p>");
    page("<p style='width:100px;box-sizing:border-box;padding:6px;border:4px solid red'>A</p>");
    CHECK(near(box("p").width, 100) && near(box("p").height, 44));
    page("<div id=host><span id=detached style='border:2px solid red;padding:3px'>A</span></div>");
    const auto detached = document.query("#detached"), host = document.query("#host");
    document.remove(detached);
    document.attribute(detached, "style", "border-left:4px double blue;border-right:3px solid red;padding:2px");
    size_t css_size = 0;
    CHECK(xl_document_get_style(document.get(), detached, "border-left-width", nullptr, 0, &css_size) ==
          XL_BUFFER_TOO_SMALL);
    std::string css(css_size, '\0');
    CHECK(xl_document_get_style(document.get(), detached, "border-left-width", css.data(), css.size(), &css_size) ==
              XL_OK &&
          css == "4px");
    document.append(host, detached);
    render(view);
    CHECK(box("#detached").width > 7);

    for (const auto* style : {"solid", "dotted", "dashed", "double", "groove", "ridge", "inset", "outset"}) {
        page(std::string("<p style='margin:10px;border:6px ") + style + " #ff0000'>A</p>");
        auto image = pixels(view);
        size_t colored = 0;
        for (size_t i = 0; i < image.size(); i += 4)
            if (image[i] > 100 && image[i + 1] < 100 && image[i + 2] < 100) ++colored;
        CHECK(colored > 100);
    }
    page("<p style='margin-top:30px'><span id=a style='padding:12px 8px;border:3px solid "
         "red;background-color:#00ffff'>A<br>B</span></p>");
    const auto before = view.stats().rasterized_pixels;
    document.attribute(document.query("#a"), "style",
                       "padding:4px 2px;border:2px dashed blue;background-color:#ffff00");
    render(view);
    CHECK(view.stats().rasterized_pixels - before < 360u * 240u);
    {
        xl::View full(runtime, &options);
        full.document(&document);
        render(full);
        CHECK(pixels(view) == pixels(full));
    }
    CHECK(view.render() == 0);
    const std::string script = "document.getElementById('a').style.cssText='padding:5px;border:2px solid green'";
    CHECK(xl_document_eval(document.get(), script.data(), script.size(), 0) == XL_OK);
    render(view);
    {
        xl::View full(runtime, &options);
        full.document(&document);
        render(full);
        CHECK(pixels(view) == pixels(full));
    }
    page("<p><span id=a>A</span><span id=b style='vertical-align:8px'>B</span>"
         "<span id=c style='vertical-align:-6px'>C</span></p>");
    CHECK(near(box("#a").y - box("#b").y, 8) && near(box("#c").y - box("#a").y, 6));
    CHECK(near(box("p").height, 38));
    CHECK(document.stats().unsupported_layout_nodes == 0);
    nodes = texts(view);
    CHECK(nodes.size() == 3 && nodes[1].name == "B");
    ranges = view.text_rects(nodes[1].id, 0, 1);
    CHECK(ranges.size() == 1);
    hit = view.hit_text(ranges[0].x + ranges[0].width * .5f, ranges[0].y + ranges[0].height * .5f);
    CHECK(hit.node == nodes[1].id);
    for (size_t i = 0, count = texts(view).size(); i < count; ++i) {
        auto text_ranges = view.text_rects(nodes[i].id, 0, 1);
        const auto text_box = bounds(view, nodes[i].id);
        CHECK(near(text_box.y, text_ranges.front().y));
    }
    equivalent("<p>A<span style='vertical-align:50%'>B</span>C</p>",
               "<p>A<span style='vertical-align:12px'>B</span>C</p>");
    equivalent("<p>A<span style='font-size:40px;line-height:1.5;vertical-align:-20%'>B</span>C</p>",
               "<p>A<span style='font-size:40px;line-height:1.5;vertical-align:-12px'>B</span>C</p>");
    equivalent("<p>A<span style='vertical-align:.4em'>B</span>C</p>",
               "<p>A<span style='vertical-align:8px'>B</span>C</p>");
    page("<p><span id=a>A</span><span style='vertical-align:8px'>B<span id=b>C</span>"
         "<span id=c style='vertical-align:inherit'>D</span>"
         "<span id=d style='vertical-align:initial'>E</span></span></p>");
    CHECK(near(box("#a").y - box("#b").y, 8) && near(box("#b").y - box("#c").y, 8));
    CHECK(near(box("#b").y, box("#d").y) && near(box("p").height, 40));
    equivalent("<p>A<span style='vertical-align:50%'>B<span style='line-height:40px;vertical-align:inherit'>C</span></span>D</p>",
               "<p>A<span style='vertical-align:12px'>B<span style='line-height:40px;vertical-align:20px'>C</span></span>D</p>");
    // An ancestor's own line-height remains even with no direct text.
    page("<p><span style='line-height:60px'><span style='line-height:0;vertical-align:1px'>A</span></span></p>");
    CHECK(box("p").height >= 60);
    page("<p><span id=a>A</span><span id=b style='line-height:normal;vertical-align:50%'>B</span></p>");
    CHECK(box("#a").y - box("#b").y > 9 && box("#a").y - box("#b").y < 15);
    page("<p style='width:70px'><span id=a style='padding:3px;border:2px solid blue;vertical-align:8px'>A B C D E F G</span></p>");
    CHECK(box("p").height > 48);
    CHECK(view.render() == 0);
    const auto raster_before = view.stats().rasterized_pixels;
    CHECK(document.evaluate("document.getElementById('a').style.setProperty('vertical-align','-6px')") == XL_OK);
    render(view);
    CHECK(view.stats().rasterized_pixels - raster_before < 360u * 240u);
    {
        xl::View full(runtime, &options);
        full.document(&document);
        render(full);
        CHECK(pixels(view) == pixels(full));
    }
    page("<p><span style='vertical-align:middle'>A</span></p>");
    CHECK(document.stats().unsupported_layout_nodes == 0);
    page("<p><span id=a>A</span><span id=b style='line-height:60px;vertical-align:text-top'>B</span></p>");
    CHECK(std::abs(box("#b").y-(60-box("#a").height)*.5f-box("#a").y)<1);
    page("<p><span id=a>A</span><span id=b style='line-height:60px;vertical-align:text-bottom'>B</span></p>");
    CHECK(std::abs(box("#b").y+box("#b").height+(60-box("#a").height)*.5f-box("#a").y-box("#a").height)<1);
    page("<p><span id=a>A</span><span id=b style='vertical-align:sub'>B</span><span id=c style='vertical-align:super'>C</span></p>");
    CHECK(near(box("#b").y-box("#a").y,4) && std::abs(box("#a").y-box("#c").y-20.0f/3)<1);
    page("<p><span style='vertical-align:top'>A</span></p>");
    CHECK(document.stats().unsupported_layout_nodes == 0 && near(box("p").height, 24));
    page("<p><span id=a>A</span><span id=b style='line-height:60px;vertical-align:top'>B</span></p>");
    CHECK(near(box("p").height, 60) && near(box("#b").y - box("#a").y, 18));
    page("<p><span id=a>A</span><span id=b style='line-height:60px;vertical-align:bottom'>B</span></p>");
    CHECK(near(box("p").height, 60) && near(box("#a").y - box("#b").y, 18));
    page("<p><span id=a>A</span><span id=b style='line-height:60px;vertical-align:top'>B</span>"
         "<span id=c style='line-height:40px;vertical-align:bottom'>C</span></p>");
    CHECK(near(box("p").height, 60) && near(box("#b").y - box("#c").y, -10) && std::abs(box("#a").y) < 1);
    // Nested groups resolve before their containing top/bottom group is placed.
    page("<p><span id=a>A</span><span id=b style='vertical-align:top'>B"
         "<span id=c style='line-height:60px;vertical-align:bottom'>C</span></span></p>");
    CHECK(near(box("p").height, 60) && near(box("#b").y - box("#a").y, 36));
    CHECK(near(box("#b").y - box("#c").y, 18));
    page("<p><span id=a>A</span><span id=b style='vertical-align:8px'>B"
         "<span id=c style='vertical-align:top'>C</span></span></p>");
    CHECK(near(box("#a").y - box("#b").y, 8) && near(box("#b").y - box("#c").y, 8));
    equivalent("<p>A<span style='vertical-align:top'>B<span style='vertical-align:inherit'>C</span></span></p>",
               "<p>A<span style='vertical-align:top'>B</span><span style='vertical-align:top'>C</span></p>");
    page("<p>A<span id=a style='line-height:60px;vertical-align:bottom'></span></p>");
    CHECK(near(box("p").height, 60) && near(box("#a").width, 0) && texts(view).size() == 1);
    page("<p><span id=a style='vertical-align:top;line-height:30px;padding:20px;border:2px solid red'>A</span></p>");
    CHECK(near(box("p").height, 30) && box("#a").height > 60);
    page("<p style='width:65px'><span id=a style='vertical-align:bottom;line-height:40px'>A B C D E F G</span></p>");
    nodes = texts(view);
    ranges = view.text_rects(nodes.front().id, 0, 13);
    CHECK(ranges.size() > 1 && near(box("p").height, float(ranges.size()) * 40));
    CHECK(near(ranges[1].y - ranges[0].y, 40));
    for (const auto& rect : ranges) {
        hit = view.hit_text(rect.x + rect.width * .5f, rect.y + rect.height * .5f);
        CHECK(hit.node == nodes.front().id);
    }
    view.selection({sizeof(xl_text_position), 0, nodes.front().id, XL_TEXT_DOWNSTREAM, 0},
                   {sizeof(xl_text_position), 13, nodes.front().id, XL_TEXT_UPSTREAM, 0});
    render(view);
    const auto selected = view.selection_rects();
    CHECK(selected.size() == ranges.size() && view.selected_text() == "A B C D E F G");
    for (size_t i = 0; i < selected.size(); ++i)
        CHECK(near(selected[i].x, ranges[i].x) && near(selected[i].y, ranges[i].y) &&
              near(selected[i].height, ranges[i].height));
    view.clear_selection();
    render(view);
    CHECK(view.render() == 0);
    const auto alignment_pixels = view.stats().rasterized_pixels;
    CHECK(document.evaluate("document.getElementById('a').style.setProperty('vertical-align','top')") == XL_OK);
    render(view);
    CHECK(view.stats().rasterized_pixels - alignment_pixels < 360u * 240u);
    {
        xl::View full(runtime, &options);
        full.document(&document);
        render(full);
        CHECK(pixels(view) == pixels(full));
    }
    CHECK(document.stats().unsupported_layout_nodes == 0);
    page("<p><span id=a style='vertical-align:top;line-height:60px'><br><br><span id=b>B</span></span></p>");
    CHECK(near(box("p").height, 180) && box("#b").y > 137 && near(box("#a").height, box("#b").height));
    page("<p style='white-space:pre'><span id=a style='vertical-align:bottom;line-height:60px'>\nB</span></p>");
    CHECK(near(box("p").height, 120) && box("#a").y > 77 && box("#a").height > 20 && box("#a").height < 30);
    page("<p><span id=a style='vertical-align:top;line-height:60px'><br></span></p>");
    CHECK(near(box("p").height, 60) && near(box("#a").width, 0));
    std::cout << "Inline edges, decoration, borders, vertical offsets, DOM maps, AX and retained pixels passed\n";
}
