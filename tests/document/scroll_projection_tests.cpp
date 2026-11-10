#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
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
    std::ifstream input(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!font.empty());
    CHECK(xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN;
    options.width = 240;
    options.height = 160;
    xl::View view(runtime, &options);
    view.document(&document);
    view.ax_updates(true);
    if (argc == 2 && std::string_view(argv[1]) == "--gpu")
        view.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
    const auto render = [&](xl::View &target) {
        xl_status status;
        uint32_t flags;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((status = xl_view_render(target.get(), &flags)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < end);
            std::this_thread::yield();
        }
        CHECK(status == XL_OK);
    };
    const auto pixels = [&] {
        std::vector<uint8_t> bytes(240 * 160 * 4);
        CHECK(xl_view_copy_rgba(view.get(), bytes.data(), bytes.size(), 240 * 4) == XL_OK);
        return bytes;
    };
    const auto stats = [&] {
        xl_document_stats value{};
        CHECK(xl_document_get_stats(document.get(), &value, sizeof(value)) == XL_OK);
        return value;
    };
    const auto snapshot = [&] {
        std::vector<xl_ax_node> result;
        size_t count;
        CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
        for (size_t i = 0; i < count; ++i) {
            xl_ax_node node{};
            CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
            result.push_back(node);
        }
        return result;
    };
    size_t generation = 0;
    const auto compare_relayout = [&] {
        const auto before = pixels();
        const auto ax = snapshot();
        const auto old = stats();
        document.attribute(document.query("body"), "data-relayout-oracle", std::to_string(++generation));
        render(view);
        const auto after = pixels();
        if (before != after) {
            size_t count = 0;
            for (size_t i = 0; i < before.size(); ++i)
                count += before[i] != after[i];
            std::cerr << "projection versus relayout pixel bytes differ " << count << " case " << generation << '\n';
            std::exit(1);
        }
        const auto actual = snapshot();
        CHECK(ax.size() == actual.size());
        for (size_t i = 0; i < ax.size(); ++i) {
            const auto &a = ax[i];
            const auto &b = actual[i];
            CHECK(a.node == b.node && a.role == b.role && a.parent == b.parent && a.states == b.states &&
                  a.actions == b.actions);
            CHECK(std::abs(a.bounds.x - b.bounds.x) < .02f && std::abs(a.bounds.y - b.bounds.y) < .02f &&
                  std::abs(a.bounds.width - b.bounds.width) < .02f &&
                  std::abs(a.bounds.height - b.bounds.height) < .02f);
        }
        CHECK(stats().layout_frames == old.layout_frames + 1);
        CHECK(view.render() == 0);
    };
    const auto page = [&](std::string html) {
        document.html("<style>body{margin:0;font:16px/20px test}div,p{margin:0}</style>" + html);
        render(view);
    };
    const auto scroll = [&](xl_node_id node, float x, float y) {
        const auto old = stats();
        view.scroll_to(node, x, y);
        render(view);
        CHECK(stats().layout_frames == old.layout_frames && stats().revision == old.revision);
    };
    page("<style>#outer{width:120px;height:65px;overflow:auto;border:3px solid black;border-radius:15px;padding:5px}"
         "#inner{width:180px;height:40px;overflow:auto;border-radius:12px;border:2px solid blue}"
         ".row{width:200px;height:36px;background:red}.row:nth-child(2n){background:lime}</style>"
         "<div id=outer><div id=inner><div class=row role=button aria-label=first>one two three</div><div class=row "
         "role=button aria-label=second>four five six</div><div class=row>seven</div></div>"
         "<div style='height:120px;background:blue'></div></div><div style='height:300px'>viewport</div>");
    const auto outer = document.query("#outer"), inner = document.query("#inner");
    xl::AXUpdate retained, batch;
    while (view.take_ax_update(batch))
        retained = std::move(batch);
    CHECK(retained);
    const auto old_root = retained.node(0);
    for (int i = 0; i < 8; ++i) {
        scroll(inner, i * 3.25f, i * 8.5f);
        scroll(outer, i * 2.5f, i * 5.25f);
        scroll(0, 0, i * 3.5f);
        compare_relayout();
    }
    CHECK(retained.node(0).bounds.x == old_root.bounds.x && retained.node(0).bounds.y == old_root.bounds.y);
    scroll(inner, 0, 0);
    scroll(outer, 0, 0);
    scroll(0, 0, 0);
    compare_relayout();
    const auto layouts = stats().layout_frames;
    xl::View other(runtime, &options);
    other.document(&document);
    render(other);
    const auto with_other = stats().layout_frames;
    CHECK(with_other == layouts + 1);
    scroll(inner, 12, 21);
    render(other);
    CHECK(other.scroll(inner).y == 0 && view.scroll(inner).y == 21);
    CHECK(stats().layout_frames == with_other);
    compare_relayout();
    page("<p>Before <span id=atomic style='display:inline-block;width:90px;height:48px;overflow:auto;border:3px solid "
         "black;border-radius:13px'>"
         "<span id=inner style='display:block;width:140px;height:35px;overflow:auto;border:2px solid "
         "blue;border-radius:10px'>"
         "<span style='display:block;width:180px;height:38px;background:red'>Alpha text</span><span "
         "style='display:block;height:70px;background:lime'>Beta text</span></span>"
         "<span style='display:block;height:100px;background:blue'>Gamma text</span></span> After</p><div "
         "style='height:400px'>body scroll</div>");
    const auto atomic = document.query("#atomic"), nested = document.query("#inner");
    for (int i = 0; i < 6; ++i) {
        scroll(nested, i * 6.5f, i * 11.25f);
        scroll(atomic, i * 4.25f, i * 9.75f);
        scroll(0, 0, i * 3.5f);
        compare_relayout();
    }
    scroll(nested, 0, 0);
    scroll(atomic, 0, 0);
    scroll(0, 0, 0);
    compare_relayout();
    const std::string identical_atomic="<p><span id=same style='display:inline-block;width:70px;height:30px;overflow:auto;border-radius:8px'>"
        "<span style='display:block;height:35px;background:red'>Same</span><span style='display:block;height:60px;background:lime'>Content</span>"
        "</span></p>";
    for(int i=0;i<3;++i){page(identical_atomic);scroll(document.query("#same"),0,22);compare_relayout();}
    page("<div id=reverse "
         "style='display:flex;flex-direction:column-reverse;width:100px;height:55px;overflow:auto;border-radius:12px'>"
         "<div style='height:80px;flex-shrink:0;background:red'>First</div><div "
         "style='height:80px;flex-shrink:0;background:lime'>Second</div></div>");
    const auto reverse = document.query("#reverse");
    CHECK(view.scroll(reverse).min_y < 0);
    scroll(reverse, 0, -47.5f);
    compare_relayout();
    page("<div id=rtl style='display:flex;flex-direction:row-reverse;width:100px;height:40px;overflow:auto'><div "
         "style='flex-shrink:0;width:230px;height:40px;background:red'>rtl</div></div>");
    const auto rtl = document.query("#rtl");
    CHECK(view.scroll(rtl).min_x < 0);
    scroll(rtl, -67.5f, 0);
    compare_relayout();
    page("<div id=control style='width:100px;height:40px;overflow:hidden'><span style='white-space:nowrap'>one two "
         "three four five six seven</span></div>");
    const auto control = document.query("#control");
    scroll(control, 75, 0);
    const auto hit = view.hit_text(20, 10);
    CHECK(hit.offset_utf16 > 2);
    compare_relayout();
    CHECK(xl_view_resize(view.get(), 200, 160) == XL_OK);
    const auto before_resize = stats();
    render(view);
    CHECK(stats().layout_frames == before_resize.layout_frames + 1);
    if (argc == 2 && std::string_view(argv[1]) == "--gpu")
        CHECK(view.graphics_info().active == XL_GRAPHICS_SWIFTSHADER);
    std::cout << "Pure scrolling retained layout, nested clips/atomic scenes, AX and pixels match full relayout\n";
}
