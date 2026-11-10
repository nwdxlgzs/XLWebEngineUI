#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#define CHECK(c)                                                               \
  do {                                                                         \
    if (!(c)) {                                                                \
      std::cerr << __LINE__ << ": " #c "\n";                                   \
      throw std::runtime_error("check failed");                                \
    }                                                                          \
  } while (false)
int main(int argc, char **argv) try {
  xl::Runtime runtime;
  xl::Document document(runtime);
  xl_view_options options;
  CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
  options.kind = XL_VIEW_OFFSCREEN;
  options.width = 240;
  options.height = 160;
  xl::View view(runtime, &options);
  view.document(&document);
  view.ax_updates(true);
  const bool gpu = argc == 2 && std::string_view(argv[1]) == "--gpu";
  if (gpu)
    view.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
  const auto render = [&](xl::View *other = nullptr) {
    auto &target = other ? *other : view;
    xl_status status;
    uint32_t flags;
    const auto end =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((status = xl_view_render(target.get(), &flags)) == XL_BUSY) {
      CHECK(std::chrono::steady_clock::now() < end);
      std::this_thread::yield();
    }
    CHECK(status == XL_OK);
  };
  const auto pixels = [&](xl::View *other = nullptr) {
    auto &target = other ? *other : view;
    std::vector<uint8_t> bytes(240 * 160 * 4);
    CHECK(xl_view_copy_rgba(target.get(), bytes.data(), bytes.size(),
                            240 * 4) == XL_OK);
    return bytes;
  };
  document.html("<style>body{margin:0}div{width:50px;height:40px;position:"
                "relative}#a{background:red;z-index:9}#b{background:blue;top:-"
                "40px;z-index:1}</style><div id=a></div><div id=b></div>");
  render();
  const auto image = pixels();
  CHECK(image[(10 * 240 + 10) * 4] == 255 &&
        image[(10 * 240 + 10) * 4 + 2] == 0);
  const auto pixel = [&](int x, int y) {
    const auto bytes = pixels();
    const auto i = (y * 240 + x) * 4;
    return uint32_t(bytes[i]) << 24 | uint32_t(bytes[i + 1]) << 16 |
           uint32_t(bytes[i + 2]) << 8 | bytes[i + 3];
  };
  const auto page = [&](std::string css,
                        std::string html = "<div id=a></div><div id=b></div>") {
    document.html("<style>body{margin:0;font:16px/20px "
                  "test}#a,#b,#c{width:50px;height:40px}#a,#c{background:red}#"
                  "b{background:blue}" +
                  css + "</style>" + html);
    render();
  };
  const auto overlap = [&](std::string a, std::string b = "z-index:1") {
    page("#a{position:relative;" + a + "}#b{position:relative;top:-40px;" + b +
         "}");
  };
  overlap("z-index:9");
  CHECK(pixel(10, 10) == 0xff0000ff);
  CHECK(view.render() == 0);
  const auto old_stats = view.stats();
  document.attribute(document.query("#a"), "style", "z-index:0");
  render();
  CHECK(pixel(10, 10) == 0x0000ffff);
  CHECK(view.stats().rasterized_pixels - old_stats.rasterized_pixels <
        240 * 160);
  document.html("<section><details open><summary id=s1></summary><p "
                "id=p1></p></details><details><summary id=s2></summary><p "
                "id=p2></p></details></section>");
  CHECK(document.query("details:nth-of-type(2) p") == document.query("#p2"));
  CHECK(document.query("details:nth-of-type(2) q") == 0);
  CHECK(document.query("details:not([open]) p") == document.query("#p2"));
  CHECK(document.query("section:has(> details:nth-of-type(2) > p)") ==
        document.query("section"));
  overlap("z-index:auto");
  CHECK(pixel(10, 10) == 0x0000ffff);
  overlap("z-index:5", "z-index:5");
  CHECK(pixel(10, 10) == 0x0000ffff);
  overlap("z-index:9;z-index:invalid");
  CHECK(pixel(10, 10) == 0xff0000ff);
  overlap("z-index:9;z-index:1.0");
  CHECK(pixel(10, 10) == 0xff0000ff);
  overlap("z-index:9;z-index:var(--missing)");
  CHECK(pixel(10, 10) == 0x0000ffff);
  overlap("--z:9;z-index:var(--z)");
  CHECK(pixel(10, 10) == 0xff0000ff);
  overlap("z-index:9!important;z-index:0");
  CHECK(pixel(10, 10) == 0xff0000ff);
  overlap("z-index:9999999999999999999999999");
  CHECK(pixel(10, 10) == 0xff0000ff);
  page("#a{position:relative;z-index:9}#b{position:relative;top:-40px;z-index:"
       "1}@layer base,top;@layer base{#a{z-index:8}}@layer "
       "top{#a{z-index:0;z-index:revert-layer}}");
  CHECK(pixel(10, 10) == 0xff0000ff);
  // Static ordinary blocks ignore z-index; positioned auto content paints
  // above non-positioned content even when the latter occurs later.
  page("#a{z-index:9}#b{margin-top:-40px;z-index:1}");
  CHECK(pixel(10, 10) == 0x0000ffff);
  page("#a{position:relative}#b{margin-top:-40px}");
  CHECK(pixel(10, 10) == 0xff0000ff);
  const auto nested = [&](std::string parent, std::string child = "z-index:9",
                          std::string other = "z-index:1") {
    page("#p{width:50px;height:40px;background:lime;" + parent +
             "}#c{position:relative;" + child +
             "}#b{position:relative;top:-40px;" + other + "}",
         "<div id=p><div id=c></div></div><div id=b></div>");
  };
  nested("position:relative;z-index:auto");
  CHECK(pixel(10, 10) == 0xff0000ff);
  nested("position:relative;z-index:0");
  CHECK(pixel(10, 10) == 0x0000ffff);
  nested("isolation:isolate");
  CHECK(pixel(10, 10) == 0x0000ffff);
  nested("isolation:auto");
  CHECK(pixel(10, 10) == 0xff0000ff);
  nested("position:relative;z-index:-2", "z-index:9999", "z-index:-1");
  CHECK(pixel(10, 10) == 0x0000ffff);
  page("#p{width:50px;height:40px;background:lime;position:relative}#c{"
       "position:relative;z-index:-1}",
       "<div id=p><div id=c></div></div>");
  CHECK(pixel(10, 10) == 0x00ff00ff);
  document.attribute(document.query("#p"), "style", "z-index:0");
  render();
  CHECK(pixel(10, 10) == 0xff0000ff);
  for (const auto *display : {"flex", "grid"}) {
    const auto layout = std::string(display) == "flex"
                            ? "flex:none;margin-right:-50px"
                            : "grid-area:1/1";
    page("#p{display:" + std::string(display) + ";width:60px}#a,#b{" + layout +
             "}#a{z-index:9}#b{z-index:1}",
         "<div id=p><div id=a></div><div id=b></div></div>");
    CHECK(pixel(10, 10) == 0xff0000ff);
    document.attribute(document.query("#a"), "style", "z-index:auto;order:2");
    document.attribute(document.query("#b"), "style", "z-index:auto;order:1");
    render();
    CHECK(pixel(10, 10) == 0xff0000ff);
    document.attribute(document.query("#a"), "style", "z-index:0;order:1");
    document.attribute(document.query("#b"), "style", "z-index:0;order:2");
    render();
    CHECK(pixel(10, 10) == 0x0000ffff);
  }
  xl_resource_stats resources{};
  CHECK(xl_runtime_resource_stats(runtime.get(), &resources,
                                  sizeof(resources)) == XL_OK);
  CHECK(resources.decompressions == 0 && resources.worker_threads == 0);
  std::ifstream input(XL_TEST_FONT, std::ios::binary);
  const std::vector<char> font{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
  CHECK(!font.empty());
  CHECK(xl_document_add_font(document.get(), "test", font.data(),
                             font.size()) == XL_OK);
  const auto compare_cold = [&] {
    const auto expected = pixels();
    xl::View cold(runtime, &options);
    cold.document(&document);
    cold.ax_updates(true);
    if (gpu)
      cold.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
    render(&cold);
    CHECK(expected == pixels(&cold));
  };
  // Pointer targeting follows the visible layer, while AX stays in DOM order.
  page("button{padding:0;border:0}#a{position:relative;z-index:9}#b{position:"
       "relative;top:-40px;z-index:1}",
       "<button id=a aria-label=first></button><button id=b "
       "aria-label=second></button>");
  // Buttons are inline by default; make this overlap independent of their text
  // metrics.
  document.attribute(document.query("#a"), "style", "display:block");
  document.attribute(document.query("#b"), "style", "display:block");
  render();
  CHECK(document.evaluate("globalThis.clicked='';document.getElementById('a')."
                          "onclick=()=>clicked='a';document.getElementById('b')"
                          ".onclick=()=>clicked='b';'ready'") == XL_OK);
  const auto click = [&](float x, float y) {
    const xl_pointer_event down{sizeof(down), XL_POINTER_DOWN, 0, 0, x, y},
        up{sizeof(up), XL_POINTER_UP, 0, 0, x, y};
    view.pointer(down);
    render();
    view.pointer(up);
    render();
  };
  click(10, 10);
  CHECK(document.evaluate("clicked") == XL_OK && document.result() == "a");
  size_t count;
  CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
  size_t ai = count, bi = count;
  for (size_t i = 0; i < count; ++i) {
    xl_ax_node node{};
    CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
    if (node.node == document.query("#a"))
      ai = i;
    if (node.node == document.query("#b"))
      bi = i;
  }
  CHECK(ai < bi);
  // An opaque front sibling must not direct a wheel into the covered port.
  page("#p{width:60px;height:40px;overflow:auto}#c{height:120px}#b{position:"
       "relative;top:-40px;z-index:9;width:60px}",
       "<div id=p><div id=c></div></div><div id=b></div>");
  view.scroll_at(10, 10, 0, 20);
  render();
  CHECK(view.scroll(document.query("#p")).y == 0);
  document.attribute(document.query("#b"), "style", "left:80px");
  render();
  view.scroll_at(10, 10, 0, 20);
  render();
  CHECK(view.scroll(document.query("#p")).y == 20);
  const auto bounds = [&](const char *selector) {
    xl_node_bounds value{};
    CHECK(xl_view_node_bounds(view.get(), document.query(selector), &value) ==
          XL_OK);
    return value;
  };
  const auto text_node = [&](const char *selector) {
    const auto parent = document.query(selector);
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    for (size_t i = 0; i < count; ++i) {
      xl_ax_node node{};
      CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
      if (node.parent == parent && node.role == XL_AX_TEXT)
        return node.node;
    }
    CHECK(false);
    return xl_node_id{};
  };
  page("#p{margin-top:20px}#a,#b{padding-top:6px;padding-bottom:6px;position:"
       "relative}#a{z-index:9}#b{z-index:1}",
       "<p id=p><span id=a>AAAA</span><span id=b>BBBB</span></p>");
  document.attribute(document.query("#b"), "style",
                     "left:-" + std::to_string(bounds("#a").width) + "px");
  render();
  CHECK(pixel(int(bounds("#a").x + 2), int(bounds("#a").y + 2)) == 0xff0000ff);
  const auto a_text = text_node("#a"), b_text = text_node("#b");
  const auto ar = view.text_rects(a_text, 0, 4);
  CHECK(!ar.empty());
  CHECK(view.hit_text(ar[0].x + ar[0].width * .5f, ar[0].y + ar[0].height * .5f)
            .node == a_text);
  document.attribute(document.query("#a"), "style", "z-index:0");
  render();
  CHECK(view.hit_text(ar[0].x + ar[0].width * .5f, ar[0].y + ar[0].height * .5f)
            .node == b_text);
  compare_cold();
  // Line fragments retain their separate positions while an inline context
  // is promoted. Nested inline contexts may not escape that boundary.
  page("p{margin:20px "
       "0;width:110px}#a{position:relative;z-index:0;padding:4px;background:"
       "lime}#c{position:relative;z-index:9;background:red}#b{position:"
       "relative;z-index:1;display:block;top:-40px}",
       "<p><span id=a>one <span id=c>nested words wrap over multiple "
       "lines</span></span><span id=b></span></p>");
  compare_cold();
  // Anonymous text flex items participate at order zero between other items.
  page("#p{display:flex;width:150px}#p "
       "span{flex:none;margin-right:-40px;width:40px;height:40px}#a{order:-1}#"
       "b{order:1}",
       "<div id=p><span id=a></span>word<span id=b></span></div>");
  compare_cold();
  // Positioned descendants of inline-block participate in the outer context
  // unless that inline-block establishes its own real stacking context.
  page("p{margin:0}#p{display:inline-block;width:50px;height:40px;background:"
       "lime}#c{display:block;position:relative;z-index:9}#b{display:inline-"
       "block;position:relative;left:-50px;z-index:1}",
       "<p><span id=p><span id=c></span></span><span id=b></span></p>");
  CHECK(pixel(10, 10) == 0xff0000ff);
  document.attribute(document.query("#p"), "style",
                     "position:relative;z-index:0");
  render();
  CHECK(pixel(10, 10) == 0x0000ffff);
  compare_cold();
  const auto before = pixels();
  const auto layout_count = document.stats().layout_frames;
  view.scroll_to(0, 0, 0);
  render();
  CHECK(document.stats().layout_frames == layout_count);
  document.attribute(document.query("body"), "data-reflow", "1");
  render();
  CHECK(before == pixels());
  CHECK(view.render() == 0);
  page("#p{width:80px;height:45px;overflow:auto;border-radius:12px;border:2px "
       "solid "
       "black}#a{height:100px;position:relative;z-index:9;background:linear-"
       "gradient(red,blue)}#b{position:relative;top:-100px;z-index:1;height:"
       "100px}",
       "<div id=p><div id=a></div><div id=b></div></div>");
  const auto stats = document.stats();
  view.scroll_to(document.query("#p"), 0, 25);
  render();
  CHECK(document.stats().layout_frames == stats.layout_frames);
  const auto scrolled = pixels();
  document.attribute(document.query("body"), "data-reflow", "2");
  render();
  CHECK(scrolled == pixels());
  {
    xl::View cold(runtime, &options);
    cold.document(&document);
    if (gpu)
      cold.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
    render(&cold);
    cold.scroll_to(document.query("#p"), 0, 25);
    render(&cold);
    CHECK(scrolled == pixels(&cold));
  }
  page("#a{position:relative;z-index:1}#a:before{content:'front';position:"
       "relative;z-index:9;display:block;background:lime;width:50px;height:"
       "40px}#b{position:relative;top:-40px;z-index:2}");
  CHECK(pixel(10, 10) == 0x0000ffff);
  // A front box suppresses text hits into its covered sibling.
  page("#a{position:relative;z-index:1}#b{position:relative;z-index:2;top:-"
       "40px}",
       "<div id=a>covered</div><div id=b></div>");
  xl_text_position hidden{};
  CHECK(xl_view_hit_test_text(view.get(), 10, 10, &hidden, sizeof(hidden)) ==
        XL_NOT_FOUND);
  // Reusing cached paragraphs across navigation must preserve the new owners.
  for (int i = 0; i < 8; ++i) {
    overlap(i % 2 ? "z-index:9" : "z-index:0");
    CHECK(pixel(10, 10) == (i % 2 ? 0xff0000ff : 0x0000ffff));
  }
  if (gpu)
    CHECK(view.graphics_info().active == XL_GRAPHICS_SWIFTSHADER);
  std::cout << "stacking passed\n";
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
