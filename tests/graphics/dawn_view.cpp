#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::to_string(__LINE__) + ": " #x); } while (false)

int main(int argc, char** argv) try {
    std::cout << std::unitbuf;
    const auto backend = argc > 1 && std::strcmp(argv[1], "--hardware") == 0 ? XL_GRAPHICS_DAWN : XL_GRAPHICS_DAWN_SWIFTSHADER;
    xl::Runtime runtime;
    xl::Document document(runtime);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 80; options.height = 80;
    xl::View view(runtime, &options), other(runtime, &options);
    view.graphics_backend(backend); other.graphics_backend(backend);
    CHECK(view.graphics_info().active == backend && other.graphics_info().active == backend);
    const auto render = [](xl::View& target) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        uint32_t flags;
        xl_status status;
        while ((status = xl_view_render(target.get(), &flags)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        CHECK(status == XL_OK);
        const auto stats = target.stats();
        std::vector<unsigned char> pixels(size_t(stats.pixel_width) * stats.pixel_height * 4);
        CHECK(xl_view_copy_rgba(target.get(), pixels.data(), pixels.size(), stats.pixel_width * 4) == XL_OK);
        return pixels;
    };
    const auto green = [](const auto& pixels, int x, int y, int width = 80) {
        const auto i = size_t(y * width + x) * 4;
        CHECK(pixels[i] == 0 && pixels[i + 1] == 255 && pixels[i + 2] == 0 && pixels[i + 3] == 255);
    };
    const xl_paint_rect rectangle{0, 0, 80, 80, 0xff00ff00, 0};
    view.rects({&rectangle, 1}); other.rects({&rectangle, 1});
    green(render(view), 20, 20); green(render(other), 20, 20);
    CHECK(view.graphics_info().active == backend && view.render() == 0);
    CHECK(xl_view_resize(view.get(), 90, 90) == XL_OK); green(render(view), 20, 20, 90);
    CHECK(xl_view_resize(view.get(), 80, 80) == XL_OK);
    document.html(R"(<style>body{margin:0}#space{height:240px;background:linear-gradient(red,blue)}
                    svg,canvas{display:block}</style><svg width="40" height="40" viewBox="0 0 40 40">
                    <path fill="#00ff00" d="M0 0H40V40H0Z"/></svg><canvas id="c" width="40" height="40"></canvas>
                    <div id="space"></div>)");
    const char* script = "var x=document.getElementById('c').getContext('2d');x.fillStyle='lime';x.fillRect(0,0,40,40);'ok'";
    CHECK(xl_document_eval(document.get(), script, std::strlen(script), 1000) == XL_OK);
    view.document(&document);
    auto initial = render(view); green(initial, 20, 20); green(initial, 20, 60);
    CHECK(view.graphics_info().active == backend && view.render() == 0);
    const auto count = document.stats().layout_frames;
    view.scroll_to(0, 0, 40);
    auto scrolled = render(view); green(scrolled, 20, 20);
    CHECK(document.stats().layout_frames == count);
    // Damage redraw on the retained Graphite surface, followed by a fresh recording.
    script = "x.fillStyle='red';x.fillRect(0,0,8,8);'ok'";
    CHECK(xl_document_eval(document.get(), script, std::strlen(script), 1000) == XL_OK);
    auto changed = render(view);
    CHECK(changed[(2 * 80 + 2) * 4] == 255 && changed[(2 * 80 + 2) * 4 + 1] == 0);
    CHECK(document.stats().layout_frames == count && view.graphics_info().active == backend);
    view.abandon_graphics_backend();
    const auto recovered = render(view);
    CHECK(view.graphics_info().active != backend && view.graphics_info().recoveries == 1);
    CHECK(recovered.size() == changed.size());
    for (size_t i = 0; i < changed.size(); ++i) {
        // Measured D3D12 -> SwiftShader rounding differs by 1/255 in this
        // red/blue gradient only. SVG/Canvas/alpha stay exact across devices.
        const auto tolerance = backend == XL_GRAPHICS_DAWN && i / (80 * 4) >= 40 &&
                               (i % 4 == 0 || i % 4 == 2) ? 1 : 0;
        CHECK(std::abs(int(changed[i]) - int(recovered[i])) <= tolerance);
    }
    xl::View replacement(runtime, &options);
    replacement.graphics_backend(static_cast<xl_graphics_backend>(view.graphics_info().active));
    replacement.document(&document);
    render(replacement);
    replacement.scroll_to(0, 0, 40);
    CHECK(render(replacement) == recovered);
    green(render(other), 20, 20);
    std::atomic_bool failed{false};
    const auto concurrent = [&] {
        try {
            xl::Runtime owner;
            xl::View target(owner, &options);
            target.graphics_backend(backend);
            target.rects({&rectangle, 1});
            green(render(target), 20, 20);
            CHECK(target.graphics_info().active == backend);
        } catch (const std::exception& e) { std::cerr << e.what() << '\n'; failed = true; }
    };
    std::thread first(concurrent), second(concurrent);
    first.join(); second.join();
    CHECK(!failed);
    std::cout << "Dawn View " << backend << ": exact pixels, SVG/Canvas/gradient, static reuse, resize, scroll without layout, loss fallback, independent Views passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
