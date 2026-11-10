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
  if (argc == 2 && std::string_view(argv[1]) == "--gpu")
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
  const auto bounds = [&](const char *selector) {
    xl_node_bounds value{};
    CHECK(xl_view_node_bounds(view.get(), document.query(selector), &value) ==
          XL_OK);
    return value;
  };
  const auto ax_bounds = [&](const char *selector) {
    const auto id = document.query(selector);
    size_t count;
    CHECK(id && xl_view_ax_count(view.get(), &count) == XL_OK);
    for (size_t i = 0; i < count; ++i) {
      xl_ax_node node{};
      CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
      if (node.node == id)
        return node.bounds;
    }
    CHECK(false);
    return xl_node_bounds{};
  };
  document.html(
      "<style>body{margin:0}div{width:40px;height:20px}"
      "#a{position:relative;left:30px;top:10px;background:red}#b{background:"
      "blue}"
      "</style><div id=a role=group></div><div id=b role=group></div>");
  render();
  if (argc == 2 && std::string_view(argv[1]) == "--gpu")
    CHECK(view.graphics_info().active == XL_GRAPHICS_SWIFTSHADER);
  CHECK(std::abs(bounds("#a").x - 30) < .02f &&
        std::abs(bounds("#a").y - 10) < .02f);
  CHECK(std::abs(bounds("#b").y - 20) < .02f);
  CHECK(std::abs(ax_bounds("#a").x - 30) < .02f &&
        std::abs(ax_bounds("#a").y - 10) < .02f);
  const auto image = pixels();
  CHECK(image[(15 * 240 + 35) * 4] == 255 &&
        image[(15 * 240 + 35) * 4 + 1] == 0);
  CHECK(view.render() == 0);
  const auto near = [&](float actual, float expected,
                        const std::source_location &at =
                            std::source_location::current()) {
    if (std::abs(actual - expected) > .03f) {
      std::cerr << at.line() << " expected " << expected << " got " << actual
                << '\n';
      throw std::runtime_error("geometry mismatch");
    }
  };
  const auto equal = [&](const xl_node_bounds &a, const xl_node_bounds &b,
                         float dx = 0, float dy = 0) {
    near(a.x, b.x + dx);
    near(a.y, b.y + dy);
    near(a.width, b.width);
    near(a.height, b.height);
  };
  const auto page = [&](std::string css, std::string html =
                                             "<div id=p><div id=a></div><div "
                                             "id=b></div></div>") {
    document.html(
        "<style>body{margin:0;font:16px/20px test}#p{width:200px;height:100px}"
        "#a,#b{width:40px;height:20px}" +
        css + "</style>" + html);
    render();
  };
  struct Case {
    const char *css;
    float x, y;
  };
  for (const auto &test : std::vector<Case>{
           {"position:relative;left:10%;top:25%", 20, 25},
           {"position:relative;right:-15px;bottom:-6px", 15, 6},
           {"position:relative;inset:9px 12px 4px 7px", 7, 9},
           {"position:relative;inset:auto 8px 3px", 8, -3},
           {"position:relative;inset:1px 2px;left:16px", 16, 1},
           {"position:relative;left:19px;inset:3px 4px", 4, 3},
           {"position:relative;left:calc(10% + 2 * 3px);top:calc(1em - 12px)",
            26, 4},
           {"position:relative;inset:initial;left:8px;left:nonsense", 8, 0},
           {"position:relative;left:8px;left:var(--missing)", 0, 0},
           {"position:relative;--x:17px;left:var(--x)", 17, 0},
           {"position:relative;left:8px;inset:var(--missing)", 0, 0},
           {"position:static;left:50px;top:50px", 0, 0},
           {"position:relative;left:9px!important;inset:3px", 9, 3},
           {"position:relative;inset:3px!important;left:9px", 3, 3},
           {"position:relative;left:9px;left:4px 8px", 9, 0},
           {"position:relative;left:9px;inset:inherit 8px", 9, 0},
           {"position:relative;left:9px;inset:1px 2px 3px 4px 5px", 9, 0},
           {"position:r\\65 lative;left:1e1px;top:2px", 10, 2}}) {
    page("#a{" + std::string(test.css) + "}");
    near(bounds("#a").x, test.x);
    near(bounds("#a").y, test.y);
    near(bounds("#b").y, 20);
  }
  page("#p{direction:rtl}#a{direction:ltr;position:relative;left:9px;right:"
       "7px}");
  near(bounds("#a").x, -7);
  page("#p{height:auto;margin-top:20px}#a{position:relative;top:25%;bottom:"
       "5px}");
  near(bounds("#a").y, 15);
  for (const auto *top : {"0%", "calc(5px + 0%)", "calc(10% - 10%)"}) {
    page(std::string(
             "#p{height:auto;margin-top:20px}#a{position:relative;top:") +
         top + ";bottom:5px}");
    near(bounds("#a").y, 15);
  }
  page("#p{height:auto;position:relative;top:0%}#a{position:relative;top:"
       "inherit;bottom:5px}");
  near(bounds("#a").y, -5);
  page("#p{position:relative;left:1em;font-size:20px}#a{position:inherit;left:"
       "inherit;font-size:10px}");
  near(bounds("#p").x, 20);
  near(bounds("#a").x, 40);
  near(bounds("#b").x, 20);
  page("@layer base,top;@layer base{#a{position:relative;left:11px}}@layer "
       "top{#a{left:18px;left:revert-layer}}");
  near(bounds("#a").x, 11);
  char style_value[64];
  size_t style_size;
  CHECK(xl_document_get_style(document.get(), document.query("#a"), "left",
                              style_value, sizeof(style_value),
                              &style_size) == XL_OK);
  CHECK(std::string_view(style_value, style_size) == "11px");
  page("#p{display:flex;gap:10px}#a{position:relative;left:20px;top:6px}");
  near(bounds("#a").x, 20);
  near(bounds("#a").y, 6);
  near(bounds("#b").x, 50);
  near(bounds("#b").y, 0);
  page("#p{display:grid;grid-template-columns:50px "
       "50px;gap:10px}#a{position:relative;left:10%;top:6px}");
  near(bounds("#a").x, 5);
  near(bounds("#a").y, 6);
  near(bounds("#b").x, 60);
  page("#p{width:60px;height:40px;overflow:auto}#a{width:100px;position:"
       "relative;left:-50px}");
  near(view.scroll(document.query("#p")).max_x, 40);
  page("#p{width:60px;height:40px;overflow:auto}#a{position:relative;left:60px;"
       "top:50px}");
  near(view.scroll(document.query("#p")).max_x, 40);
  near(view.scroll(document.query("#p")).max_y, 30);
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
  page("#p{width:120px;height:auto}#s{background:lime;border-radius:4px}",
       "<div id=p><span id=s role=group>office words that wrap over "
       "lines</span><span id=t role=group>end</span></div>");
  const auto s = text_node("#s"), t = text_node("#t");
  const auto old = view.text_rects(s, 0, 33), tail = view.text_rects(t, 0, 3);
  CHECK(old.size() >= 2);
  const auto parent = bounds("#p"), span = bounds("#s");
  const xl_text_position first{sizeof(first), 0, s, XL_TEXT_DOWNSTREAM, 0},
      last{sizeof(last), 33, s, XL_TEXT_UPSTREAM, 0};
  const auto caret = view.caret(first);
  document.attribute(document.query("#s"), "style",
                     "position:relative;left:11px;top:7px");
  render();
  const auto shifted = view.text_rects(s, 0, 33);
  CHECK(shifted.size() == old.size());
  for (size_t i = 0; i < old.size(); ++i)
    equal(shifted[i], old[i], 11, 7);
  equal(bounds("#p"), parent);
  equal(bounds("#s"), span, 11, 7);
  equal(view.caret(first), caret, 11, 7);
  const auto after_tail = view.text_rects(t, 0, 3);
  CHECK(after_tail.size() == tail.size());
  for (size_t i = 0; i < tail.size(); ++i)
    equal(after_tail[i], tail[i]);
  for (const auto &rect : shifted)
    CHECK(view.hit_text(rect.x + rect.width * .5f, rect.y + rect.height * .5f)
              .node == s);
  view.selection(first, last);
  CHECK(view.selected_text() == "office words that wrap over lines");
  const auto selection = view.selection_rects();
  CHECK(selection.size() == shifted.size());
  for (size_t i = 0; i < selection.size(); ++i)
    equal(selection[i], shifted[i]);
  view.clear_selection();
  render();
  const auto compare_relayout = [&] {
    const auto before = pixels();
    document.attribute(document.query("body"), "data-layout-oracle",
                       std::to_string(document.stats().revision));
    render();
    CHECK(before == pixels());
    xl::View cold(runtime, &options);
    cold.document(&document);
    cold.ax_updates(true);
    if (argc == 2 && std::string_view(argv[1]) == "--gpu")
      cold.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
    render(&cold);
    const auto scroll = view.scroll();
    cold.scroll_to(0, scroll.x, scroll.y);
    render(&cold);
    // Inner scroll offsets belong to each View; callers below compare a
    // cold surface separately when they intentionally scroll a container.
    xl_scroll_info inner{};
    const auto status = xl_view_get_scroll(view.get(), document.query("#p"),
                                           &inner, sizeof(inner));
    CHECK(status == XL_OK || status == XL_NOT_FOUND);
    if (status == XL_NOT_FOUND || inner.y == 0)
      CHECK(before == pixels(&cold));
  };
  compare_relayout();
  page(
      "#p{width:150px;height:auto}#s{position:relative;left:12px;top:3px}#n{"
      "position:relative;left:-4px;top:5px}",
      "<div id=p><span id=s role=group>A<span id=n "
      "role=group>nested</span>Z</span><span id=t role=group>end</span></div>");
  const auto nested = text_node("#n");
  const auto r = view.text_rects(nested, 0, 6);
  CHECK(!r.empty());
  const auto nested_before = r;
  document.attribute(document.query("#s"), "style", "position:static");
  render();
  const auto nested_after = view.text_rects(nested, 0, 6);
  CHECK(nested_after.size() == nested_before.size());
  for (size_t i = 0; i < nested_after.size(); ++i)
    equal(nested_before[i], nested_after[i], 12, 3);
  page("#p{margin:20px "
       "10px;width:120px;height:auto}#s{background:lime;border:1px solid "
       "blue}",
       "<div id=p><span id=s>office e\xcc\x81 words that wrap over "
       "lines</span></div>");
  const auto original_image = pixels();
  document.attribute(document.query("#s"), "style",
                     "position:relative;left:11px;top:7px");
  render();
  const auto translated_image = pixels();
  size_t moved_ink = 0;
  for (int y = 0; y < 160; ++y)
    for (int x = 0; x < 240; ++x)
      for (int channel = 0; channel < 4; ++channel) {
        const auto expected =
            x >= 11 && y >= 7
                ? original_image[((y - 7) * 240 + x - 11) * 4 + channel]
                : uint8_t(255);
        if (translated_image[(y * 240 + x) * 4 + channel] != expected) {
          std::cerr << "translated pixel " << x << ',' << y << " channel "
                    << channel << " expected " << unsigned(expected) << " got "
                    << unsigned(translated_image[(y * 240 + x) * 4 + channel])
                    << '\n';
          throw std::runtime_error("pixel translation mismatch");
        }
        moved_ink += channel == 0 &&
                     translated_image[(y * 240 + x) * 4 + channel] != 255;
      }
  CHECK(moved_ink > 100);
  compare_relayout();
  page(
      "#p{width:150px;height:auto}#s{position:relative;left:9px;top:6px}#a{"
      "display:inline-block;position:relative;left:5px;top:7px;background:red}",
      "<div id=p><span id=s role=group>Hi<div id=a role=group><span "
      "role=group>box</span></div></span><span id=t "
      "role=group>end</span></div>");
  const auto atomic = bounds("#a"), atomic_tail = bounds("#t");
  const auto line_height = bounds("#p").height;
  const auto atomic_pixels = pixels();
  const auto atomic_point = (int(atomic.y + atomic.height - 2) * 240 +
                             int(atomic.x + atomic.width - 2)) *
                            4;
  CHECK(atomic_pixels[atomic_point] == 255 &&
        atomic_pixels[atomic_point + 1] == 0);
  document.attribute(document.query("#s"), "style", "position:static");
  document.attribute(document.query("#a"), "style", "position:static");
  render();
  equal(atomic, bounds("#a"), 14, 13);
  equal(atomic_tail, bounds("#t"));
  near(bounds("#p").height, line_height);
  page("#p{width:120px;height:40px;overflow:auto;position:relative;left:15px;"
       "top:10px;border-radius:10px}"
       "#a{height:120px;position:relative;left:20px;top:12px;background:linear-"
       "gradient(red,blue)}");
  const auto before_stats = document.stats();
  view.scroll_to(document.query("#p"), 0, 22);
  render();
  CHECK(document.stats().layout_frames == before_stats.layout_frames);
  compare_relayout();
  {
    xl::View cold(runtime, &options);
    cold.document(&document);
    if (argc == 2 && std::string_view(argv[1]) == "--gpu")
      cold.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
    render(&cold);
    cold.scroll_to(document.query("#p"), 0, 22);
    render(&cold);
    CHECK(pixels() == pixels(&cold));
  }
  CHECK(view.render() == 0);
  page("#p{width:120px;height:100px}#a{display:block;padding:0;border:0;"
       "position:relative;left:25px;top:15px;"
       "width:30px;height:30px;background:red;border-radius:50%}",
       "<div id=p><button id=a aria-label=target></button></div>");
  CHECK(document.evaluate("globalThis.clicks=0;document.getElementById('a')."
                          "onclick=()=>clicks++;'ready'") == XL_OK);
  const auto click = [&](float x, float y) {
    const xl_pointer_event down{sizeof(down), XL_POINTER_DOWN, 0, 0, x, y},
        up{sizeof(up), XL_POINTER_UP, 0, 0, x, y};
    view.pointer(down);
    render();
    view.pointer(up);
    render();
  };
  click(3, 3);
  click(26, 16);
  CHECK(document.evaluate("clicks") == XL_OK && document.result() == "0");
  click(40, 30);
  CHECK(document.evaluate("clicks") == XL_OK && document.result() == "1");
  equal(ax_bounds("#a"), bounds("#a"));
  document.attribute(document.query("#a"), "style",
                     "position:relative;left:5px;top:6px");
  render();
  near(bounds("#a").x, 5);
  near(bounds("#a").y, 6);
  document.attribute(document.query("#a"), "style", "");
  render();
  near(bounds("#a").x, 25);
  near(bounds("#a").y, 15);
  // Default z-index stacking contexts and out-of-flow/sticky positioning are
  // separate pending stages; these checks exercise relative geometry only.
  std::cout << "relative positioning passed\n";
} catch (const std::exception &error) {
  std::cerr << "unexpected exception: " << error.what() << '\n';
  return 1;
}
