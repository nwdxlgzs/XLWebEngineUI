#include <XLWebEngineUI/xl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "text_consumer.h"
int main(void) {
    xl_runtime* runtime = NULL;
    if (xl_runtime_create(NULL, &runtime) != XL_OK) return 1;
    printf("%s\n", xl_version_string());
    xl_view_options options;
    if (xl_view_options_init(&options, sizeof(options)) != XL_OK) return 20;
    options.kind = XL_VIEW_OFFSCREEN;
    options.width = 16;
    options.height = 12;
    options.background_argb = 0xff102030u;
    xl_view* view = NULL;
    if (xl_view_create(runtime, &options, &view) != XL_OK) return 21;
    const xl_paint_rect rect = {2, 3, 5, 4, 0xffcc8844u, 0};
    uint32_t flags = 0;
    if (xl_view_set_rects(view, &rect, 1) != XL_OK || xl_view_render(view, &flags) != XL_OK
            || flags != XL_FRAME_REDRAWN) return 22;
    unsigned char pixels[16 * 12 * 4];
    if (xl_view_copy_rgba(view, pixels, sizeof(pixels), 16 * 4) != XL_OK) return 23;
    const unsigned char background[] = {0x10, 0x20, 0x30, 0xff};
    const unsigned char painted[] = {0xcc, 0x88, 0x44, 0xff};
    if (memcmp(pixels, background, 4) || memcmp(pixels + (4 * 16 + 3) * 4, painted, 4)) return 24;
    if (xl_view_render(view, &flags) != XL_OK || flags != 0) return 25;
    xl_view_stats view_stats;
    if (xl_view_get_stats(view, &view_stats, sizeof(view_stats)) != XL_OK
            || view_stats.raster_frames != 1 || view_stats.skipped_frames != 1 || view_stats.needs_frame) return 26;
    xl_document* document = NULL;
    if (xl_document_create(runtime, &document) != XL_OK) return 28;
    const char html[] = "<div id='surface' style='width:8px;height:6px;background-color:#336699'></div>";
    if (xl_document_load_html(document, html, sizeof(html) - 1) != XL_OK || xl_view_set_document(view, document) != XL_OK) return 29;
    if (xl_view_render(view, &flags) != XL_OK || flags != XL_FRAME_REDRAWN) return 30;
    const char script[] = "document.getElementById('surface').style.cssText='width:8px;height:6px;background-color:#abcdef'; 'done'";
    if (xl_document_eval(document, script, sizeof(script) - 1, 0) != XL_OK) return 31;
    if (xl_view_render(view, &flags) != XL_OK || flags != XL_FRAME_REDRAWN
            || xl_view_copy_rgba(view, pixels, sizeof(pixels), 16 * 4) != XL_OK) return 32;
    const unsigned char changed[] = {0xab, 0xcd, 0xef, 0xff};
    if (memcmp(pixels + (9 * 16 + 9) * 4, changed, 4)) return 33;
    if (xl_document_destroy(document) != XL_BUSY) return 34;
    if (xl_runtime_destroy(runtime) != XL_BUSY || xl_view_destroy(view) != XL_OK) return 27;
    if (xl_document_destroy(document) != XL_OK) return 35;
    xl_resource_stats stats;
    if (xl_runtime_resource_stats(runtime, &stats, sizeof(stats)) != XL_OK || stats.decompressions || stats.worker_threads) return 2;
    xl_resource_request* request = NULL;
    if (xl_runtime_prewarm_resource(runtime, XL_RESOURCE_ICU_DATA, &request) != XL_OK) return 3;
    xl_resource_lease* lease = NULL;
    xl_status status;
    const clock_t start = clock();
    while ((status = xl_resource_request_poll(request, &lease)) == XL_BUSY)
        if (clock() - start > 30 * CLOCKS_PER_SEC) return 4;
    if (status != XL_OK) return 5;
    const void* data = NULL;
    size_t size = 0;
    if (xl_resource_lease_data(lease, &data, &size) != XL_OK || !data || (size <= 1024 * 1024 || size >= 16 * 1024 * 1024)) return 6;
    while ((status = xl_runtime_prepare_text(runtime)) == XL_BUSY)
        if (clock() - start > 30 * CLOCKS_PER_SEC) return 7;
    if (status != XL_OK) return 8;
    char output[8];
    size_t required = 0;
    if (xl_runtime_normalize_utf8(runtime, "e\xcc\x81", 3, output, sizeof(output), &required) != XL_OK
            || required != 2 || memcmp(output, "\xc3\xa9", 2)) return 9;
    const int text_status = consume_inline_text(runtime);
    if (text_status) return text_status;
    xl_resource_request_release(request);
    if (xl_runtime_destroy(runtime) != XL_OK) return 10;
    if (xl_resource_lease_data(lease, &data, &size) != XL_OK || (size <= 1024 * 1024 || size >= 16 * 1024 * 1024)) return 11;
    xl_resource_lease_release(lease);
    return 0;
}
