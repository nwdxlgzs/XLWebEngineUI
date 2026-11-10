#include <XLWebEngineUI/xl.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <fstream>
#include <chrono>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
static std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats = view.stats();
    std::vector<unsigned char> output(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), output.data(), output.size(), size_t(stats.pixel_width) * 4) == XL_OK);
    return output;
}
static std::string text(xl_document* doc, xl_node_id node) {
    size_t required = 0;
    CHECK(xl_document_get_text(doc, node, nullptr, 0, &required) == XL_BUFFER_TOO_SMALL);
    std::string output(required, '\0');
    CHECK(xl_document_get_text(doc, node, output.data(), output.size(), &required) == XL_OK);
    return output;
}
int main() {
    xl::Runtime runtime;
    xl::Document document(runtime);
    std::ifstream input(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!font.empty());
    CHECK(xl_document_add_font(document.get(), "test font", font.data(), font.size()) == XL_OK);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    xl_status prepared;
    while ((prepared = xl_runtime_prepare_text(runtime.get())) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    CHECK(prepared == XL_OK);
    document.html(R"HTML(<!doctype html><style>
        body { margin:0; padding:4px; }
        #panel { width:64px; height:40px; background-color:#ff0000; }
        #panel.active { background-color:rgb(0,255,0) !important; }
        button { display:block; }
    </style><div id="panel" role="button" tabindex="0" aria-label="Activate">Hello <b id="old">world</b></div>)HTML");
    const auto panel = document.query("#panel"), old = document.query("#old");
    CHECK(panel && old && text(document.get(), panel) == "Hello world");
    CHECK(xl_runtime_destroy(runtime.get()) == XL_BUSY);
    xl_view_options options{};
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 128; options.height = 96;
    xl::View view(runtime, &options);
    view.document(&document);
    CHECK(xl_document_destroy(document.get()) == XL_BUSY);
    CHECK(view.render() == XL_FRAME_REDRAWN);
    const auto image = pixels(view);
    const auto offset = (40 * 128 + 60) * 4;
    CHECK(image[offset] == 255 && image[offset + 1] == 0 && image[offset + 2] == 0);
    xl_node_bounds bounds;
    CHECK(xl_view_node_bounds(view.get(), panel, &bounds) == XL_OK);
    CHECK(bounds.x == 4 && bounds.y == 4 && bounds.width == 64 && bounds.height == 40);
    const auto frames = document.stats().layout_frames;
    CHECK(view.render() == 0 && document.stats().layout_frames == frames);
    CHECK(document.stats().unsupported_layout_nodes == 0);
    CHECK(document.evaluate("globalThis.saved = document.getElementById('panel'); saved === document.querySelector('#panel')") == XL_OK);
    CHECK(document.result() == "true");
    CHECK(document.evaluate("saved.className = 'active'; saved.textContent = 'Updated'; saved.textContent") == XL_OK);
    CHECK(document.result() == "Updated" && text(document.get(), old) == "world");
    CHECK(xl_view_node_bounds(view.get(), panel, &bounds) == XL_BUSY);
    const auto before = view.stats();
    CHECK(view.render() == XL_FRAME_REDRAWN);
    const auto green = pixels(view);
    CHECK(green[offset] == 0 && green[offset + 1] == 255 && green[offset + 2] == 0);
    CHECK(view.stats().rasterized_pixels - before.rasterized_pixels < 128u * 96u);
    {
        xl::View complete(runtime, &options);
        complete.document(&document); complete.render();
        CHECK(pixels(view) == pixels(complete));
    }
    CHECK(document.evaluate("saved.addEventListener('click', () => { saved.setAttribute('aria-label','Clicked'); }); 'ready'") == XL_OK);
    CHECK(xl_document_ax_action(document.get(), panel, XL_AX_ACTION_ACTIVATE) == XL_OK);
    CHECK(xl_document_ax_action(document.get(), panel, XL_AX_ACTION_FOCUS) == XL_OK);
    view.render();
    size_t count = 0;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK && count > 1);
    bool found = false;
    for (size_t index = 0; index < count; ++index) {
        xl_ax_node node;
        CHECK(xl_view_ax_get(view.get(), index, &node, sizeof(node)) == XL_OK);
        if (node.node != panel) continue;
        found = true;
        CHECK(node.role == XL_AX_BUTTON && (node.states & XL_AX_FOCUSED) && node.bounds.width == 64);
        char name[20]{}; size_t required;
        CHECK(xl_view_ax_name(view.get(), index, name, sizeof(name), &required) == XL_OK);
        CHECK(std::string(name, required) == "Clicked");
    }
    CHECK(found);
    bool text_found = false;
    for (size_t index = 0; index < count; ++index) {
        xl_ax_node node;
        CHECK(xl_view_ax_get(view.get(), index, &node, sizeof(node)) == XL_OK);
        if (node.role != XL_AX_TEXT) continue;
        text_found = true;
        CHECK(node.bounds.width > 0 && node.bounds.height > 0);
        size_t required_ranges = 0;
        CHECK(xl_view_text_rects(view.get(), node.node, 0, 2, nullptr, 0, &required_ranges) == XL_BUFFER_TOO_SMALL);
        CHECK(required_ranges > 0);
        std::vector<xl_node_bounds> ranges(required_ranges);
        CHECK(xl_view_text_rects(view.get(), node.node, 0, 2, ranges.data(), ranges.size(), &required_ranges) == XL_OK);
        CHECK(ranges[0].x >= bounds.x && ranges[0].width > 0);
        CHECK(xl_view_text_rects(view.get(), node.node, 0, 1000, nullptr, 0, &required_ranges) == XL_INVALID_ARGUMENT);
    }
    CHECK(text_found);
    CHECK(document.evaluate("var created = document.createElement('div'); created.textContent='New'; saved.appendChild(created); created.remove(); created.textContent") == XL_OK);
    CHECK(document.result() == "New");
    CHECK(document.evaluate("document.querySelector('#panel').className = ''") == XL_OK);
    char declaration[128]{}; size_t declaration_size;
    CHECK(xl_document_get_style(document.get(), panel, "background-color", declaration, sizeof(declaration), &declaration_size) == XL_OK);
    CHECK(std::string(declaration, declaration_size) == "#ff0000");
    CHECK(document.evaluate("document.querySelector('#panel').className = 'active'") == XL_OK);
    CHECK(document.evaluate("saved.appendChild(saved)") == XL_SCRIPT_ERROR);
    CHECK(document.evaluate("throw new Error('expected diagnostic')") == XL_SCRIPT_ERROR);
    CHECK(document.result().find("expected diagnostic") != std::string::npos);
    CHECK(document.evaluate("while (true) {}", 5) == XL_TIMEOUT);
    CHECK(document.evaluate("6 * 7") == XL_OK && document.result() == "42");
    CHECK(document.evaluate("({toString() { while(true) {} }})", 5) == XL_TIMEOUT);
    CHECK(document.evaluate("throw {toString() { while(true) {} }}", 5) == XL_TIMEOUT);
    CHECK(document.evaluate("'after conversion timeout'") == XL_OK);
    const auto native_child = document.create("div");
    document.attribute(native_child, "id", "from-cpp");
    document.text(native_child, "Shared DOM");
    document.append(panel, native_child);
    CHECK(document.evaluate("document.getElementById('from-cpp').textContent") == XL_OK && document.result() == "Shared DOM");
    document.remove(native_child);
    CHECK(document.query("#from-cpp") == 0 && text(document.get(), native_child) == "Shared DOM");
    CHECK(document.evaluate("Promise.resolve().then(() => saved.setAttribute('data-job','ran')); 'queued'") == XL_OK);
    char attribute[4]{}; size_t required;
    CHECK(xl_document_get_attribute(document.get(), panel, "data-job", attribute, sizeof(attribute), &required) == XL_OK);
    CHECK(std::string(attribute, required) == "ran");
    char untouched[] = "abc";
    CHECK(xl_document_get_text(document.get(), panel, untouched, 1, &required) == XL_BUFFER_TOO_SMALL);
    CHECK(std::strcmp(untouched, "abc") == 0);
    std::thread other([&] {
        CHECK(xl_document_set_text(document.get(), panel, "bad", 3) == XL_WRONG_THREAD);
        CHECK(xl_document_destroy(document.get()) == XL_WRONG_THREAD);
    });
    other.join();
    xl::Document second(runtime);
    second.html("<div id=another></div>");
    CHECK(xl_document_set_text(second.get(), panel, "bad", 3) == XL_NOT_FOUND);
    document.html("<div id=new></div>");
    CHECK(xl_document_set_text(document.get(), panel, "bad", 3) == XL_NOT_FOUND);
    CHECK(document.query("#new") != panel);
    CHECK(document.evaluate("typeof saved") == XL_OK && document.result() == "undefined");
    view.document(nullptr);
    std::cout << "HTML/CSS/DOM/JS, retained block updates, AX snapshots, identities, navigation and budgets passed\n";
}
