#include <XLWebEngineUI/xl.hpp>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iterator>
#include <vector>
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
int main(int argc, char** argv) {
    xl::Runtime runtime;
    xl::Document document(runtime);
    xl_view_options options; xl_view_options_init(&options, sizeof(options));
    options.kind = XL_VIEW_OFFSCREEN; options.width = 128; options.height = 96; options.background_argb = 0xffffffff;
    xl::View view(runtime, &options); view.document(&document);
    const auto render = [&] {
        uint32_t flags; CHECK(xl_view_render(view.get(), &flags) == XL_OK);
        const auto stats = view.stats(); std::vector<unsigned char> bytes(size_t(stats.pixel_width) * stats.pixel_height * 4);
        CHECK(xl_view_copy_rgba(view.get(), bytes.data(), bytes.size(), size_t(stats.pixel_width) * 4) == XL_OK);
        return bytes;
    };
    const auto pixel = [&](int x, int y) {
        const auto data = render(); const auto offset = size_t(y * 128 + x) * 4;
        return uint32_t(data[offset]) << 24 | uint32_t(data[offset + 1]) << 16 | uint32_t(data[offset + 2]) << 8 | data[offset + 3];
    };
    const auto page = [&](std::string source) { document.html("<style>body{margin:0}</style>" + source); };
    const auto width = [&](const char* selector) {
        render(); xl_node_bounds bounds; CHECK(xl_view_node_bounds(view.get(), document.query(selector), &bounds) == XL_OK);
        return bounds.width;
    };
    const auto script = [&](const char* code, const char* expected) {
        CHECK(document.evaluate(code) == XL_OK);
        if (document.result() != expected) { std::cerr << document.result() << " != " << expected << '\n'; std::exit(1); }
    };
    page("<style>:root{--Tone:red;--tone:blue;--size:20px}.box{height:12px;width:var(--size);background:var(--Tone)}</style>"
         "<div id=a class=box></div><div id=b class=box style='--size:30px;--Tone:var(--tone)'></div>");
    CHECK(pixel(2, 2) == 0xff0000ff); CHECK(pixel(2, 14) == 0x0000ffff);
    CHECK(width("#a") == 20); CHECK(width("#b") == 30);
    script("getComputedStyle(document.getElementById('b')).getPropertyValue('--Tone').trim()", "blue");
    script("getComputedStyle(document.getElementById('b')).getPropertyValue('--tone').trim()", "blue");
    script("document.getElementById('b').style.setProperty('--size','40px');'changed'", "changed");
    CHECK(width("#b") == 40); CHECK(width("#a") == 20);
    CHECK(view.render() == 0); // No new style mutation: retain the same frame.

    page("<style>#parent{--base:red;--alias:var(--base)}#child{--base:blue;background:var(--alias);height:12px}</style>"
         "<div id=parent><div id=child></div></div>");
    CHECK(pixel(2, 2) == 0xff0000ff); // Resolve before inheritance, not in the child.
    page("<style>div{--x:red!important}#x{--x:blue}</style><div id=x style='--x:green;background:var(--x);height:12px'></div>");
    CHECK(pixel(2, 2) == 0xff0000ff);
    script("document.getElementById('x').style.setProperty('--x','blue','important');'changed'", "changed");
    CHECK(pixel(2, 2) == 0x0000ffff);
    page("<div style='--c:blue;background:red!important;background:var(--c);height:12px'></div>");
    CHECK(pixel(2, 2) == 0xff0000ff);
    page("<div style='--c:blue;background:var(--c)!important;background:red;height:12px'></div>");
    CHECK(pixel(2, 2) == 0x0000ffff);

    page("<div style='--fallback:lime;background-color:var(--missing,var(--fallback,red));height:12px'></div>");
    CHECK(pixel(2, 2) == 0x00ff00ff);
    page("<div style='--good:lime;--a:var(--good,var(--a));--b:var(--a,blue);background-color:var(--b);height:12px'></div>");
    CHECK(pixel(2, 2) == 0x0000ffff); // A reference in an unused fallback still forms a cycle.
    page("<div style='--a:var(--b);--b:var(--a);background-color:var(--a,red);height:12px'></div>");
    CHECK(pixel(2, 2) == 0xff0000ff);
    page("<div style='--a:;background-color:lime;background-color:var(--a,red);height:12px'></div>");
    CHECK(pixel(2, 2) == 0xffffffff); // Empty is valid; it does not select the fallback.
    page("<div style='--a:initial;background-color:var(--a,red);height:12px'></div>");
    CHECK(pixel(2, 2) == 0xff0000ff);

    page("<div id=x style='width:11px;width:var(--missing);height:12px;background-color:lime;background-color:var(--bad)'></div>");
    CHECK(width("#x") == 128); CHECK(pixel(2, 2) == 0xffffffff);
    page("<div style='background-color:lime;background-color:var(not-custom);height:12px'></div>");
    CHECK(pixel(2, 2) == 0x00ff00ff); // Malformed var() is discarded at parse time.
    page("<div id=x style='--n:12;width:9px;width:var(--n)px;height:12px'></div>");
    CHECK(width("#x") == 128); // Preserve number/identifier token boundaries.
    page(R"HTML(<div style='--Tone:blue;background:v\61r(/*name*/--\54 one);height:12px'></div>)HTML");
    CHECK(pixel(2, 2) == 0x0000ffff);
    page("<div style='--r:rgb(255,0,0);background:var(--missing,var(--r));height:12px'></div>");
    CHECK(pixel(2, 2) == 0xff0000ff);
    page("<div style='--a:var(--b);--b:lime;background:var(--a);height:12px'></div>");
    CHECK(pixel(2, 2) == 0x00ff00ff);
    page("<div style='--a:red'><div style='--a:unset;background:var(--a);height:12px'></div></div>");
    CHECK(pixel(2, 2) == 0xff0000ff);
    page("<div style='--a:red'><div style='--a:initial;background:var(--a,lime);height:12px'></div></div>");
    CHECK(pixel(2, 2) == 0x00ff00ff);
    page("<div style='color:red'><div style='color:var(--missing,initial);background:currentcolor;height:12px'></div></div>");
    CHECK(pixel(2, 2) == 0x000000ff);
    page("<div style='--a:20px;background-color:red;background-color:var(--a);height:12px'></div>");
    CHECK(pixel(2, 2) == 0xffffffff);
    page("<div style='--a:blue;height:12px;background:var(--a'></div>"); // CSS EOF closing.
    CHECK(pixel(2, 2) == 0x0000ffff);
    page("<div id=x style='--a:1em;font-size:20px;width:var(--a);height:12px'></div>");
    CHECK(width("#x") == 20);

    page("<style>.x{--m:4px;margin:var(--m);margin-left:9px;height:12px;background:red}</style><div id=x class=x></div>");
    render(); xl_node_bounds bounds; CHECK(xl_view_node_bounds(view.get(), document.query("#x"), &bounds) == XL_OK);
    CHECK(bounds.x == 9);
    page("<style>.x{--m:4px;margin-left:9px;margin:var(--m);height:12px;background:red}</style><div id=x class=x></div>");
    render(); CHECK(xl_view_node_bounds(view.get(), document.query("#x"), &bounds) == XL_OK); CHECK(bounds.x == 4);
    page("<div style='--border:3px solid red;border:var(--border);border-left-color:blue;height:12px'></div>");
    CHECK(pixel(1, 6) == 0x0000ffff); CHECK(pixel(6, 1) == 0xff0000ff);
    page("<div style='border:3px solid red;border:var(--missing);background:blue;height:12px'></div>");
    CHECK(pixel(1, 1) == 0x0000ffff);
    page("<div id=x style='--tracks:20px 30px;display:grid;grid-template-columns:var(--tracks)'><div id=a></div><div id=b></div></div>");
    CHECK(width("#a") == 20); CHECK(width("#b") == 30);
    page("<div id=x style='--gap:7px;display:flex;gap:var(--gap)'><div id=a style='width:20px'></div><div id=b style='width:30px'></div></div>");
    render(); CHECK(xl_view_node_bounds(view.get(), document.query("#b"), &bounds) == XL_OK); CHECK(bounds.x == 27);
    page("<div id=x style='--font:italic bold 12px/16px serif;font:var(--font)'></div>");
    script("getComputedStyle(document.getElementById('x')).getPropertyValue('font-size')", "12px");

    std::string expansion = "<div id=x style='--p0:x;";
    for (int index = 1; index <= 20; ++index) {
        const auto parent = "var(--p" + std::to_string(index - 1) + ')';
        expansion += "--p" + std::to_string(index) + ':' + parent + parent + ';';
    }
    expansion += "background:var(--p20,red);height:12px'></div>";
    page(expansion); CHECK(pixel(2, 2) == 0xff0000ff);
    script("getComputedStyle(document.getElementById('x')).getPropertyValue('--p20')", "");

    page("<div id=parent style='--a:red'><div id=child style='background:var(--a,blue);height:12px'></div></div>");
    CHECK(pixel(2, 2) == 0xff0000ff);
    script("globalThis.detached=document.getElementById('child');detached.remove();getComputedStyle(detached).getPropertyValue('--a')", "");
    script("document.body.appendChild(detached);'attached'", "attached");
    CHECK(pixel(2, 2) == 0x0000ffff);

    page("<style>#toggle:checked~div{--theme:blue}div{--theme:red;height:12px;background:var(--theme)}</style>"
         "<input id=toggle type=checkbox hidden><div></div>");
    CHECK(pixel(2, 2) == 0xff0000ff);
    script("document.getElementById('toggle').checked=true;'changed'", "changed");
    CHECK(pixel(2, 2) == 0x0000ffff);
    for (int index = 0; index < 40; ++index) {
        document.attribute(document.query("div"), "style", index % 2 ? "--theme:red" : "--theme:lime!important");
        CHECK(pixel(2, 2) == (index % 2 ? 0xff0000ffu : 0x00ff00ffu));
    }
    {
        xl::Runtime second;
        const char* sheet = "xl-webengine-ui://app/theme.css";
        xl::Content red(":root{--theme:red}#x{height:12px;background:var(--theme)}", "text/css");
        xl::Content blue(":root{--theme:blue}#x{height:12px;background:var(--theme)}", "text/css");
        runtime.mount(sheet, red); second.mount(sheet, blue);
        document.html("<style>body{margin:0}</style><link rel=stylesheet href=theme.css><div id=x></div>", "xl-webengine-ui://app/index.html");
        CHECK(pixel(2, 2) == 0xff0000ff);
        xl::Document other(second);
        other.html("<link rel=stylesheet href=theme.css><div id=x></div>", "xl-webengine-ui://app/index.html");
        CHECK(other.evaluate("getComputedStyle(document.getElementById('x')).getPropertyValue('--theme').trim()") == XL_OK);
        CHECK(other.result() == "blue");
        runtime.mount(sheet, blue); CHECK(pixel(2, 2) == 0x0000ffff);
        runtime.mount(sheet, red); CHECK(pixel(2, 2) == 0xff0000ff);
        CHECK(other.evaluate("getComputedStyle(document.getElementById('x')).getPropertyValue('--theme').trim()") == XL_OK);
        CHECK(other.result() == "blue");
        runtime.unmount(sheet); CHECK(pixel(2, 2) == 0xffffffff);
    }
    if (argc == 2) {
        std::ifstream input(argv[1], std::ios::binary);
        const std::string html{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        CHECK(!html.empty()); document.html(html);
        script("getComputedStyle(document.querySelector('.page')).getPropertyValue('--bg').trim()", "#f6f7f9");
        script("getComputedStyle(document.querySelector('.site-header')).getPropertyValue('--bg').trim()", "#f6f7f9");
        script("document.querySelector('.theme-btn').click();getComputedStyle(document.querySelector('.page')).getPropertyValue('--bg').trim()", "#0d1117");
        script("document.querySelector('.theme-btn').click();getComputedStyle(document.querySelector('.page')).getPropertyValue('--bg').trim()", "#f6f7f9");
        script("document.querySelector('.page').getAttribute('class')", "page");
    }
    std::cout << "Custom property inheritance, cycles, cascade, tokens, dynamic themes and retained pixels passed\n";
}
