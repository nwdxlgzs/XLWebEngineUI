#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>
#include <vector>
#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value)) {                                                                                                \
            std::cerr << __LINE__ << ": " #value "\n";                                                                 \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)
int main(int argc, char **argv) {
    xl::Runtime runtime;
    xl::Document document(runtime);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN;
    options.width = 240;
    options.height = 200;
    xl::View view(runtime, &options);
    view.document(&document);
    view.ax_updates(true);
    if (argc == 2 && std::string(argv[1]) == "--gpu")
        CHECK(xl_view_set_graphics_backend(view.get(), XL_GRAPHICS_SWIFTSHADER) == XL_OK);
    const auto page = [&](const std::string &html) {
        document.html("<style>body{margin:0}#x{width:80px;height:80px;background:red}</style>" + html);
    };
    const auto render = [&] {
        uint32_t flags;
        xl_status status;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        if (status != XL_OK)
            std::cerr << "render " << status << '\n';
        CHECK(status == XL_OK);
        return flags;
    };
    const auto pixels = [&] {
        std::vector<unsigned char> bytes(240 * 200 * 4);
        CHECK(xl_view_copy_rgba(view.get(), bytes.data(), bytes.size(), 240 * 4) == XL_OK);
        return bytes;
    };
    const auto pixel = [&](int x, int y) {
        const auto bytes = pixels();
        const auto i = size_t(y * 240 + x) * 4;
        return uint32_t(bytes[i]) << 24 | uint32_t(bytes[i + 1]) << 16 | uint32_t(bytes[i + 2]) << 8 | bytes[i + 3];
    };
    const auto expect = [&](int x, int y, uint32_t value,
                            const std::source_location &at = std::source_location::current()) {
        const auto actual = pixel(x, y);
        if (actual != value) {
            std::cerr << at.line() << " pixel " << x << ',' << y << " expected " << std::hex << value << " got "
                      << actual << std::dec << '\n';
            std::exit(1);
        }
    };
    const auto script = [&](const char *source, const char *expected,
                            const std::source_location &at = std::source_location::current()) {
        const auto status = document.evaluate(source);
        if (status != XL_OK || document.result() != expected) {
            std::cerr << at.line() << " status " << status << ' ' << source << " expected " << expected << " got "
                      << document.result() << '\n';
            std::exit(1);
        }
    };
    const auto bounds = [&](const char *selector) {
        xl_node_bounds result{};
        CHECK(xl_view_node_bounds(view.get(), document.query(selector), &result) == XL_OK);
        return result;
    };
    const auto ax = [&](xl_node_id id) {
        size_t count;
        CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
        for (size_t i = 0; i < count; ++i) {
            xl_ax_node node{};
            CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
            if (node.node == id)
                return node;
        }
        std::cerr << "missing AX node " << id << '\n';
        std::exit(1);
    };
    page("<div id=x style='border-radius:50%'></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(10, 10, 0xffffffff);
    expect(40, 2, 0xff0000ff);
    expect(20, 20, 0xff0000ff);
    CHECK(bounds("#x").width == 80 && bounds("#x").height == 80);
    CHECK(view.render() == 0);
    xl_resource_stats resources{};
    CHECK(xl_runtime_resource_stats(runtime.get(), &resources, sizeof(resources)) == XL_OK);
    CHECK(resources.decompressions == 0 && resources.worker_threads == 0);
    document.attribute(document.query("#x"), "style", "border-radius:9999px");
    render();
    expect(1, 1, 0xffffffff);
    expect(20, 20, 0xff0000ff);
    CHECK(view.render() == 0);
    document.attribute(document.query("#x"), "style", "border-radius:0");
    render();
    expect(1, 1, 0xff0000ff);
    CHECK(view.render() == 0);
    page("<div id=x style='width:120px;height:60px;border-radius:50%'></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(12, 6, 0xffffffff);
    expect(30, 8, 0xff0000ff);
    expect(60, 1, 0xff0000ff);
    page("<div id=x style='border-radius:40px 0 0 0'></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(78, 1, 0xff0000ff);
    expect(1, 78, 0xff0000ff);
    page("<div id=x style='border-radius:40px / 20px'></div>");
    render();
    expect(3, 3, 0xffffffff);
    expect(20, 6, 0xff0000ff);
    expect(3, 30, 0xff0000ff);
    page("<div id=x style='border-radius:80px 80px / 40px 40px'></div>");
    render();
    expect(10, 2, 0xffffffff);
    expect(40, 1, 0xff0000ff);
    expect(1, 30, 0xff0000ff);
    page("<style>#x{border-radius:40px;border-radius:-1px}#x{border-top-right-radius:0!important}</style><div "
         "id=x></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(78, 1, 0xff0000ff);
    page("<style>#x{border-radius:40px;border-radius:var(--missing)}</style><div id=x></div>");
    render();
    expect(1, 1, 0xff0000ff);
    page("<style>#x{--radius:50%;border-radius:var(--radius)}#x::before{content:'';display:block;width:20px;height:"
         "20px;border-radius:inherit;background:blue}</style><div id=x></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(10, 2, 0x0000ffff);
    expect(40, 2, 0xff0000ff);
    page("<style>@layer a,b;@layer a{#x{border-radius:50%}}@layer "
         "b{#x{border-radius:0;border-radius:revert-layer}}</style><div id=x></div>");
    render();
    expect(1, 1, 0xffffffff);
    page("<div id=x style='font-size:20px;border-radius:calc(1em + 25% - 10px)'></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(30, 2, 0xff0000ff);
    page("<div id=x style='border-radius:calc((10px + 10%) * 2)'></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(40, 2, 0xff0000ff);
    page("<div id=x style='border-radius:calc(10px - 100%)'></div>");
    render();
    expect(1, 1, 0xff0000ff);
    for (const auto *invalid : {"calc(20px +20px)", "calc(20px+ 20px)", "calc(0)", "(40px)", "calc(2px * 3px)",
                                "calc(20px / 0)", "10px /", "-1%"}) {
        page(std::string("<div id=x style='border-radius:40px;border-radius:") + invalid + "'></div>");
        render();
        expect(1, 1, 0xffffffff);
    }
    page("<div id=x style='border-radius:0 40px / 20px 0'></div>");
    render();
    expect(1, 1, 0xff0000ff);
    expect(78, 1, 0xff0000ff);

    page("<div id=x style='border:8px solid blue;border-radius:20px'></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(40, 2, 0x0000ffff);
    expect(10, 10, 0x0000ffff);
    expect(16, 16, 0xff0000ff);
    expect(40, 12, 0xff0000ff);
    page("<div id=x style='border:9px double blue;border-radius:30px'></div>");
    render();
    expect(40, 1, 0x0000ffff);
    expect(40, 4, 0xff0000ff);
    expect(40, 7, 0x0000ffff);
    expect(40, 11, 0xff0000ff);
    expect(1, 1, 0xffffffff);
    page("<div id=x style='box-sizing:border-box;border:12px solid;border-color:red green blue "
         "yellow;border-radius:50%;background:white'></div>");
    render();
    expect(40, 2, 0xff0000ff);
    expect(77, 40, 0x008000ff);
    expect(40, 77, 0x0000ffff);
    expect(2, 40, 0xffff00ff);
    expect(1, 1, 0xffffffff);
    expect(40, 40, 0xffffffff);
    for (const auto *style : {"dotted", "dashed", "groove", "ridge", "inset", "outset"}) {
        page(std::string("<div id=x style='border:8px ") + style + " blue;border-radius:50%'></div>");
        render();
        expect(1, 1, 0xffffffff);
        expect(40, 40, 0xff0000ff);
        CHECK(view.render() == 0);
    }
    page("<div id=x style='border:8px solid transparent;border-radius:20px'></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(40, 2, 0xff0000ff);
    page("<div id=x style='border:30px solid blue;width:10px;height:10px;border-radius:50%'></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(35, 35, 0xff0000ff);
    expect(35, 2, 0x0000ffff);

    page("<style>#fill{width:80px;height:140px;background:lime}</style><div id=x "
         "style='border-radius:50%;overflow:auto'><div id=fill></div></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(40, 2, 0x00ff00ff);
    CHECK(bounds("#fill").height == 140);
    CHECK(ax(document.query("#fill")).bounds.height == 80);
    CHECK(xl_view_scroll_at(view.get(), 2, 2, 0, 20) == XL_OK);
    render();
    CHECK(view.scroll(document.query("#x")).y == 0);
    CHECK(xl_view_scroll_at(view.get(), 40, 40, 0, 20) == XL_OK);
    render();
    CHECK(view.scroll(document.query("#x")).y == 20);
    expect(1, 1, 0xffffffff);
    expect(40, 40, 0x00ff00ff);
    page("<div id=x style='border:8px solid blue;border-radius:30px;overflow:hidden'><div "
         "style='width:160px;height:160px;background:lime'></div></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(48, 2, 0x0000ffff);
    expect(48, 10, 0x00ff00ff);
    expect(13, 13, 0x0000ffff);
    page("<div id=x style='border-radius:40px;overflow:visible'><div "
         "style='width:80px;height:80px;background:lime'></div></div>");
    render();
    expect(1, 1, 0x00ff00ff);
    page("<div id=x style='border-radius:40px;overflow-x:clip;overflow-y:visible'><div "
         "style='width:80px;height:120px;background:lime'></div></div>");
    render();
    expect(1, 1, 0x00ff00ff);
    expect(40, 100, 0x00ff00ff);
    page("<div id=x style='border-radius:50%;overflow:hidden'><div "
         "style='width:80px;height:80px;border-radius:20px;overflow:hidden'><div "
         "style='width:120px;height:120px;background:blue'></div></div></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(40, 2, 0x0000ffff);
    expect(40, 40, 0x0000ffff);
    page("<div id=x style='display:flex;border-radius:50%;overflow:hidden'><div "
         "style='flex-shrink:0;width:120px;height:120px;background:lime'></div></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(40, 40, 0x00ff00ff);
    page("<div id=x style='display:grid;border-radius:50%;overflow:hidden'><div "
         "style='width:120px;height:120px;background:blue'></div></div>");
    render();
    expect(1, 1, 0xffffffff);
    expect(40, 40, 0x0000ffff);

    page("<style>#x{border-radius:50%;overflow:hidden}#target{display:block;width:8px;height:8px;padding:0;border:0;"
         "background:blue}</style><div id=x><button id=target></button></div>");
    render();

    script("globalThis.clicks=0;document.getElementById('target').onclick=()=>clicks++;'ready'", "ready");
    const auto click = [&](float x, float y) {
        const xl_pointer_event down{sizeof(down), XL_POINTER_DOWN, 0, 0, x, y},
            up{sizeof(up), XL_POINTER_UP, 0, 0, x, y};
        uint32_t handled = 0;
        CHECK(xl_view_pointer(view.get(), &down, sizeof(down), &handled) == XL_OK && handled);
        render();
        CHECK(xl_view_pointer(view.get(), &up, sizeof(up), &handled) == XL_OK && handled);
        render();
    };
    click(2, 2);
    script("clicks", "0");
    CHECK(bounds("#target").width == 8);
    CHECK(ax(document.query("#target")).states & XL_AX_OFFSCREEN);
    xl::AXUpdate previous, batch;
    while (view.take_ax_update(batch))
        previous = std::move(batch);
    CHECK(previous);
    document.attribute(document.query("#x"), "style", "border-radius:0");
    render();
    click(2, 2);
    script("clicks", "1");
    CHECK(bounds("#target").width == 8);
    CHECK(!(ax(document.query("#target")).states & XL_AX_OFFSCREEN));
    bool old_offscreen = false;
    for (size_t i = 0; i < previous.info().node_count; ++i)
        if (previous.node(i).node == document.query("#target"))
            old_offscreen = (previous.node(i).states & XL_AX_OFFSCREEN) != 0;
    CHECK(old_offscreen);

    std::ifstream font_file(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(font_file), std::istreambuf_iterator<char>()};
    CHECK(!font.empty());
    CHECK(xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    page("<style>body{font:16px/20px test}#x{border-radius:50%;overflow:hidden}span{background:blue}</style><div "
         "id=x><span>office reading and selection</span></div>");
    render();
    expect(1, 1, 0xffffffff);
    CHECK(view.render() == 0);
    xl_text_position clipped_position{};
    CHECK(xl_view_hit_test_text(view.get(), 1, 1, &clipped_position, sizeof(clipped_position)) == XL_NOT_FOUND);
    page("<style>body{font:16px/20px "
         "test}#x{width:80px;height:40px;display:inline-block;border-radius:20px;overflow:hidden}#fill{width:120px;"
         "height:80px;background:blue}</style><div id=x><div id=fill></div></div>");
    render();
    const auto atomic = bounds("#x");
    expect(int(atomic.x) + 1, int(atomic.y) + 1, 0xffffffff);
    expect(int(atomic.x) + 40, int(atomic.y) + 20, 0x0000ffff);
    CHECK(view.render() == 0);
    page("<style>body{font:20px/30px test}span{padding:4px 10px;background:red;border:3px solid "
         "blue;border-radius:15px}</style><div style='width:100px'><span id=inline>one two three four</span></div>");
    render();
    CHECK(bounds("#inline").height > 30);
    CHECK(view.render() == 0);
    const auto inline_pixels = pixels();
    CHECK(xl_view_invalidate(view.get(), 0, 1, 100, 3) == XL_OK);
    render();
    CHECK(pixels() == inline_pixels);
    page("<div id=x style='border-radius:20px;outline:4px solid blue;outline-offset:2px;margin:10px'></div>");
    render();
    expect(8, 8, 0xffffffff);
    expect(50, 5, 0x0000ffff);
    CHECK(view.render() == 0);
    page("<div id=x style='border-radius:50%;overflow:hidden;border:4px solid blue'><div "
         "style='width:100px;height:100px;background:lime'></div></div>");
    render();
    const auto stable_pixels = pixels();
    const auto old_painted = view.stats().rasterized_pixels;
    for (int axis = 0; axis < 2; ++axis)
        for (int offset = 0; offset < 90; offset += 3) {
            CHECK(xl_view_invalidate(view.get(), axis ? offset : 0, axis ? 0 : offset, axis ? 3 : 90, axis ? 90 : 3) ==
                  XL_OK);
            render();
            CHECK(pixels() == stable_pixels);
        }
    CHECK(view.stats().rasterized_pixels - old_painted < 60 * 240 * 200);
    for (const auto *change :
         {"border-radius:0", "border-radius:15px 40px / 30px 20px;border:5px solid blue;overflow:hidden",
          "border-radius:50%;margin:20px;overflow:hidden"}) {
        document.attribute(document.query("#x"), "style", change);
        render();
        const auto incremental = pixels();
        CHECK(xl_view_invalidate(view.get(), 0, 0, 240, 200) == XL_OK);
        render();
        CHECK(pixels() == incremental);
        CHECK(view.render() == 0);
    }
    if (argc == 2 && std::string(argv[1]) != "--gpu") {
        std::ifstream input(argv[1], std::ios::binary);
        const std::string html{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        CHECK(!html.empty());
        document.html(html);
        render();
        script("getComputedStyle(document.querySelector('.btn')).getPropertyValue('border-top-left-radius')",
               "10px 10px");
    }
    if (argc == 2 && std::string(argv[1]) == "--gpu") {
        xl_graphics_info info{};
        CHECK(xl_view_get_graphics_info(view.get(), &info, sizeof(info)) == XL_OK);
        CHECK(info.active == XL_GRAPHICS_SWIFTSHADER);
    }
    std::cout << "radius cascade, overlap/ellipses, curved borders/overflow, hit/scroll/AX, inline and retained pixels "
                 "passed\n";
}
