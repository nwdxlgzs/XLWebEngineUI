#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
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
  const auto render = [&] {
    xl_status status;
    uint32_t flags;
    const auto end =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
      CHECK(std::chrono::steady_clock::now() < end);
      std::this_thread::yield();
    }
    CHECK(status == XL_OK);
  };
  document.html("<style>body{margin:0}#p{position:relative;width:100px;height:"
                "80px;padding:10px;border:2px solid black}"
                "#a{position:absolute;left:20px;top:15px;width:30px;height:"
                "25px;background:red}#b{width:20px;height:10px;background:blue}"
                "</style><div id=p><div id=a></div><div id=b></div></div>");
  render();
  xl_node_bounds a{}, b{};
  CHECK(xl_view_node_bounds(view.get(), document.query("#a"), &a) == XL_OK);
  CHECK(xl_view_node_bounds(view.get(), document.query("#b"), &b) == XL_OK);
  CHECK(std::abs(a.x - 22) < .02f && std::abs(a.y - 17) < .02f &&
        std::abs(b.y - 12) < .02f);
  if (gpu)
    CHECK(view.graphics_info().active == XL_GRAPHICS_SWIFTSHADER);
  const auto near = [](float actual, float expected,
                       const std::source_location &at =
                           std::source_location::current()) {
    if (std::abs(actual - expected) > .04f) {
      std::cerr << at.line() << " expected " << expected << " got " << actual
                << '\n';
      throw std::runtime_error("geometry mismatch");
    }
  };
  const auto bounds = [&](const char *selector) {
    xl_node_bounds value{};
    CHECK(xl_view_node_bounds(view.get(), document.query(selector), &value) ==
          XL_OK);
    return value;
  };
  const auto ax = [&](const char *selector) {
    const auto id = document.query(selector);
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    for (size_t i = 0; i < count; ++i) {
      xl_ax_node node{};
      CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
      if (node.node == id)
        return node.bounds;
    }
    CHECK(false);
    return xl_node_bounds{};
  };
  const auto pixels = [&] {
    std::vector<uint8_t> bytes(240 * 160 * 4);
    CHECK(xl_view_copy_rgba(view.get(), bytes.data(), bytes.size(), 240 * 4) ==
          XL_OK);
    return bytes;
  };
  const auto pixel = [&](int x, int y) {
    const auto bytes = pixels();
    const auto i = (y * 240 + x) * 4;
    return uint32_t(bytes[i]) << 24 | uint32_t(bytes[i + 1]) << 16 |
           uint32_t(bytes[i + 2]) << 8 | bytes[i + 3];
  };
  const auto page =
      [&](std::string css,
          std::string html =
              "<div id=p><div id=a role=button aria-label=absolute></div><div "
              "id=b></div></div>") {
        document.html("<style>body{margin:0;font:16px/20px "
                      "test}#p{position:relative;width:200px;height:100px}#a{"
                      "position:absolute;width:40px;height:20px;background:red}"
                      "#b{width:20px;height:10px;background:blue}" +
                      css + "</style>" + html);
        render();
      };
  page("#p{height:auto}#a{top:15px;left:20px}");
  near(bounds("#p").height, 10);
  near(bounds("#b").y, 0);
  near(bounds("#a").y, 15);
  near(ax("#a").x, 20);
  CHECK(pixel(21, 16) == 0xff0000ff);
  CHECK(view.render() == 0);
  struct Case {
    const char *css;
    float x, y, width, height;
  };
  for (const auto &c : std::vector<Case>{
           {"left:10%;top:25%;width:50%;height:50%", 20, 25, 100, 50},
           {"right:10px;bottom:5px", 150, 75, 40, 20},
           {"left:10px;right:20px;width:auto;top:5px;bottom:15px;height:auto",
            10, 5, 170, 80},
           {"inset:0;margin:auto", 80, 40, 40, 20},
           {"left:10px;right:20px;width:40px;margin-left:auto", 140, 0, 40, 20},
           {"left:10px;right:20px;width:auto;max-width:80px", 10, 0, 80, 20},
           {"left:10px;right:20px;width:auto;min-width:190px", 10, 0, 190, 20},
           {"left:calc(10% + 3px);top:calc(25% - 5px)", 23, 20, 40, 20},
           {"top:0%;bottom:0%;height:auto;min-height:120px", 0, 0, 40, 120},
           {"left:10px;top:10px;margin:5%", 20, 20, 40, 20},
           {"left:10px;right:10px;width:auto;padding:5px;border:2px solid "
            "black;box-sizing:border-box",
            10, 0, 180, 20},
           {"inset:10px;width:auto;height:auto;padding:5px;border:2px solid "
            "black",
            10, 10, 180, 80}}) {
    page("#a{" + std::string(c.css) + "}");
    const auto r = bounds("#a");
    near(r.x, c.x);
    near(r.y, c.y);
    near(r.width, c.width);
    near(r.height, c.height);
    near(bounds("#b").y, 0);
  }
  page("#p{direction:rtl}#a{left:10px;right:20px;direction:ltr}");
  near(bounds("#a").x, 140);
  page("#p{direction:rtl}#a{left:10px;right:20px;width:auto;max-width:80px}");
  near(bounds("#a").x, 100);
  page("#p{direction:rtl}#a{inset:0;margin:auto;width:220px}");
  near(bounds("#a").x, -20);
  page("#a{inset:0;margin:auto;width:220px;height:120px}");
  near(bounds("#a").x, 0);
  near(bounds("#a").y, -10);
  page("#p{padding:10px;border:2px solid "
       "black}#a{left:10%;top:25%;width:50%;height:50%}");
  near(bounds("#a").x, 24);
  near(bounds("#a").y, 32);
  near(bounds("#a").width, 110);
  near(bounds("#a").height, 60);
  page("#p{position:static;padding:10px;border:2px solid "
       "black;margin:20px}#a{right:0;bottom:0}");
  near(bounds("#a").x, 200);
  near(bounds("#a").y, 140);
  page("#p{padding:10px;border:2px solid "
       "black}#a{position:fixed;right:0;bottom:0}");
  near(bounds("#a").x, 200);
  near(bounds("#a").y, 140);
  // Fixed descendants escape both viewport scrolling and intermediate clips.
  page("#p{width:100px;height:40px;overflow:auto;border-radius:12px}#b{height:"
       "300px}#a{position:fixed;left:140px;top:10px}");
  near(ax("#a").width, 40);
  CHECK(pixel(145, 15) == 0xff0000ff);
  const auto fixed_before = bounds("#a");
  const auto old = document.stats();
  view.scroll_to(document.query("#p"), 0, 25);
  render();
  near(bounds("#a").x, fixed_before.x);
  near(bounds("#a").y, fixed_before.y);
  CHECK(document.stats().layout_frames == old.layout_frames);
  CHECK(pixel(145, 15) == 0xff0000ff);
  const auto compare_relayout = [&] {
    const auto before = pixels();
    const auto rectangle = ax("#a");
    const auto stats = document.stats();
    document.attribute(document.query("body"), "data-reflow",
                       std::to_string(stats.layout_frames));
    render();
    CHECK(before == pixels());
    near(ax("#a").x, rectangle.x);
    near(ax("#a").y, rectangle.y);
    near(ax("#a").width, rectangle.width);
    near(ax("#a").height, rectangle.height);
    CHECK(document.stats().layout_frames == stats.layout_frames + 1);
  };
  compare_relayout();
  page("#p{height:300px}#a{position:fixed;top:10px;left:15px}");
  view.scroll_to(0, 0, 50);
  render();
  near(bounds("#a").y, 10);
  compare_relayout();
  page("#p{height:40px;overflow:auto}#a{left:10px;top:70px}#b{height:200px}");
  near(bounds("#a").y, 70);
  view.scroll_to(document.query("#p"), 0, 50);
  render();
  near(bounds("#a").y, 20);
  near(ax("#a").y, 20);
  compare_relayout();
  // A static clip between the containing block and absolute box does not clip
  // it.
  page("#clip{width:20px;height:20px;overflow:hidden}#a{left:100px;top:0}",
       "<div id=p><div id=clip><div id=a role=button "
       "aria-label=a></div></div></div>");
  CHECK(pixel(105, 5) == 0xff0000ff);
  near(ax("#a").width, 40);
  // Fixed overflow cannot enlarge the document scroll range.
  page("#p{height:10px}#a{position:fixed;left:400px;top:400px}");
  near(view.scroll().max_x, 0);
  near(view.scroll().max_y, 0);
  near(ax("#a").width, 0);
  near(ax("#a").height, 0);
  page("#a{position:fixed;left:220px;top:10px}");
  near(bounds("#a").width, 40);
  near(ax("#a").width, 20);
  // Absolute children do contribute to their containing block's scrollable
  // overflow.
  page("#p{width:100px;height:40px;overflow:auto}#a{left:150px;top:70px}");
  CHECK(view.scroll(document.query("#p")).max_x >= 90);
  CHECK(view.scroll(document.query("#p")).max_y >= 50);
  for (const auto *display : {"flex", "grid"}) {
    page("#p{display:" + std::string(display) +
         ";height:auto}#a{width:1000px;height:300px}");
    near(bounds("#p").height, 10);
    near(bounds("#b").x, 0);
    near(bounds("#b").y, 0);
  }
  page("#p{display:flex;justify-content:center;align-items:center}");
  near(bounds("#a").x, 80);
  near(bounds("#a").y, 40);
  page("#p{display:flex;flex-direction:row-reverse;justify-content:flex-start;"
       "align-items:flex-end}");
  near(bounds("#a").x, 160);
  near(bounds("#a").y, 80);
  page("#p{display:flex;flex-direction:column;justify-content:center;align-"
       "items:flex-end}");
  near(bounds("#a").x, 160);
  near(bounds("#a").y, 40);
  page("#p{display:grid;grid-template-columns:60px 1fr;grid-template-rows:30px "
       "1fr;gap:10px}#a{grid-column:2;grid-row:2;inset:0;width:auto;height:"
       "auto}");
  near(bounds("#a").x, 70);
  near(bounds("#a").y, 40);
  near(bounds("#a").width, 130);
  near(bounds("#a").height, 60);
  page("#p{display:grid;padding:10px;border:2px solid "
       "black;grid-template-columns:60px "
       "1fr;gap:10px}#a{inset:0;width:auto;height:auto}");
  near(bounds("#a").x, 2);
  near(bounds("#a").width, 220);
  near(bounds("#a").height, 120);
  page("#p{display:grid;grid-template-columns:[first] 60px [second] 1fr "
       "[last];gap:10px}#a{grid-column:second / "
       "last;left:0;right:0;width:auto}");
  near(bounds("#a").x, 70);
  near(bounds("#a").width, 130);
  // Nested containing blocks and fixed subtrees keep independent origins.
  page("#a{left:20px;top:15px;padding:5px}#c{position:absolute;right:0;bottom:"
       "0;width:10px;height:10px;background:lime}",
       "<div id=p><div id=a><div id=c></div></div></div>");
  near(bounds("#c").x, 60);
  near(bounds("#c").y, 35);
  page("#p{position:static}#in{position:relative}#a{left:10px;top:5px}",
       "<div id=p><span id=in><span id=a></span></span></div>");
  near(bounds("#a").x, 10);
  near(bounds("#a").y, 5);
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
  page("#a{width:auto;top:0;right:0}",
       "<div id=p><div id=a>one two three</div><div id=b></div></div>");
  CHECK(bounds("#a").width > 30 && bounds("#a").width < 200);
  near(bounds("#a").x + bounds("#a").width, 200);
  near(bounds("#b").y, 0);
  page("#a{display:inline;left:auto;top:auto}",
       "<div id=p>abc<span id=a></span>def</div>");
  CHECK(bounds("#a").x > 10);
  near(bounds("#a").y, 0);
  const auto inline_origin = bounds("#a").x;
  document.attribute(document.query("#a"), "style",
                     "position:static;display:none");
  render();
  CHECK(bounds("#p").height == 100);
  page("#p{position:static}#in{position:relative;padding:4px;border:2px solid "
       "black}#a{left:0;top:0}",
       "<div id=p><span id=in>abc<span id=a></span>def</span></div>");
  near(bounds("#a").x, bounds("#in").x + 2);
  near(bounds("#a").y, bounds("#in").y + 2);
  page("#p{position:static}#in{display:inline-block;position:relative;padding:"
       "4px;border:2px solid black}#a{right:0;bottom:0;width:10px;height:10px}",
       "<div id=p>prefix<span id=in>abc<span id=a></span></span>tail</div>");
  near(bounds("#a").x + bounds("#a").width,
       bounds("#in").x + bounds("#in").width - 2);
  near(bounds("#a").y + bounds("#a").height,
       bounds("#in").y + bounds("#in").height - 2);
  CHECK(inline_origin > 0);
  std::cout << "absolute positioning passed\n";
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
