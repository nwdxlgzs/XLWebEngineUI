#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>
#include <vector>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      std::cerr << __LINE__ << ": " #x "\n";                                   \
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
  options.height = 180;
  xl::View view(runtime, &options);
  view.document(&document);
  view.ax_updates(true);
  const bool gpu = argc == 2 && std::string_view(argv[1]) == "--gpu";
  if (gpu)
    view.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
  const auto render = [&] {
    xl_status status;
    uint32_t flags;
    const auto until =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
      CHECK(std::chrono::steady_clock::now() < until);
      std::this_thread::yield();
    }
    CHECK(status == XL_OK);
  };
  document.html("<style>body{margin:0}svg{display:block;width:40px;height:30px}"
                "</style><svg id=icon viewBox='0 0 40 30' role=img "
                "aria-label=red><rect width=40 height=30 fill=red  /></svg>");
  render();
  std::vector<uint8_t> first_pixels(240 * 180 * 4);
  CHECK(xl_view_copy_rgba(view.get(), first_pixels.data(), first_pixels.size(),
                          240 * 4) == XL_OK);
  CHECK(first_pixels[(10 * 240 + 10) * 4] == 255 &&
        first_pixels[(10 * 240 + 10) * 4 + 1] == 0 &&
        first_pixels[(10 * 240 + 10) * 4 + 2] == 0);
  CHECK(view.render() == 0);
  const auto pixels = [&] {
    std::vector<uint8_t> bytes(240 * 180 * 4);
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
  const auto bounds = [&](const char *selector) {
    xl_node_bounds b{};
    CHECK(xl_view_node_bounds(view.get(), document.query(selector), &b) ==
          XL_OK);
    return b;
  };
  const auto near = [](float a, float b,
                       const std::source_location &where =
                           std::source_location::current()) {
    if (std::abs(a - b) > .05f) {
      std::cerr << where.line() << " expected " << b << " got " << a << '\n';
      throw std::runtime_error("geometry mismatch");
    }
  };
  const auto ax = [&](const char *selector) {
    const auto id = document.query(selector);
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    for (size_t i = 0; i < count; ++i) {
      xl_ax_node node{};
      CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
      if (node.node == id)
        return node;
    }
    CHECK(false);
    return xl_ax_node{};
  };
  const auto page = [&](std::string html, std::string css = "") {
    document.html("<style>body{margin:0}svg,img{display:block}" + css +
                  "</style>" + html);
    render();
  };
  page("<svg id=x width=60 height=40 viewBox='0 0 30 20'><title>Vector "
       "icon</title><rect width=30 height=20 fill=red  /></svg><div id=b "
       "style='height:10px;background:blue'></div>");
  near(bounds("#x").width, 60);
  near(bounds("#x").height, 40);
  near(bounds("#b").y, 40);
  CHECK(ax("#x").role == XL_AX_IMAGE);
  CHECK(pixel(50, 30) == 0xff0000ff);
  CHECK(pixel(10, 45) == 0x0000ffff);
  page("<svg id=x viewBox='0 0 40 20'><rect width=40 height=20 fill=red  "
       "/></svg>",
       "#x{width:100px}");
  near(bounds("#x").height, 50);
  page("<svg id=x viewBox='0 0 40 20'><rect width=40 height=20 fill=red  "
       "/></svg>",
       "#x{height:30px}");
  near(bounds("#x").width, 60);
  page("<svg id=x viewBox='0 0 40 20'><rect width=40 height=20 fill=red  "
       "/></svg>");
  near(bounds("#x").width, 300);
  near(bounds("#x").height, 150);
  page("<svg id=x width=40 height=20 viewBox='0 0 40 20'><rect width=40 "
       "height=20 fill=red  /></svg>",
       "#x{width:120px;max-width:80px}");
  near(bounds("#x").width, 80);
  near(bounds("#x").height, 40);
  page("<svg id=x width=40 height=20 viewBox='0 0 40 20'><rect width=40 "
       "height=20 fill=red  /></svg>",
       "#x{width:60px;height:40px;padding:5px;border:2px solid "
       "blue;box-sizing:border-box}");
  near(bounds("#x").width, 60);
  near(bounds("#x").height, 40);
  CHECK(pixel(0, 10) == 0x0000ffff);
  CHECK(pixel(30, 20) == 0xff0000ff);
  // ViewBox and preserveAspectRatio are handled by the SVG renderer, not
  // by converting the whole document to a fixed-resolution bitmap.
  page("<svg id=x viewBox='0 0 40 20'><rect width=40 height=20 fill=red  "
       "/></svg>",
       "#x{width:100px;height:100px}");
  CHECK(pixel(10, 10) == 0xffffffff);
  CHECK(pixel(10, 30) == 0xff0000ff);
  document.attribute(document.query("#x"), "preserveAspectRatio", "none");
  render();
  CHECK(pixel(10, 10) == 0xff0000ff);
  page(
      "<svg id=x width=100 height=60 viewBox='0 0 100 "
      "60'><defs><linearGradient id=g><stop offset='0%' stop-color=red /><stop "
      "offset='100%' stop-color=blue /></linearGradient><clipPath id=c><rect "
      "x=10 y=10 width=60 height=30 /></clipPath><path id=shape d='M0 0 H100 "
      "V60 H0Z' /></defs><g clip-path='url(#c)' transform='translate(5 "
      "0)'><use href='#shape' fill='url(#g)' /></g></svg>");
  CHECK(pixel(5, 20) == 0xffffffff);
  const auto gradient = pixel(25, 20);
  CHECK((gradient >> 24) > 120 && ((gradient >> 8) & 255) > 20 &&
        (gradient & 0x00ff0000) == 0);
  CHECK(pixel(80, 20) == 0xffffffff);
  page("<svg id=x width=50 height=40><circle class=shape cx=20 cy=20 r=15 "
       "fill=red  /></svg>",
       ".shape{fill:lime;stroke:blue;stroke-width:2px}");
  CHECK(pixel(20, 20) == 0x00ff00ff);
  document.attribute(document.query(".shape"), "style", "fill:blue");
  render();
  CHECK(pixel(20, 20) == 0x0000ffff);
  page("<svg id=x width=40 height=30><rect class=shape width=40 height=30 "
       "/></svg>",
       ".shape{--paint:oklch(62.8% .2577 29.23);fill:var(--paint)}");
  CHECK((pixel(10, 10) >> 24) > 245 && ((pixel(10, 10) >> 16) & 255) < 10);
  page("<div style='color:rgb(0 255 0)'><svg id=x width=40 height=30><path "
       "d='M0 0 H40 V30 H0Z' fill=currentColor  /></svg></div>");
  CHECK(pixel(10, 10) == 0x00ff00ff);
  page("<div style='color:rgb(255 0 0 / .5)'><svg id=x width=40 "
       "height=30><path d='M0 0 H40 V30 H0Z' fill=currentColor /></svg></div>");
  const auto alpha = pixel(10, 10);
  CHECK((alpha >> 24) == 255 && ((alpha >> 16) & 255) >= 126 &&
        ((alpha >> 16) & 255) <= 129);
  page("<svg id=x width=50 height=40><rect width=50 height=40 fill=red  "
       "/></svg>",
       "#x{border-radius:15px;position:relative;left:10px;top:5px}");
  CHECK(pixel(10, 5) == 0xffffffff);
  CHECK(pixel(30, 20) == 0xff0000ff);
  const auto retained = pixels();
  for (int x = 0; x < 65; x += 7) {
    CHECK(xl_view_invalidate(view.get(), x, 0, 2, 60) == XL_OK);
    render();
    CHECK(pixels() == retained);
  }
  CHECK(view.render() == 0);
  // Hidden images perform no load or decode; even shape-only SVG must not
  // initialize the unrelated ICU data worker.
  xl_resource_stats resources{};
  CHECK(xl_runtime_resource_stats(runtime.get(), &resources,
                                  sizeof(resources)) == XL_OK);
  CHECK(resources.decompressions == 0 && resources.worker_threads == 0);
  {
    const auto eval = [&](std::string_view code) {
      CHECK(document.evaluate(code, 100) == XL_OK);
      return document.result();
    };
    page("<svg width=20 height=20><rect width=20 height=20 fill=red /></svg>");
    eval("globalThis.visibilityEvents=[];document.addEventListener('"
         "visibilitychange',()=>visibilityEvents.push(document.visibilityState)"
         ");globalThis.hiddenFrames=0");
    auto hidden_options = options;
    hidden_options.kind = XL_VIEW_WINDOW;
    hidden_options.flags = XL_VIEW_HIDDEN;
    xl::View hidden(runtime, &hidden_options);
    hidden.document(&document);
    uint32_t flags;
    CHECK(xl_view_render(hidden.get(), &flags) == XL_OK);
    CHECK(eval("document.hidden+':'+document.visibilityState") ==
          "true:hidden");
    eval("requestAnimationFrame(()=>hiddenFrames++);'scheduled'");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(xl_view_render(hidden.get(), &flags) == XL_OK);
    CHECK(eval("String(hiddenFrames)") == "0");
    render();
    CHECK(eval("document.hidden+':'+document.visibilityState+':'+hiddenFrames+'"
               ":'+visibilityEvents.join(',')") ==
          "false:visible:1:hidden,visible");
  }
  for (const auto *file :
       {"red.png", "red.jpg", "red.webp", "red.gif", "red.bmp", "red.ico"}) {
    std::ifstream input(std::filesystem::path(XL_IMAGE_FIXTURES) / file,
                        std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(input),
                                  std::istreambuf_iterator<char>()};
    CHECK(!bytes.empty());
    xl::Content content(std::string_view(bytes.data(), bytes.size()),
                        "application/octet-stream");
    runtime.mount("xl-webengine-ui://app/image", content);
    page("<img id=x src=image alt='Red image'>");
    near(bounds("#x").width, std::string_view(file) == "red.ico" ? 16 : 4);
    near(bounds("#x").height, std::string_view(file) == "red.ico" ? 16 : 2);
    const auto color = pixel(1, 1);
    CHECK((color >> 24) >= 250 && ((color >> 16) & 255) <= 3 &&
          ((color >> 8) & 255) <= 3);
    CHECK(ax("#x").role == XL_AX_IMAGE);
  }
  const std::string external =
      "<svg xmlns='http://www.w3.org/2000/svg' width='40' height='20' "
      "viewBox='0 0 40 20'><rect width='40' height='20' fill='red' /></svg>";
  runtime.mount(
      "xl-webengine-ui://app/width.svg",
      xl::Content(
          "<svg xmlns='http://www.w3.org/2000/svg' width='40' viewBox='0 0 40 "
          "20'><rect width='40' height='20' fill='red'/></svg>",
          "image/svg+xml"));
  page("<img id=x src='width.svg'>");
  near(bounds("#x").width, 40);
  near(bounds("#x").height, 20);
  {
    std::ifstream input(std::filesystem::path(XL_IMAGE_FIXTURES) / "red.png",
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
    runtime.mount("xl-webengine-ui://app/red.png",
                  xl::Content(bytes, "image/png"));
    page("<svg id=x width=50 height=30><image href='red.png' x=5 y=5 width=40 "
         "height=20 /></svg>");
    CHECK(pixel(10, 10) == 0xff0000ff);
    CHECK(pixel(1, 1) == 0xffffffff);
  }
  xl::Content shared(external, "image/svg+xml");
  runtime.mount("xl-webengine-ui://app/vector.svg", shared);
  page("<img id=x src='vector.svg' alt='Shared vector'>", "#x{width:100px}");
  near(bounds("#x").height, 50);
  CHECK(pixel(50, 25) == 0xff0000ff);
  document.attribute(document.query("#x"), "style", "width:60px");
  render();
  near(bounds("#x").height, 30);
  // Same immutable encoded bytes can be explicitly mounted in another
  // Runtime, while URI resolution and replacement remain isolated.
  {
    xl::Runtime other_runtime;
    xl::Document other_doc(other_runtime);
    other_runtime.mount("xl-webengine-ui://app/vector.svg", shared);
    xl::View other(other_runtime, &options);
    other.document(&other_doc);
    other_doc.html("<style>body{margin:0}img{display:block}</style><img "
                   "src='vector.svg'>");
    uint32_t flags;
    CHECK(xl_view_render(other.get(), &flags) == XL_OK);
    std::vector<uint8_t> image(240 * 180 * 4);
    CHECK(xl_view_copy_rgba(other.get(), image.data(), image.size(), 240 * 4) ==
          XL_OK);
    CHECK(image[(10 * 240 + 10) * 4 + 1] == 0);
    runtime.mount(
        "xl-webengine-ui://app/vector.svg",
        xl::Content(
            "<svg xmlns='http://www.w3.org/2000/svg' width='40' "
            "height='20'><rect width='40' height='20' fill='blue' /></svg>",
            "image/svg+xml"));
    render();
    CHECK(pixel(10, 10) == 0x0000ffff);
    CHECK(other.render() == 0);
    CHECK(xl_view_copy_rgba(other.get(), image.data(), image.size(), 240 * 4) ==
          XL_OK);
    CHECK(image[(10 * 240 + 10) * 4] == 255 &&
          image[(10 * 240 + 10) * 4 + 2] == 0);
  }
  page(
      "<div id=p style='width:70px;height:40px;overflow:auto'><svg id=x "
      "width=60 height=120><rect width=60 height=120 fill=red  /></svg></div>");
  const auto frames = document.stats().layout_frames;
  view.scroll_to(document.query("#p"), 0, 25);
  render();
  CHECK(document.stats().layout_frames == frames);
  near(bounds("#x").y, -25);
  near(ax("#x").bounds.height, 40);
  const auto scroll_image = pixels();
  document.attribute(document.query("body"), "data-oracle", "same-scroll");
  render();
  CHECK(pixels() == scroll_image);
  for (const auto *display : {"flex", "grid"}) {
    page("<div id=p><img id=x src='vector.svg'><div id=b "
         "style='width:10px;height:10px;background:lime'></div></div>",
         "#p{display:" + std::string(display) + ";width:100px}" +
             (std::string_view(display) == "grid"
                  ? "#p{grid-template-columns:60px 40px}"
                  : ""));
    near(bounds("#x").height, 20);
    CHECK(bounds("#b").x >= 40);
  }
  page("<div style='position:relative;width:150px;height:80px'><img id=x "
       "src='vector.svg'></div>",
       "#x{position:absolute;right:5px;bottom:3px;width:80px}");
  near(bounds("#x").x, 65);
  near(bounds("#x").height, 40);
  near(bounds("#x").y, 37);
  runtime.mount("xl-webengine-ui://app/bad.svg",
                xl::Content("<!DOCTYPE svg [<!ENTITY x 'bad'>]><svg "
                            "xmlns='http://www.w3.org/2000/svg'>&x;</svg>",
                            "image/svg+xml"));
  page("<img id=x src='bad.svg'>", "#x{width:20px;height:20px}");
  CHECK(pixel(5, 5) == 0xffffffff);
  const auto loads = [](void *context, const char *,
                        xl_content **result) -> xl_status {
    ++*static_cast<int *>(context);
    *result = nullptr;
    return XL_NOT_FOUND;
  };
  int load_count = 0;
  runtime.content_loader(loads, &load_count);
  page("<img id=x src='hidden.png' style='display:none'>");
  CHECK(load_count == 0);
  page("<img id=x src='missing.png' alt='Missing'>",
       "#x{width:20px;height:20px}");
  CHECK(load_count == 1);
  CHECK(pixel(2, 2) == 0xffffffff);
  CHECK(ax("#x").role == XL_AX_IMAGE);
  CHECK(view.render() == 0);
  CHECK(load_count == 1);
  page("<img id=x src='https://example.invalid/image.png'>",
       "#x{width:20px;height:20px}");
  CHECK(load_count == 1);
  runtime.content_loader(nullptr);
  page("<img id=x "
       "src=\"data:image/svg+xml,%3Csvg%20xmlns='http://www.w3.org/2000/"
       "svg'%20width='20'%20height='10'%3E%3Crect%20width='20'%20height='10'%"
       "20fill='red'/%3E%3C/svg%3E\"> ");
  CHECK(pixel(5, 5) == 0xff0000ff);
  near(bounds("#x").width, 20);
  page("<img id=x "
       "src='data:image/"
       "gif;base64,R0lGODlhAQABAIAAAP8AAAAAACH5BAAAAAAALAAAAAABAAEAAAICRAEAOw=="
       "'>");
  CHECK(pixel(0, 0) == 0xff0000ff);
  near(bounds("#x").width, 1);
  std::ifstream font_input(XL_TEST_FONT, std::ios::binary);
  const std::vector<char> font{std::istreambuf_iterator<char>(font_input),
                               std::istreambuf_iterator<char>()};
  CHECK(!font.empty());
  CHECK(xl_document_add_font(document.get(), "test", font.data(),
                             font.size()) == XL_OK);
  page("before<svg id=x width=40 height=20><rect width=40 height=20 fill=red  "
       "/></svg>after",
       "body{font:16px/20px test}#x{display:inline}");
  near(bounds("#x").height, 20);
  CHECK(bounds("#x").x > 20);
  page("<svg id=x width=160 height=40><title>Text icon</title><text x=2 y=30 "
       "font-size=28 font-family=test fill=red>SVG</text></svg>");
  bool glyph = false;
  const auto text = pixels();
  for (size_t i = 0; i < text.size(); i += 4)
    glyph |= text[i] > 100 && text[i + 1] < 100 && text[i + 2] < 100;
  CHECK(glyph);
  CHECK(view.render() == 0);
  if (gpu)
    CHECK(view.graphics_info().active == XL_GRAPHICS_SWIFTSHADER);
  std::cout << "SVG and image rendering passed\n";
} catch (const std::exception &error) {
  std::cerr << error.what() << '\n';
  return 1;
}
