#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
void render(xl::View& view) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags;
    xl_status status;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
    CHECK(status == XL_OK);
}
std::vector<unsigned char> pixels(xl::View& view) {
    auto stats = view.stats();
    std::vector<unsigned char> result(size_t(stats.pixel_width)*stats.pixel_height*4);
    CHECK(xl_view_copy_rgba(view.get(), result.data(), result.size(), size_t(stats.pixel_width)*4) == XL_OK);
    return result;
}
std::string style(xl::Document& doc, xl_node_id node, const char* property) {
    size_t size;
    const auto status = xl_document_get_style(doc.get(), node, property, nullptr, 0, &size);
    CHECK(status == XL_OK || status == XL_BUFFER_TOO_SMALL);
    std::string result(size, '\0');
    CHECK(xl_document_get_style(doc.get(), node, property, result.data(), result.size(), &size) == XL_OK);
    return result;
}
xl_node_bounds bounds(xl::View& view, xl_node_id node) {
    xl_node_bounds result;
    CHECK(xl_view_node_bounds(view.get(), node, &result) == XL_OK);
    return result;
}
}
int main() {
    xl::Runtime runtime;
    xl::Document document(runtime);
    std::ifstream input(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    for (const auto* family : {"test", "two words", "A,B", "semi;colon", "quote\"name"})
        CHECK(!font.empty() && xl_document_add_font(document.get(), family, font.data(), font.size()) == XL_OK);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 360; options.height = 260;
    xl::View view(runtime, &options); view.document(&document);
    const std::string base = "<!doctype html><style>body,p,div{margin:0}p{width:300px}</style>";
    const auto page = [&](const std::string& content) { document.html(base+content); render(view); };
    const auto equivalent = [&](const std::string& a, const std::string& b) {
        page(a); const auto first = pixels(view);
        page(b);
        if (first != pixels(view)) { std::cerr << "Unequal pages:\n" << a << '\n' << b << '\n'; std::exit(1); }
    };
    const auto fonts = [&](const std::string& a, const std::string& b) {
        equivalent("<p style=\""+a+"\">office e\xcc\x81 words<br>Next</p>",
                   "<p style=\""+b+"\">office e\xcc\x81 words<br>Next</p>");
    };
    fonts("font:italic bold 24px/1.5 test", "font-style:italic;font-weight:bold;font-size:24px;line-height:1.5;font-family:test");
    CHECK(bounds(view, document.query("p")).height == 72);
    fonts("font:bold italic 24px / 36px test", "font-style:italic;font-weight:700;font-size:24px;line-height:36px;font-family:test");
    fonts("font:oblique 24px/150% test", "font-style:oblique;font-size:24px;line-height:150%;font-family:test");
    fonts("font:normal normal normal normal 20px test", "font-size:20px;font-family:test");
    fonts("font:600 20pt/2 test", "font-weight:600;font-size:20pt;line-height:2;font-family:test");
    fonts("font:0/0 test", "font-size:0;line-height:0;font-family:test");
    fonts("font:20px/0 test", "font-size:20px;line-height:0;font-family:test");
    fonts("font:20px/2 test;font-size:30px", "font-size:30px;line-height:2;font-family:test");
    fonts("font-size:30px;font:20px/2 test", "font-size:20px;line-height:2;font-family:test");
    fonts("font-weight:bold;font-style:italic;line-height:4;font-stretch:condensed;font:20px test", "font-size:20px;font-family:test");
    fonts("font:20px/2 test!important;font-size:30px", "font-size:20px;line-height:2;font-family:test");
    fonts("font:20px/2 test!important;font-size:30px!important", "font-size:30px;line-height:2;font-family:test");
    fonts("font:condensed 20px test", "font-stretch:75%;font-size:20px;font-family:test");
    fonts("font:20px 'two words',test", "font-size:20px;font-family:'two words',test");
    fonts("font:20px two words,test", "font-size:20px;font-family:'two words',test");
    fonts("font:20px t\\65 st", "font-size:20px;font-family:test");
    fonts("font:20px A\\,B", "font-size:20px;font-family:'A,B'");
    fonts("font:20px semi\\;colon", "font-size:20px;font-family:'semi;colon'");
    fonts("font:20px 'quote\\\"name'", "font-size:20px;font-family:'quote\\\"name'");
    fonts("font:italic/**/bold/**/20px/**/ / /**/2/**/test", "font-style:italic;font-weight:bold;font-size:20px;line-height:2;font-family:test");
    fonts("font-size:20px;font-family:two words,test", "font-size:20px;font-family:'two words',test");
    equivalent("<style>p{font:italic 20px/2 A\\,B}</style><p>office</p>", "<p style='font-style:italic;font-size:20px;line-height:2;font-family:&quot;A,B&quot;'>office</p>");
    equivalent("<style>p{font-size:20px;font-family:two words,test}</style><p>office</p>", "<p style='font-size:20px;font-family:&quot;two words&quot;,test'>office</p>");
    fonts("font:20px test;FONT:italic 30px/2 test", "font-style:italic;font-size:30px;line-height:2;font-family:test");
    for (const auto* invalid : {"", "12px", "bold test", "20px/ test", "20px/2", "20px test,", "20px ,test", "-2px test",
            "italic italic 20px test", "normal normal normal normal normal 20px test", "20px test 'two words'", "20px inherit", "20px test/2", "20px/**//**/2 test"}) {
        fonts(std::string("font:24px/2 test;font:")+invalid, "font-size:24px;line-height:2;font-family:test");
    }
    for (const auto* global : {"inherit", "unset"}) {
        equivalent(std::string("<div style='font:italic bold 30px/2 test'><p style='font:")+global+"'>A<br>B</p></div>",
                   "<div style='font:italic bold 30px/2 test'><p>A<br>B</p></div>");
    }
    equivalent("<div style='font:italic bold 30px/2 test'><p style='font:initial'>A<br>B</p></div>",
               "<div style='font:italic bold 30px/2 test'><p style='font-size:16px;font-weight:400;font-style:normal;line-height:normal;font-family:serif'>A<br>B</p></div>");
    equivalent("<div style='font-size:30px'><p style='font-size:initial'>A</p></div>", "<p style='font-size:16px'>A</p>");
    equivalent("<div style='font-size:30px'><p style='font:150%/1.5 test'>A<br>B</p></div>", "<p style='font:45px/1.5 test'>A<br>B</p>");
    equivalent("<style>html{font-size:20px}p{font:2rem/1.5 test;width:10rem}</style><p>A<br>B</p>",
               "<style>html{font-size:20px}p{font:40px/1.5 test;width:200px}</style><p>A<br>B</p>");
    equivalent("<style>html{font-size:2rem}</style><p style='font:1rem/1 test'>A</p>", "<p style='font:32px/1 test'>A</p>");
    for (const auto& [keyword, size] : std::vector<std::pair<std::string, int>>{{"xx-small",9},{"x-small",10},{"small",13},{"medium",16},{"large",18},{"x-large",24},{"xx-large",32},{"xxx-large",48}})
        fonts("font:"+keyword+"/1.5 test", "font-size:"+std::to_string(size)+"px;line-height:1.5;font-family:test");

    // Source order and specificity are retained across sheets and inline CSS.
    page("<style>p{font:18px/1 test}.rule{font:22px/2 test}#target{font-size:30px}</style><style>p{font:10px test}</style><p id=target class=rule>A</p>");
    auto target = document.query("#target");
    CHECK(style(document, target, "font-size") == "30px" && style(document, target, "line-height") == "2");
    document.attribute(target, "class", ""); render(view);
    CHECK(style(document, target, "font-size") == "30px" && style(document, target, "line-height") == "normal");
    page("<style>#target{font:20px/2 test!important}p{font-size:30px}</style><p id=target style='font:12px/1 test'>A</p>");
    target = document.query("#target");
    CHECK(style(document, target, "font-size") == "20px");
    document.attribute(target, "style", "font:24px/2 test!important"); render(view);
    CHECK(style(document, target, "font-size") == "24px");
    CHECK(style(document, target, "font-weight") == "normal");
    CHECK(style(document, target, "font-kerning") == "initial");

    // Detached inline lists remain queryable and can be reparsed, reattached,
    // and destroyed after navigation without dangling cascade references.
    document.remove(target);
    CHECK(style(document, target, "font-size") == "24px");
    document.attribute(target, "style", "font:18px/1 test");
    CHECK(style(document, target, "font-size") == "18px");
    document.append(document.query("body"), target); render(view);
    CHECK(style(document, target, "font-size") == "20px");
    page("<p id=target>A<span id=child>B</span></p>");
    target = document.query("#target");
    const auto child = document.query("#child");
    for (int i = 0; i < 24; ++i) {
        document.attribute(target, "style", "font:"+std::to_string(18+i%4)+"px/2 test;color:#123456");
        render(view);
        CHECK(style(document, target, "font-size") == std::to_string(18+i%4)+"px");
    }
    CHECK(document.evaluate("document.getElementById('target').style.cssText='font:24px/2 test';document.getElementById('target').style.setProperty('font-size','30px')") == XL_OK);
    render(view);
    CHECK(style(document, target, "font-size") == "30px");
    auto changed = pixels(view);
    xl::View full(runtime, &options); full.document(&document); render(full);
    CHECK(changed == pixels(full));
    auto text = view.hit_text(bounds(view,child).x+1, bounds(view,child).y+2);
    CHECK(!view.text_rects(text.node,0,1).empty());
    uint32_t flags;
    CHECK(xl_view_render(view.get(), &flags) == XL_OK && flags == 0);
    document.remove(target); document.remove(child); // Overlapping detached roots.
    document.attribute(child, "style", "font:18px test");
    CHECK(style(document, child, "font-size") == "18px");

    page("<p style='font:small-caps 20px test'>Abc</p>");
    CHECK(document.stats().unsupported_layout_nodes > 0);
    page("<p style='font:caption'>Abc</p>");
    CHECK(document.stats().unsupported_layout_nodes > 0);
    CHECK(style(document, document.query("p"), "font") == "caption");
    const std::string authored = "font:italic 20px/2 'two words',test";
    document.attribute(document.query("p"), "style", authored); render(view);
    size_t length;
    CHECK(xl_document_get_attribute(document.get(),document.query("p"),"style",nullptr,0,&length)==XL_BUFFER_TOO_SMALL);
    std::string attribute(length, '\0');
    CHECK(xl_document_get_attribute(document.get(),document.query("p"),"style",attribute.data(),attribute.size(),&length)==XL_OK);
    CHECK(attribute==authored);
    std::cout << "Font cascade, inheritance, source ranges and rendering passed\n";
}
