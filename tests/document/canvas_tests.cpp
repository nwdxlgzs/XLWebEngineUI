#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(std::string("line ") +                          \
                               std::to_string(__LINE__) + ": " #x);            \
  } while (false)
int main(int argc, char **argv) try {
  xl::Runtime runtime;
  xl::Document document(runtime);
  xl_view_options options;
  CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
  options.kind = XL_VIEW_OFFSCREEN;
  options.width = 160;
  options.height = 120;
  xl::View view(runtime, &options);
  view.document(&document);
  if (argc == 2 && std::string_view(argv[1]) == "--gpu")
    view.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
  const auto script = [&](const char *code, const char *expected = "ok") {
    xl_status s = xl_document_eval(document.get(), code,
                                   std::char_traits<char>::length(code), 1000);
    if (s != XL_OK) {
      size_t n = 0;
      xl_document_script_result(document.get(), nullptr, 0, &n);
      std::string error(n, ' ');
      xl_document_script_result(document.get(), error.data(), error.size(), &n);
      std::cerr << error << '\n';
    }
    CHECK(s == XL_OK);
    size_t n = 0;
    xl_document_script_result(document.get(), nullptr, 0, &n);
    std::string result(n, ' ');
    CHECK(xl_document_script_result(document.get(), result.data(),
                                    result.size(), &n) == XL_OK);
    CHECK(result == expected);
  };
  const auto render = [&] {
    const auto end =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags;
    xl_status s;
    while ((s = xl_view_render(view.get(), &flags)) == XL_BUSY) {
      CHECK(std::chrono::steady_clock::now() < end);
      std::this_thread::yield();
    }
    CHECK(s == XL_OK);
  };
  const auto pixels = [&] {
    std::vector<uint8_t> data(160 * 120 * 4);
    CHECK(xl_view_copy_rgba(view.get(), data.data(), data.size(), 160 * 4) ==
          XL_OK);
    return data;
  };
  const auto pixel = [&](int x, int y) {
    const auto p = pixels();
    const auto i = (y * 160 + x) * 4;
    return uint32_t(p[i]) << 24 | uint32_t(p[i + 1]) << 16 |
           uint32_t(p[i + 2]) << 8 | p[i + 3];
  };
  document.html("<style>body{margin:0}canvas{display:block}</style><canvas "
                "id=c width=80 height=60 aria-label=Artwork></canvas>");
  script(
      "var "
      "c=document.getElementById('c'),x=c.getContext('2d');if(x!==c.getContext("
      "'2d')||x.canvas!==c||c.width!==80||c.height!==60)throw Error('context "
      "identity');x.fillStyle='red';x.fillRect(0,0,40,30);'ok'");
  render();
  CHECK(pixel(10, 10) == 0xff0000ff);
  CHECK(pixel(50, 10) == 0xffffffff);
  CHECK(view.render() == 0);
  const auto layout_count = document.stats().layout_frames;
  script("x.fillStyle='lime';x.fillRect(0,0,5,5);'ok'");
  render();
  CHECK(document.stats().layout_frames == layout_count);
  CHECK(pixel(2, 2) == 0x00ff00ff);
  script("String(x.getImageData(10,10,1,1).data)", "255,0,0,255");
  script("String(x.getImageData(79,59,2,2).data)",
         "0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0");
  script("var "
         "empty=document.createElement('canvas');if(empty.width!==300||empty."
         "height!==150||empty.getContext('webgpu')!==null)throw Error('canvas "
         "defaults');empty.width=0;var "
         "ex=empty.getContext('2d');ex.fillText('unused',0,0);if(empty."
         "toDataURL()!=='data:,')throw Error('zero bitmap');'ok'");
  xl_resource_stats resource{};
  CHECK(xl_runtime_resource_stats(runtime.get(), &resource, sizeof(resource)) ==
        XL_OK);
  CHECK(resource.shared_text_data_bytes == 0);
  script("c.width=c.width;if(x.fillStyle!=='#000000'||x.globalAlpha!==1||x!==c."
         "getContext('2d'))throw Error('resize "
         "reset');x.fillStyle='blue';x.fillRect(0,0,20,20);'ok'");
  render();
  CHECK(pixel(10, 10) == 0x0000ffff);
  CHECK(pixel(30, 10) == 0xffffffff);
  script("x.save();x.translate(30,0);x.beginPath();x.rect(0,0,10,20);x.clip();"
         "x.fillStyle='lime';x.fillRect(-20,0,100,60);x.restore();x.fillStyle='"
         "red';x.fillRect(50,0,10,10);'ok'");
  render();
  CHECK(pixel(35, 10) == 0x00ff00ff);
  CHECK(pixel(25, 10) == 0xffffffff);
  CHECK(pixel(55, 5) == 0xff0000ff);
  // Current paths survive restore and retain the transform used at
  // construction.
  script("c.width=80;x.save();x.translate(20,10);x.beginPath();x.rect(0,0,10,"
         "10);x.restore();x.fillStyle='red';x.fill();'ok'");
  render();
  CHECK(pixel(25, 15) == 0xff0000ff);
  CHECK(pixel(5, 5) == 0xffffffff);
  script("x.beginPath();x.rect(40,10,25,25);x.rect(45,15,15,15);x.fillStyle='"
         "lime';x.fill('evenodd');'ok'");
  render();
  CHECK(pixel(42, 12) == 0x00ff00ff);
  CHECK(pixel(50, 20) == 0xffffffff);
  script("c.width=80;x.beginPath();x.rect(30,30,-20,-20);x.fillStyle='red';x."
         "fill();'ok'");
  render();
  CHECK(pixel(20, 20) == 0xff0000ff);
  script("c.width=80;var "
         "g=x.createLinearGradient(0,0,80,0);g.addColorStop(0,'red');g."
         "addColorStop(1,'blue');x.fillStyle=g;x.fillRect(0,0,80,20);'ok'");
  render();
  CHECK((pixel(4, 10) >> 24) > 220);
  CHECK(((pixel(75, 10) >> 8) & 255) > 220);
  script("x.fillStyle='red';x.beginPath();x.arc(20,40,10,0,2*Math.PI);x.fill();"
         "x.strokeStyle='blue';x.lineWidth=4;x.beginPath();x.moveTo(40,40);x."
         "quadraticCurveTo(50,25,60,40);x.stroke();'ok'");
  render();
  CHECK(pixel(20, 40) == 0xff0000ff);
  script("c.width=80;x.save();x.translate(50,0);var "
         "t=x.createLinearGradient(0,0,40,0);t.addColorStop(0,'red');t."
         "addColorStop(1,'blue');x.restore();x.fillStyle=t;x.fillRect(0,0,40,"
         "10);x.translate(40,0);x.fillRect(0,0,40,10);x.resetTransform();var "
         "r=x.createRadialGradient(20,35,0,20,35,10);r.addColorStop(0,'lime');"
         "r.addColorStop(1,'blue');x.fillStyle=r;x.fillRect(0,20,40,30);'ok'");
  render();
  CHECK(pixel(2, 5) == pixel(42, 5));
  CHECK(((pixel(20, 35) >> 16) & 255) > 200);
  CHECK(((pixel(5, 35) >> 8) & 255) > 240);
  script(
      "var "
      "conic=x.createConicGradient(0,60,35);conic.addColorStop(0,'red');conic."
      "addColorStop(1,'blue');x.fillStyle=conic;x.fillRect(50,25,25,25);'ok'");
  render();
  CHECK((pixel(70, 35) >> 24) > 220);
  CHECK(((pixel(60, 26) >> 8) & 255) > 160);
  script("c.width=80;x.fillStyle='red';x.fillRect(0,0,30,30);x.globalAlpha=.5;"
         "x.fillStyle='blue';x.fillRect(0,0,20,20);'ok'");
  render();
  const auto blended = pixel(10, 10);
  CHECK((blended >> 24) >= 126 && (blended >> 24) <= 128);
  CHECK(((blended >> 8) & 255) >= 127 && ((blended >> 8) & 255) <= 129);
  script("x.globalAlpha=1;x.globalCompositeOperation='copy';x.fillStyle='lime';"
         "x.fillRect(40,0,10,10);'ok'");
  render();
  CHECK(pixel(10, 10) == 0xffffffff);
  CHECK(pixel(45, 5) == 0x00ff00ff);
  script("c.width=80;var d=new "
         "ImageData(2,2);d.data.set([255,0,0,255,0,255,0,255,0,0,255,255,0,0,0,"
         "0]);x.translate(30,30);x.globalAlpha=.1;x.beginPath();x.rect(50,50,1,"
         "1);x.clip();x.putImageData(d,1,1);'ok'");
  render();
  CHECK(pixel(1, 1) == 0xff0000ff);
  CHECK(pixel(2, 1) == 0x00ff00ff);
  CHECK(pixel(1, 2) == 0x0000ffff);
  script("String(x.getImageData(1,1,2,2).data)",
         "255,0,0,255,0,255,0,255,0,0,255,255,0,0,0,0");
  script("c.width=80;var "
         "off=document.createElement('canvas');off.width=20;off.height=10;var "
         "ox=off.getContext('2d');ox.fillStyle='red';ox.fillRect(0,0,20,10);x."
         "drawImage(off,10,5,40,20);'ok'");
  render();
  CHECK(pixel(20, 10) == 0xff0000ff);
  CHECK(pixel(5, 5) == 0xffffffff);
  // Source snapshots survive later source mutations, even across distinct
  // Views.
  script("ox.fillStyle='blue';ox.fillRect(0,0,20,10);'ok'");
  render();
  CHECK(pixel(20, 10) == 0xff0000ff);
  script("x.drawImage(off,0,0,10,10,50,30,20,20);'ok'");
  render();
  CHECK(pixel(60, 40) == 0x0000ffff);
  script("var "
         "exported=c.toDataURL();if(!exported.startsWith('data:image/"
         "png;base64,iVBOR'))throw Error('PNG export');var "
         "pic=document.createElement('img');pic.setAttribute('src',exported);x."
         "reset();x.drawImage(pic,0,0);'ok'");
  render();
  CHECK(pixel(60, 40) == 0x0000ffff);
  script("c.width=80;x.fillStyle='lime';x.fillRect(0,0,80,60);x.globalAlpha=0;"
         "x.clearRect(0,0,10,10);'ok'");
  render();
  CHECK(pixel(5, 5) == 0xffffffff);
  CHECK(pixel(20, 20) == 0x00ff00ff);
  script("var "
         "good=0;try{x.arc(0,0,-1,0,1)}catch(e){good++}try{g.addColorStop(2,'"
         "red')}catch(e){good++}try{x.getImageData(0,0,0,1)}catch(e){good++}if("
         "good!==3)throw Error('invalid "
         "input');x.lineWidth=-1;x.globalAlpha=2;x.fillStyle='not-a-color';if("
         "x.lineWidth!==1||x.globalAlpha!==0||x.fillStyle!=='#00ff00')throw "
         "Error('invalid state ignored');'ok'");
  script("c.width=80;x.setLineDash([2,3,4]);x.save();x.setLineDash([1,1]);x."
         "restore();String(x.getLineDash())",
         "2,3,4,2,3,4");
  script("c.setAttribute('width','80');String([x.fillStyle,x.getLineDash()."
         "length,x.getTransform().a])",
         "#000000,0,1");
  std::ifstream input(XL_TEST_FONT, std::ios::binary);
  const std::vector<char> font{std::istreambuf_iterator<char>(input),
                               std::istreambuf_iterator<char>()};
  CHECK(!font.empty());
  CHECK(xl_document_add_font(document.get(), "canvas test", font.data(),
                             font.size()) == XL_OK);
  script("x.font='24px \"canvas test\"';x.fillStyle='red';x.fillText('office "
         "e\\u0301',2,30);var m=x.measureText('office "
         "e\\u0301');if(!(m.width>40&&m.actualBoundingBoxAscent>5))throw "
         "Error('text metrics');'ok'");
  render();
  bool text = false;
  const auto ink = pixels();
  for (size_t i = 0; i < ink.size(); i += 4)
    text |= ink[i] > 150 && ink[i + 1] < 100 && ink[i + 2] < 100;
  CHECK(text);
  CHECK(view.render() == 0);
  script(
      "c.width=80;x.font='bold italic 24px \"canvas "
      "test\"';x.strokeStyle='blue';x.lineWidth=1;x.strokeText('SVG',2,30);x."
      "fillStyle='red';x.textAlign='center';x.fillText('aa',60,50,16);'ok'");
  render();
  bool blue = false;
  const auto stroke = pixels();
  for (size_t i = 0; i < stroke.size(); i += 4)
    blue |= stroke[i] < 100 && stroke[i + 1] < 100 && stroke[i + 2] > 150;
  CHECK(blue);
  script("c.width=80;x.fillStyle='red';x.fillRect(0,0,20,20);'ok'");
  render();
  xl::View other(runtime, &options);
  other.document(&document);
  CHECK(other.render() == XL_FRAME_REDRAWN);
  script("x.fillStyle='blue';x.fillRect(0,0,20,20);'ok'");
  render();
  CHECK(other.render() == XL_FRAME_REDRAWN);
  std::vector<uint8_t> other_pixels(160 * 120 * 4);
  CHECK(xl_view_copy_rgba(other.get(), other_pixels.data(), other_pixels.size(),
                          160 * 4) == XL_OK);
  CHECK(other_pixels == pixels());
  script("c.style.width='40px';c.style.height='30px';'ok'");
  render();
  xl_node_bounds box{};
  CHECK(xl_view_node_bounds(view.get(), document.query("#c"), &box) == XL_OK);
  CHECK(box.width == 40 && box.height == 30);
  CHECK(pixel(5, 5) == 0x0000ffff);
  CHECK(pixel(15, 15) == 0xffffffff);
  // Navigation removes old bitmap content and contexts; an untouched canvas
  // stays transparent.
  document.html("<style>body{margin:0}canvas{display:block}</style><canvas "
                "id=c width=80 height=60></canvas>");
  render();
  CHECK(pixel(5, 5) == 0xffffffff);
  document.html(
      "<style>body{margin:0;font:16px 'canvas "
      "test'}#scroll{width:100px;height:50px;overflow:auto}canvas{vertical-"
      "align:top}#space{height:80px}</style><div id=scroll><span "
      "style='display:inline-block'>a<canvas id=c width=40 "
      "height=40></canvas>b</span><div id=space></div></div>");
  script("var "
         "c=document.getElementById('c'),x=c.getContext('2d');x.fillStyle='red'"
         ";x.fillRect(0,0,40,40);'ok'");
  render();
  const auto inline_layouts = document.stats().layout_frames;
  const auto red = pixels();
  script("x.fillStyle='blue';x.fillRect(0,0,40,40);'ok'");
  render();
  CHECK(document.stats().layout_frames == inline_layouts);
  CHECK(pixels() != red);
  auto current = pixels();
  script("c.setAttribute('aria-label','Canvas');'ok'");
  render();
  CHECK(pixels() == current);
  CHECK(xl_view_set_scroll(view.get(), document.query("#scroll"), 0, 15) ==
        XL_OK);
  render();
  const auto scroll_layouts = document.stats().layout_frames;
  script("x.fillStyle='lime';x.fillRect(0,0,40,40);'ok'");
  render();
  CHECK(document.stats().layout_frames == scroll_layouts);
  current = pixels();
  script("c.setAttribute('aria-label','Canvas again');'ok'");
  render();
  CHECK(pixels() == current);
#ifdef XL_CANVAS_PAGE
  if (argc == 2 && std::string_view(argv[1]) == "--page") {
    const std::string page_path = XL_CANVAS_PAGE;
    std::ifstream page_input(
        std::filesystem::path(
            std::u8string(reinterpret_cast<const char8_t *>(page_path.data()),
                          page_path.size())),
        std::ios::binary);
    const std::string html{std::istreambuf_iterator<char>(page_input),
                           std::istreambuf_iterator<char>()};
    CHECK(!html.empty());
    std::cerr << "Palette original source read\n";
    CHECK(xl_view_resize(view.get(), 1000, 760) == XL_OK);
    document.html(html);
    script("globalThis.console={log(){},warn(){},error(){},info(){}};for(const "
           "s of "
           "document.querySelectorAll('script'))if(!s.getAttribute('src'))(0,"
           "eval)(s.textContent);'ok'");
    std::cerr << "Palette original script initialized\n";
    render();
    std::cerr << "Palette first frame rendered\n";
    // Isolate Canvas updates from the page's wall-clock DOM updates. The
    // separate WebTest smoke continues to execute the author's unaltered page.
    script("for(let "
           "timer=1;timer<=1024;++timer){clearTimeout(timer);"
           "cancelAnimationFrame(timer)}'ok'");
    script("var "
           "vc=document.getElementById('vcv'),qc=document.getElementById('qrcv'"
           ");if(vc.width!==480||vc.height!==270||qc.width!==105)throw "
           "Error('authored bitmap dimensions');var "
           "before=vctx.getImageData(0,0,480,270).data;vT=10;vDraw();var "
           "after=vctx.getImageData(0,0,480,270).data;if(before.every((v,i)=>v="
           "==after[i]))throw Error('video demo bitmap unchanged');var "
           "qr=qc.getContext('2d').getImageData(0,0,105,105).data;if(!qr.some(("
           "v,i)=>i%4===3&&v>0)||!qr.some((v,i)=>i%4===3&&v===0))throw "
           "Error('QR bitmap not drawn');'ok'");
    std::cerr << "Palette bitmap changes and QR verified\n";
    const auto before = document.stats().layout_frames;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i) {
      script("vT+=1;vDraw();'ok'");
      std::cerr << "Palette Canvas frame " << i << " queued\n";
      render();
      std::cerr << "Palette Canvas frame " << i << " rendered\n";
    }
    const auto ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    CHECK(document.stats().layout_frames == before);
    std::cout << "Palette with unrelated timers stopped after its first frame: "
                 "3 Canvas updates "
              << ms << " ms; extra layouts 0\n";
  }
#endif
  if (argc == 2 && std::string_view(argv[1]) == "--gpu")
    CHECK(view.graphics_info().active == XL_GRAPHICS_SWIFTSHADER);
  std::cout << "Canvas2D rendering passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << '\n';
  return 1;
}
