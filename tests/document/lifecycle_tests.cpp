#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
int main() {
    std::ifstream input(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!font.empty());
    for (int cycle = 0; cycle < 12; ++cycle) {
        xl::Runtime runtime;
        xl::Document document(runtime);
        CHECK(xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
        document.html("<style>body {margin:0} p {width:120px; margin:0}</style><p hidden>Not loaded until visible</p>");
        xl_resource_stats resources;
        CHECK(xl_runtime_resource_stats(runtime.get(), &resources, sizeof(resources)) == XL_OK);
        CHECK(resources.decompressions == 0 && resources.worker_threads == 0);
        document.html("<style>body {margin:0} p {width:120px; margin:0}</style><p>office e\xcc\x81</p>");
        xl_view_options options;
        CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
        options.kind = XL_VIEW_OFFSCREEN; options.width = 128; options.height = 64;
        xl::View view(runtime, &options);
        view.document(&document);
        document.html("<style>body {margin:0; background-color:#123456}</style><p hidden>Hidden text</p>");
        uint32_t flags;
        CHECK(xl_view_render(view.get(), &flags) == XL_OK && flags == XL_FRAME_REDRAWN);
        CHECK(xl_runtime_resource_stats(runtime.get(), &resources, sizeof(resources)) == XL_OK && resources.decompressions == 0);
        std::vector<unsigned char> background(128 * 64 * 4);
        CHECK(xl_view_copy_rgba(view.get(), background.data(), background.size(), 128 * 4) == XL_OK);
        CHECK(background[background.size()-4] == 0x12 && background[background.size()-3] == 0x34 && background[background.size()-2] == 0x56);
        // A type=hidden attribute only hides an input. This visible paragraph
        // must trigger lazy text preparation instead of returning busy forever.
        document.html("<style>body {margin:0} p {width:120px; margin:0}</style><p type=hidden>office e\xcc\x81</p>");
        xl_status status;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        CHECK(status == XL_OK && flags == XL_FRAME_REDRAWN);
        CHECK(xl_runtime_resource_stats(runtime.get(), &resources, sizeof(resources)) == XL_OK);
        CHECK(resources.decompressions == 1 && (resources.shared_text_data_bytes > 1024 * 1024 && resources.shared_text_data_bytes < 16 * 1024 * 1024));
        std::vector<unsigned char> pixels(128 * 64 * 4);
        CHECK(xl_view_copy_rgba(view.get(), pixels.data(), pixels.size(), 128 * 4) == XL_OK);
        size_t ink = 0;
        for (size_t index = 0; index < pixels.size(); index += 4) if (pixels[index] < 240) ++ink;
        CHECK(ink > 20);
        CHECK(view.render() == 0);
        size_t ax_count;
        CHECK(xl_view_ax_count(view.get(), &ax_count) == XL_OK);
        CHECK(xl_view_resize(view.get(), 140, 64) == XL_OK);
        CHECK(xl_view_ax_count(view.get(), &ax_count) == XL_BUSY);
        CHECK(view.render() == XL_FRAME_REDRAWN);
        // A detached grandchild that is reattached under a detached root must
        // be destroyed once, after JS references have been released.
        CHECK(document.evaluate("var parent=document.createElement('div'), child=document.createElement('div'); parent.appendChild(child); child.remove(); parent.appendChild(child); parent.textContent='replacement'; child.textContent") == XL_OK);
        CHECK(document.result().empty());
        if (cycle % 2 == 0) document.html("<div></div>");
    }
    std::cout << "12 complete text/ICU/Skia/JS lifetime cycles and lazy rendering passed\n";
}
