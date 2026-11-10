#include <XLWebEngineUI/xl.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::terminate(); } } while (false)
static std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats = view.stats();
    std::vector<unsigned char> data(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), data.data(), data.size(), size_t(stats.pixel_width) * 4) == XL_OK);
    return data;
}
static xl_view_options offscreen(int width = 128, int height = 128) {
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN;
    options.width = width; options.height = height;
    options.background_argb = 0xff203040;
    return options;
}
int main() {
    xl::Runtime runtime;
    auto options = offscreen();
    {
        xl::View view(runtime, &options);
        CHECK(xl_runtime_destroy(runtime.get()) == XL_BUSY);
        CHECK(xl_view_copy_rgba(view.get(), nullptr, 0, 0) == XL_BUSY);
        CHECK(view.render() == XL_FRAME_REDRAWN);
        std::vector<xl_paint_rect> rects{{8.25f, 9.5f, 20.5f, 18.75f, 0xffff0000, 0},
                                        {15.5f, 18.25f, 25, 20, 0x8000ff00, 0}};
        view.rects(rects);
        auto before = view.stats();
        CHECK(view.render() == XL_FRAME_REDRAWN);
        CHECK(view.stats().rasterized_pixels - before.rasterized_pixels < 128u * 128u);
        for (int step = 0; step < 24; ++step) {
            rects[0].x = 8.25f + float(step % 8);
            rects[1].y = 18.25f + float(step % 5);
            view.rects(rects);
            view.render();
            xl::View full(runtime, &options);
            full.rects(rects);
            full.render();
            CHECK(pixels(view) == pixels(full));
        }
        before = view.stats();
        for (int index = 0; index < 200; ++index) CHECK(view.render() == 0);
        const auto after = view.stats();
        CHECK(after.raster_frames == before.raster_frames && after.skipped_frames == before.skipped_frames + 200);
        CHECK(after.presented_frames == 0 && after.uploaded_pixels == 0);
        auto invalid = rects[0]; invalid.x = std::numeric_limits<float>::quiet_NaN();
        CHECK(xl_view_set_rects(view.get(), &invalid, 1) == XL_INVALID_ARGUMENT);
        CHECK(!view.stats().needs_frame);
        std::vector<unsigned char> untouched(128u * 128u * 4u, 0x77);
        CHECK(xl_view_copy_rgba(view.get(), untouched.data(), untouched.size() - 1, 128 * 4) == XL_BUFFER_TOO_SMALL);
        CHECK(std::all_of(untouched.begin(), untouched.end(), [](auto byte) { return byte == 0x77; }));
        xl_view_stats stats;
        std::memset(&stats, 0x55, sizeof(stats));
        std::vector<unsigned char> old(sizeof(stats)); std::memcpy(old.data(), &stats, sizeof(stats));
        CHECK(xl_view_get_stats(view.get(), &stats, sizeof(stats) - 1) == XL_ABI_MISMATCH);
        CHECK(std::memcmp(old.data(), &stats, sizeof(stats)) == 0);
        std::thread other([&] {
            uint32_t flags;
            CHECK(xl_view_render(view.get(), &flags) == XL_WRONG_THREAD);
            CHECK(xl_view_destroy(view.get()) == XL_WRONG_THREAD);
        });
        other.join();
        CHECK(xl_view_resize(view.get(), 80, 64) == XL_OK);
        CHECK(xl_view_copy_rgba(view.get(), nullptr, 0, 0) == XL_BUSY);
        CHECK(view.render() == XL_FRAME_REDRAWN);
        CHECK(pixels(view).size() == 80u * 64u * 4u);
        CHECK(xl_view_set_rects(view.get(), nullptr, 0) == XL_OK);
        view.render();
        uint32_t kind = 999; uintptr_t native = 1;
        CHECK(xl_view_native_handle(view.get(), &kind, &native) == XL_OK && kind == XL_NATIVE_NONE && native == 0);
    }
    xl_resource_stats stats;
    CHECK(xl_runtime_resource_stats(runtime.get(), &stats, sizeof(stats)) == XL_OK);
    CHECK(stats.decompressions == 0 && stats.worker_threads == 0);
    std::cout << "retained Skia views: incremental/full pixel equivalence, idle reuse, ABI, ownership and resize passed\n";
}
