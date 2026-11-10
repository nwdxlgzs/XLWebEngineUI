#include <XLWebEngineUI/xl.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include "text_consumer.h"
int main() {
    xl::Runtime runtime;
    std::cout << xl_version_string() << "\n";
    if (xl_runtime_check_thread(runtime.get()) != XL_OK) return 1;
    {
        xl::Document document(runtime);
        document.html("<div id='surface' style='width:8px;height:6px;background-color:#336699'></div>");
        xl_view_options options{};
        if (xl_view_options_init(&options, sizeof(options)) != XL_OK) return 20;
        options.kind = XL_VIEW_OFFSCREEN;
        options.width = 16;
        options.height = 12;
        options.background_argb = 0xff102030u;
        xl::View view(runtime, &options);
        const xl_paint_rect rects[] = {{2, 3, 5, 4, 0xffcc8844u, 0}};
        view.rects(rects);
        if (view.render() != XL_FRAME_REDRAWN) return 21;
        unsigned char pixels[16 * 12 * 4]{};
        if (xl_view_copy_rgba(view.get(), pixels, sizeof(pixels), 16 * 4) != XL_OK) return 22;
        const unsigned char background[] = {0x10, 0x20, 0x30, 0xff};
        const unsigned char painted[] = {0xcc, 0x88, 0x44, 0xff};
        if (std::memcmp(pixels, background, 4) || std::memcmp(pixels + (4 * 16 + 3) * 4, painted, 4)) return 23;
        if (view.render() != 0) return 24;
        const auto stats = view.stats();
        if (stats.raster_frames != 1 || stats.skipped_frames != 1 || stats.needs_frame) return 25;
        if (xl_runtime_destroy(runtime.get()) != XL_BUSY) return 26;
        view.document(&document);
        if (view.render() != XL_FRAME_REDRAWN) return 28;
        if (document.evaluate("document.getElementById('surface').style.cssText='width:8px;height:6px;background-color:#abcdef'; 'done'") != XL_OK
                || document.result() != "done") return 29;
        if (view.render() != XL_FRAME_REDRAWN) return 30;
        xl_node_bounds bounds{};
        if (xl_view_node_bounds(view.get(), document.query("#surface"), &bounds) != XL_OK || bounds.width != 8 || bounds.height != 6) return 31;
        if (xl_view_copy_rgba(view.get(), pixels, sizeof(pixels), 16 * 4) != XL_OK) return 32;
        const unsigned char changed[] = {0xab, 0xcd, 0xef, 0xff};
        if (std::memcmp(pixels + (9 * 16 + 9) * 4, changed, 4)) return 33;
    }
    xl_resource_stats initial{};
    if (xl_runtime_resource_stats(runtime.get(), &initial, sizeof(initial)) != XL_OK
            || initial.decompressions || initial.worker_threads) return 27;
    auto request = runtime.resource(XL_RESOURCE_ICU_DATA);
    xl::ResourceLease lease;
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    xl_status status;
    while ((status = request.poll(lease)) == XL_BUSY) {
        if (std::chrono::steady_clock::now() > end) return 2;
        std::this_thread::yield();
    }
    if (status != XL_OK || (lease.size() <= 1024 * 1024 || lease.size() >= 16 * 1024 * 1024)) return 3;
    char output[8];
    size_t required = 0;
    while ((status = xl_runtime_normalize_utf8(runtime.get(), "e\xcc\x81", 3, output, sizeof(output), &required)) == XL_BUSY) {
        if (std::chrono::steady_clock::now() > end) return 4;
        std::this_thread::yield();
    }
    if (status != XL_OK || required != 2 || std::memcmp(output, "\xc3\xa9", 2)) return 5;
    return consume_inline_text(runtime.get());
}
