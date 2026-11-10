#ifndef XL_TEST_TEXT_CONSUMER_H
#define XL_TEST_TEXT_CONSUMER_H
#include <XLWebEngineUI/xl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int consume_inline_text(xl_runtime* runtime) {
    FILE* input = fopen(XL_TEST_FONT, "rb");
    if (!input || fseek(input, 0, SEEK_END)) return 40;
    const long size = ftell(input);
    if (size <= 0 || size > 4 * 1024 * 1024 || fseek(input, 0, SEEK_SET)) return 41;
    void* font = malloc((size_t)size);
    if (!font || fread(font, 1, (size_t)size, input) != (size_t)size) return 42;
    fclose(input);
    xl_document* document = NULL;
    if (xl_document_create(runtime, &document) != XL_OK
            || xl_document_add_font(document, "consumer", font, (size_t)size) != XL_OK) return 43;
    free(font);
    const char html[] = "<style>body,p{margin:0}p{width:220px;font-size:20px;font-family:consumer}</style><p><span id=a>  A </span><span id=b>B</span></p>";
    if (xl_document_load_html(document, html, sizeof(html)-1) != XL_OK) return 44;
    xl_view_options options;
    if (xl_view_options_init(&options, sizeof(options)) != XL_OK) return 45;
    options.kind = XL_VIEW_OFFSCREEN; options.width = 240; options.height = 80;
    xl_view* view = NULL;
    uint32_t flags;
    if (xl_view_create(runtime, &options, &view) != XL_OK || xl_view_set_document(view, document) != XL_OK
            || xl_view_render(view, &flags) != XL_OK) return 46;
    xl_node_id span_a, span_b, a = 0, b = 0;
    if (xl_document_query_selector(document, "#a", 2, &span_a) != XL_OK
            || xl_document_query_selector(document, "#b", 2, &span_b) != XL_OK) return 47;
    size_t count;
    if (xl_view_ax_count(view, &count) != XL_OK) return 48;
    for (size_t i = 0; i < count; ++i) {
        xl_ax_node node;
        if (xl_view_ax_get(view, i, &node, sizeof(node)) != XL_OK) return 49;
        if (node.role == XL_AX_TEXT && node.parent == span_a) a = node.node;
        if (node.role == XL_AX_TEXT && node.parent == span_b) b = node.node;
    }
    if (!a || !b) return 50;
    xl_node_bounds ra, rb;
    if (xl_view_dom_text_rects(view, a, 2, 3, &ra, 1, &count) != XL_OK || count != 1
            || xl_view_dom_text_rects(view, b, 0, 1, &rb, 1, &count) != XL_OK || count != 1
            || ra.y != rb.y || rb.x <= ra.x + ra.width) return 51;
    xl_text_position hit;
    if (xl_view_hit_test_text(view, ra.x + ra.width * .1f, ra.y + ra.height * .5f, &hit, sizeof(hit)) != XL_OK
            || hit.node != a || hit.offset_utf16 != 2) return 52;
    unsigned char pixels[240 * 80 * 4], full[240 * 80 * 4];
    if (xl_view_copy_rgba(view, pixels, sizeof(pixels), 240 * 4) != XL_OK) return 53;
    size_t ink = 0;
    for (size_t i = 0; i < sizeof(pixels); i += 4) if (pixels[i] < 240) ++ink;
    if (ink < 20 || xl_view_render(view, &flags) != XL_OK || flags != 0) return 54;
    const char script[] = "document.getElementById('b').style.cssText='color:#ff0000'";
    if (xl_document_eval(document, script, sizeof(script)-1, 0) != XL_OK || xl_view_render(view, &flags) != XL_OK
            || xl_view_copy_rgba(view, pixels, sizeof(pixels), 240 * 4) != XL_OK) return 55;
    xl_view* rebuilt = NULL;
    if (xl_view_create(runtime, &options, &rebuilt) != XL_OK || xl_view_set_document(rebuilt, document) != XL_OK
            || xl_view_render(rebuilt, &flags) != XL_OK || xl_view_copy_rgba(rebuilt, full, sizeof(full), 240 * 4) != XL_OK
            || memcmp(pixels, full, sizeof(full))) return 56;
    const char preserved[] = "<style>body,p{margin:0}p{font:20px/32px consumer;white-space:pre;tab-size:40px}</style><p><span id=a>A</span>\t<span id=b>B</span><br>C</p>";
    if (xl_document_load_html(document, preserved, sizeof(preserved)-1) != XL_OK || xl_view_render(view, &flags) != XL_OK
            || xl_document_query_selector(document, "#b", 2, &span_b) != XL_OK || xl_view_node_bounds(view, span_b, &rb) != XL_OK
            || rb.x < 39.9f || rb.x > 40.1f) return 58;
    xl_node_id paragraph;
    if (xl_document_query_selector(document, "p", 1, &paragraph) != XL_OK || xl_view_node_bounds(view, paragraph, &ra) != XL_OK
            || ra.height != 64) return 59;
    char size_css[16];
    if (xl_document_get_style(document, paragraph, "font-size", size_css, sizeof(size_css), &count) != XL_OK
            || count != 4 || memcmp(size_css, "20px", 4)) return 60;
    const char font_update[] = "document.querySelector('p').style.cssText='font:30px/40px consumer;color:#123456'";
    if (xl_document_eval(document, font_update, sizeof(font_update)-1, 0) != XL_OK || xl_view_render(view, &flags) != XL_OK
            || xl_view_node_bounds(view, paragraph, &ra) != XL_OK || ra.height != 80
            || xl_view_copy_rgba(view, pixels, sizeof(pixels), 240*4) != XL_OK
            || xl_view_render(rebuilt, &flags) != XL_OK || xl_view_copy_rgba(rebuilt, full, sizeof(full), 240*4) != XL_OK
            || memcmp(pixels, full, sizeof(full))) return 61;
    const char boxes[] = "<style>body,p{margin:0}p{font:20px/24px consumer;width:220px}</style><p><span id=a style='margin:0 4px 0 3px;padding:2px 5px;border:2px solid red'>  A </span><span id=b>B</span></p>";
    if (xl_document_load_html(document, boxes, sizeof(boxes)-1) != XL_OK || xl_view_render(view, &flags) != XL_OK
            || xl_document_query_selector(document, "#a", 2, &span_a) != XL_OK
            || xl_document_query_selector(document, "#b", 2, &span_b) != XL_OK
            || xl_view_node_bounds(view, span_a, &ra) != XL_OK || xl_view_node_bounds(view, span_b, &rb) != XL_OK
            || ra.x != 3 || ra.height < 31 || ra.height > 33 || rb.x < ra.x+ra.width+3.9f || rb.x > ra.x+ra.width+4.1f) return 62;
    a = 0;
    if (xl_view_ax_count(view, &count) != XL_OK) return 63;
    for (size_t i = 0; i < count; ++i) {
        xl_ax_node node;
        if (xl_view_ax_get(view, i, &node, sizeof(node)) != XL_OK) return 64;
        if (node.role == XL_AX_TEXT && node.parent == span_a) {
            char name[16]; size_t size_name;
            if (xl_view_ax_name(view, i, name, sizeof(name), &size_name) != XL_OK || size_name != 2 || memcmp(name, "A ", 2)) return 65;
            a = node.node;
        }
    }
    if (!a || xl_view_dom_text_rects(view, a, 2, 3, &ra, 1, &count) != XL_OK || count != 1 || ra.x != 10
            || xl_view_hit_test_text(view, ra.x+1, ra.y+ra.height*.5f, &hit, sizeof(hit)) != XL_OK
            || hit.node != a || hit.offset_utf16 != 2) return 66;
    const char box_update[] = "document.getElementById('a').style.cssText='padding:3px 8px;border:3px double blue'";
    if (xl_document_eval(document, box_update, sizeof(box_update)-1, 0) != XL_OK || xl_view_render(view, &flags) != XL_OK
            || xl_view_copy_rgba(view, pixels, sizeof(pixels), 240*4) != XL_OK || xl_view_render(rebuilt, &flags) != XL_OK
            || xl_view_copy_rgba(rebuilt, full, sizeof(full), 240*4) != XL_OK || memcmp(pixels, full, sizeof(full))) return 67;
    if (xl_view_destroy(rebuilt) != XL_OK || xl_view_destroy(view) != XL_OK || xl_document_destroy(document) != XL_OK) return 57;
    return 0;
}
#endif
