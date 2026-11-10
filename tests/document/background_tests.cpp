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
      std::exit(1);                                                            \
    }                                                                          \
  } while (false)
int main(int argc, char **argv) {
  xl::Runtime runtime;
  xl::Document document(runtime);
  xl_view_options options;
  CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
  options.kind = XL_VIEW_OFFSCREEN;
  options.width = 240;
  options.height = 180;
  xl::View view(runtime, &options);
  view.document(&document);
  view.ax_updates(true);
  const bool gpu = argc == 2 && std::string_view(argv[1]) == "--gpu";
  if (gpu)
    view.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
  const auto render = [&](xl::View &target) {
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
  const auto pixels = [&](xl::View &target) {
    std::vector<uint8_t> out(240 * 180 * 4);
    CHECK(xl_view_copy_rgba(target.get(), out.data(), out.size(), 240 * 4) ==
          XL_OK);
    return out;
  };
  const auto pixel = [&](int x, int y) {
    const auto bytes = pixels(view);
    const auto i = (y * 240 + x) * 4;
    return uint32_t(bytes[i]) << 24 | uint32_t(bytes[i + 1]) << 16 |
           uint32_t(bytes[i + 2]) << 8 | bytes[i + 3];
  };
  const auto page = [&](std::string css,
                        std::string html = "<div id=x></div>") {
    document.html("<style>body{margin:0}#x{width:100px;height:100px}" + css +
                  "</style>" + html);
    render(view);
  };
  const auto near = [&](int x, int y, uint32_t expected, unsigned tolerance = 2,
                        const std::source_location &at =
                            std::source_location::current()) {
    const auto actual = pixel(x, y);
    for (unsigned shift : {0u, 8u, 16u, 24u})
      if (unsigned(std::abs(int((actual >> shift) & 255) -
                            int((expected >> shift) & 255))) > tolerance) {
        std::cerr << at.line() << " pixel " << x << ',' << y << " expected "
                  << std::hex << expected << " got " << actual << std::dec
                  << '\n';
        std::exit(1);
      }
  };
  page("#x{background:linear-gradient(red,blue)}");
  near(50, 0, 0xfe0001ff);
  near(50, 99, 0x0100feff);
  CHECK(view.render() == 0);
  // One-tile native path and a translated repeated tile's procedural path
  // must agree, including alpha, curved clips and modern interpolation.
  for(const auto* gradient:{"linear-gradient(to right,red,blue)",
                           "linear-gradient(in oklab,red,transparent)",
                           "radial-gradient(ellipse,red,blue)",
                           "conic-gradient(red,lime,blue,red)"}) {
    const auto rule=std::string("#x{border-radius:13px;background:")+gradient+";background-size:100px 100px;";
    page(rule+"background-position:0 0}");const auto direct=pixels(view);
    page(rule+"background-position:100px 0}");
    const auto shifted=pixels(view);unsigned maximum=0;size_t count=0;
    for(size_t i=0;i<direct.size();++i){maximum=std::max(maximum,unsigned(std::abs(int(direct[i])-int(shifted[i]))));count+=direct[i]!=shifted[i];}
    CHECK(maximum<=1);(void)count;
    page(rule+"background-position:-100px 0}");
    const auto negative=pixels(view);maximum=0;count=0;
    for(size_t i=0;i<direct.size();++i){maximum=std::max(maximum,unsigned(std::abs(int(direct[i])-int(negative[i]))));count+=direct[i]!=negative[i];}
    CHECK(maximum<=1);
  }
  page("#x{background-image:linear-gradient(to right,red,blue)}");
  near(0, 50, 0xfe0001ff);
  near(99, 50, 0x0100feff);
  page("#x{background:linear-gradient(to right,red 0 50%,blue 50% 100%)}");
  near(49, 50, 0xff0000ff);
  near(50, 50, 0x0000ffff);
  page("#x{background:repeating-linear-gradient(to right,red 0 10px,blue 10px "
       "20px)}");
  for (int x : {1, 21, 41, 81})
    near(x, 50, 0xff0000ff);
  for (int x : {11, 31, 51, 91})
    near(x, 50, 0x0000ffff);
  page("#x{background:linear-gradient(to right,red -50%,blue 50%)}");
  near(0, 50, 0x7e0081ff);
  near(50, 50, 0x0000ffff);
  page("#x{background:linear-gradient(to right,red,25%,blue)}");
  near(24, 50, 0x81007eff, 3);
  for (const int x : {1, 5, 13, 37, 73, 95}) {
    const auto blue = unsigned(std::lround(255 * std::sqrt((x + .5) / 100.)));
    near(x, 50, ((255 - blue) << 24) | (blue << 8) | 255, 2);
  }
  page("#x{background:linear-gradient(to right,red,transparent) lime}");
  near(49, 50, 0x817e00ff, 3);
  page("#x{background:linear-gradient(to right in oklab,red,blue)}");
  const auto modern = pixel(50, 50);
  page("#x{background:color-mix(in oklab,red 49.5%,blue 50.5%)}");
  near(50, 50, modern, 2);
  page("#x{background:radial-gradient(circle 50px at 50% 50%,red,blue)}");
  near(50, 50, 0xfb0004ff, 2);
  near(0, 0, 0x0000ffff);
  page("#x{background:radial-gradient(ellipse 50px 25px,red,blue)}");
  near(75, 50, 0x7d0082ff, 3);
  near(50, 75, 0x0000ffff);
  page("#x{background:repeating-radial-gradient(circle,red 0 10px,blue 10px "
       "20px)}");
  near(55, 50, 0xff0000ff);
  near(65, 50, 0x0000ffff);
  near(75, 50, 0xff0000ff);
  page("#x{background:conic-gradient(red 0deg 90deg,lime 90deg 180deg,blue "
       "180deg 270deg,yellow 270deg)}");
  near(80, 20, 0xff0000ff);
  near(80, 80, 0x00ff00ff);
  near(20, 80, 0x0000ffff);
  near(20, 20, 0xffff00ff);
  page("#x{background:conic-gradient(from 90deg,red 0deg 90deg,lime 90deg "
       "180deg,blue 180deg 270deg,yellow 270deg)}");
  near(80, 20, 0xffff00ff);
  page("#x{background:repeating-conic-gradient(red 0deg 45deg,blue 45deg "
       "90deg)}");
  near(60, 20, 0xff0000ff);
  near(80, 40, 0x0000ffff);
  page("#x{background:linear-gradient(red,red) center / 20px 20px no-repeat "
       "lime}");
  near(5, 5, 0x00ff00ff);
  near(45, 45, 0xff0000ff);
  near(61, 45, 0x00ff00ff);
  page("#x{background:linear-gradient(red,red) left top / 30px 20px space "
       "no-repeat blue}");
  near(5, 5, 0xff0000ff);
  near(32, 5, 0x0000ffff);
  near(37, 5, 0xff0000ff);
  near(67, 5, 0x0000ffff);
  near(75, 5, 0xff0000ff);
  page("#x{background:linear-gradient(red,red) left top / 20px 20px no-repeat "
       "content-box blue;padding:10px;border:10px solid black}");
  near(5, 20, 0x000000ff);
  near(135, 135, 0x000000ff);
  near(11, 11, 0xffffffff);
  near(21, 21, 0xff0000ff);
  near(45, 21, 0x0000ffff);
  page("#x{background:linear-gradient(rgb(255 0 0 / .5),rgb(255 0 0 / "
       ".5)),linear-gradient(blue,blue) lime}");
  near(50, 50, 0x80007fff);
  page("#x{background-image:linear-gradient(blue,blue);background:red}");
  near(50, 50, 0xff0000ff);
  page("@layer base,top;@layer "
       "base{#x{background:linear-gradient(red,red)}}@layer "
       "top{#x{background-image:linear-gradient(blue,blue);background:revert-"
       "layer}}");
  near(50, 50, 0xff0000ff);
  page("#x{background:lime;background-image:linear-gradient(transparent,"
       "transparent)}");
  near(50, 50, 0x00ff00ff);
  page("#x{background-image:linear-gradient(red,red)!important;background:"
       "blue}");
  near(50, 50, 0xff0000ff);
  page("#x{background-image:linear-gradient(red,red);background:var(--missing)"
       "}");
  near(50, 50, 0xffffffff);
  page("#x{background:linear-gradient(red,red);background:linear-gradient(bad,"
       "blue)}");
  near(50, 50, 0xff0000ff);
  page("#parent{background:linear-gradient(red,red)}#x{background:inherit}",
       "<div id=parent><div id=x></div></div>");
  near(50, 50, 0xff0000ff);
  page("#parent{background:linear-gradient(red,red)}#x{background-image:"
       "inherit;background-color:blue}",
       "<div id=parent><div id=x></div></div>");
  near(50, 50, 0xff0000ff);
  page("#x{--g:linear-gradient(red,blue);background:var(--g)}");
  const auto original = pixels(view);
  xl::AXUpdate old, batch;
  while (view.take_ax_update(batch))
    old = std::move(batch);
  CHECK(old);
  document.attribute(document.query("#x"), "style",
                     "--g:linear-gradient(blue,red)");
  render(view);
  CHECK(pixels(view) != original);
  const auto changed = pixels(view);
  CHECK(xl_view_invalidate(view.get(), 0, 0, 240, 180) == XL_OK);
  render(view);
  CHECK(pixels(view) == changed);
  page("#x{background:linear-gradient(to right in "
       "srgb,currentColor,light-dark(red,blue));color:lime;color-scheme:dark}");
  near(99, 50, 0x0001feff);
  page("#x{background:linear-gradient(to "
       "right,red,blue);background-size:10000000px 10000000px}");
  near(50, 50, 0xff0000ff);
  page("#x{background:linear-gradient(red,blue);border-radius:30px;border:3px "
       "solid black}");
  near(0, 0, 0xffffffff);
  const auto rounded = pixels(view);
  for (int x = 0; x < 110; x += 3) {
    CHECK(xl_view_invalidate(view.get(), x, 0, 1, 120) == XL_OK);
    render(view);
    CHECK(pixels(view) == rounded);
  }
  page("#x{height:400px;background:linear-gradient(red,blue) fixed}");
  const auto fixed = pixel(50, 50);
  const auto frames = document.stats().layout_frames;
  view.scroll_to(0, 0, 40);
  render(view);
  near(50, 50, fixed);
  CHECK(document.stats().layout_frames == frames);
  page("#x{overflow:auto;background:linear-gradient(red,blue) "
       "local}#content{height:300px}",
       "<div id=x><div id=content></div></div>");
  const auto local_before = pixel(50, 10);
  const auto layouts = document.stats().layout_frames;
  view.scroll_to(document.query("#x"), 0, 60);
  render(view);
  CHECK(pixel(50, 10) != local_before &&
        document.stats().layout_frames == layouts);
  const auto local_after = pixels(view);
  document.attribute(document.query("body"), "data-oracle", "one");
  render(view);
  CHECK(pixels(view) == local_after);
  page("body{height:400px;background:linear-gradient(red,blue)}");
  const auto canvas_before = pixel(50, 50);
  view.scroll_to(0, 0, 40);
  render(view);
  CHECK(pixel(50, 50) != canvas_before);
  const auto projected = pixels(view);
  document.attribute(document.query("body"), "data-oracle", "two");
  render(view);
  CHECK(pixels(view) == projected);
  xl_resource_stats resources{};
  CHECK(xl_runtime_resource_stats(runtime.get(),&resources,sizeof(resources))==XL_OK);
  CHECK(resources.decompressions==0&&resources.worker_threads==0);
  std::ifstream font_file(XL_TEST_FONT, std::ios::binary);
  const std::vector<char> font{std::istreambuf_iterator<char>(font_file),
                               std::istreambuf_iterator<char>()};
  CHECK(!font.empty());
  CHECK(xl_document_add_font(document.get(), "test", font.data(),
                             font.size()) == XL_OK);
  page("#x{width:150px}#atom{display:inline-block;width:90px;height:50px;"
       "overflow:auto;background:linear-gradient(red,blue) "
       "local;border-radius:12px}#inside{height:200px}",
       "<p id=x>Before <span id=atom><span id=inside "
       "style='display:block'>Content</span></span> After</p>");
  const auto atom = document.query("#atom");
  const auto atom_before = pixels(view);
  view.scroll_to(atom, 0, 45);
  render(view);
  CHECK(pixels(view) != atom_before);
  const auto atom_after = pixels(view);
  document.attribute(document.query("body"), "data-oracle", "atomic");
  render(view);
  CHECK(pixels(view) == atom_after);
  page("#x{font:30px test}span{background:linear-gradient(to "
       "right,red,blue);border-radius:5px}",
       "<div id=x><span>Inline</span> text</div>");
  bool colored = false;
  const auto inline_pixels = pixels(view);
  for (size_t i = 0; i < inline_pixels.size(); i += 4)
    colored |= inline_pixels[i] > 30 && inline_pixels[i + 2] > 30 &&
               inline_pixels[i + 1] < 10;
  CHECK(colored);
  CHECK(view.render() == 0);
  if (argc == 2 && !gpu) {
    std::ifstream input(argv[1], std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>()};
    CHECK(!html.empty());
    document.html(html);
    render(view);
    for (int i = 1; i <= 4; ++i) {
      const auto selector =
          ".gradient-grid .grad:nth-child(" + std::to_string(i) + ")";
      const auto node = document.query(selector);
      CHECK(node);
      size_t needed = 0;
      auto status = xl_document_get_style(
          document.get(), node, "background-image", nullptr, 0, &needed);
      CHECK(status == XL_BUFFER_TOO_SMALL);
      std::string value(needed, '\0');
      CHECK(xl_document_get_style(document.get(), node, "background-image",
                                  value.data(), value.size(),
                                  &needed) == XL_OK);
      CHECK(value.find("gradient(") != std::string::npos);
      xl_node_bounds bounds;
      CHECK(xl_view_node_bounds(view.get(), node, &bounds) == XL_OK);
      view.scroll_to(0, 0, std::max(0.f, bounds.y - 20));
      render(view);
      const auto frames = document.stats().layout_frames;
      view.scroll_to(0, 0, view.scroll(0).y + 5);
      render(view);
      CHECK(document.stats().layout_frames == frames);
      const auto projected = pixels(view);
      document.attribute(document.query("body"), "data-gradient-oracle",
                         std::to_string(i));
      render(view);
      CHECK(pixels(view) == projected);
      CHECK(view.render() == 0);
    }
  }
  if (gpu)
    CHECK(view.graphics_info().active == XL_GRAPHICS_SWIFTSHADER);
  std::cout << "Gradient pixels, layers, cascade, alpha, clips, inline, "
               "retained damage and scroll projection passed\n";
}
