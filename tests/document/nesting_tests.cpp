#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>
#include <vector>
#define CHECK(value)                                                                                                   \
    do {                                                                                                               \
        if (!(value)) {                                                                                                \
            std::cerr << __LINE__ << ": " #value "\n";                                                                 \
            std::exit(1);                                                                                              \
        }                                                                                                              \
    } while (false)
int main(int argc, char **argv) {
    xl::Runtime runtime;
    xl::Document document(runtime);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN;
    options.width = 240;
    options.height = 200;
    xl::View view(runtime, &options);
    view.document(&document);
    view.ax_updates(true);
    if (argc == 2 && std::string_view(argv[1]) == "--gpu")
        view.graphics_backend(XL_GRAPHICS_SWIFTSHADER);
    const auto page = [&](const std::string &source) { document.html("<style>body{margin:0}</style>" + source); };
    const auto style = [&](const char *selector, const char *property, const std::string &expected,
                           const std::source_location &at = std::source_location::current()) {
        size_t size = 0;
        const auto node = document.query(selector);
        auto status = xl_document_get_style(document.get(), node, property, nullptr, 0, &size);
        if (status != XL_OK && status != XL_BUFFER_TOO_SMALL) {
            std::cerr << at.line() << " style status " << status << ' ' << selector << ' ' << property << '\n';
            std::exit(1);
        }
        std::string value(size, '\0');
        CHECK(xl_document_get_style(document.get(), node, property, value.data(), size, &size) == XL_OK);
        if (value != expected) {
            std::cerr << at.line() << ' ' << selector << ' ' << property << " expected " << expected << " got " << value
                      << '\n';
            std::exit(1);
        }
    };
    const auto script = [&](std::string_view source, const std::string &expected,
                            const std::source_location &at = std::source_location::current()) {
        const auto status = document.evaluate(source);
        if (status != XL_OK || document.result() != expected) {
            std::cerr << at.line() << " script " << status << ' ' << source << " expected " << expected << " got "
                      << document.result() << '\n';
            std::exit(1);
        }
    };
    const auto render = [&] {
        uint32_t flags;
        xl_status status;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < end);
            std::this_thread::yield();
        }
        if (status != XL_OK)
            std::cerr << "render " << status << '\n';
        CHECK(status == XL_OK);
        return flags;
    };
    const auto pixels = [&] {
        std::vector<unsigned char> result(240 * 200 * 4);
        CHECK(xl_view_copy_rgba(view.get(), result.data(), result.size(), 240 * 4) == XL_OK);
        return result;
    };
    const auto pixel = [&](int x, int y) {
        const auto data = pixels();
        const auto i = size_t(y * 240 + x) * 4;
        return uint32_t(data[i]) << 24 | uint32_t(data[i + 1]) << 16 | uint32_t(data[i + 2]) << 8 | data[i + 3];
    };
    const auto bounds = [&](const char *selector) {
        xl_node_bounds result{};
        CHECK(xl_view_node_bounds(view.get(), document.query(selector), &result) == XL_OK);
        return result;
    };
    const auto ax = [&](xl_node_id id) {
        size_t size;
        CHECK(xl_view_ax_count(view.get(), &size) == XL_OK);
        for (size_t i = 0; i < size; ++i) {
            xl_ax_node node{};
            CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
            if (node.node == id)
                return node;
        }
        CHECK(false);
        return xl_ax_node{};
    };

    page("<style>#x{color:red;&.active{color:blue}color:green;&.active{color:lime}}</style><div id=x "
         "class=active></div>");
    style("#x", "color", "lime");
    document.attribute(document.query("#x"), "class", "");
    style("#x", "color", "green");
    page("<style>&{color:blue}html{color:red}</style><div id=x></div>");
    style("html", "color", "red");
    page("<style>.parent{span{color:blue}> p{color:green}+ .peer{color:lime}~ .later{color:red}}</style><div "
         "class=parent><span id=x></span><p id=p></p><div><p id=deep></p></div></div><div class=peer></div><div "
         "class=later></div>");
    style("#x", "color", "blue");
    style("#p", "color", "green");
    style(".peer", "color", "lime");
    style(".later", "color", "red");
    page("<style>:is(.a).yes ~ .last{color:blue}:is(.outer) .a{background:lime}</style><div class=outer><div class='a "
         "yes'></div><div class='a no'></div><div class=last id=x></div><section><div class=a "
         "id=y></div></section></div>");
    style("#x", "color", "blue");
    style("#y", "background-color", "lime");
    page("<style>.p{&:has(> :is(.yes)){color:blue}&:not(.no) ~ .later{background:lime}}</style><div class=p><span "
         "class=no></span><span class=yes></span></div><div class=no></div><div id=x class=later></div>");
    style(".p", "color", "blue");
    style("#x", "background-color", "lime");
    page("<style>.low,#missing{&.active{color:blue}}.low.active{color:red}</style><div id=x class='low active'></div>");
    style("#x", "color", "blue");
    page("<style>#missing::before,.low{&.active{color:blue}}.low.active{color:red}</style><div id=x class='low "
         "active'></div>");
    style("#x", "color", "blue");
    page("<style>.a,.a::before{content:'A';color:red;@media(width<300px){color:blue}&{background:lime}color:green}</"
         "style><div class=a id=x></div>");
    style("#x", "color", "green");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "color") == "green");
    page("<style>:where(#x){&.active{color:blue}}.active{color:red}</style><div id=x class=active></div>");
    style("#x", "color", "red");
    page("<style>.a,#unused{& &{color:blue}}.a .a{color:red}</style><div class=a><span class=a id=x></span></div>");
    style("#x", "color", "blue");
    page("<style>.parent{.child{&.active{color:blue}color:green}}</style><div class=parent><span id=x class='child "
         "active'></span><span id=y class=child></span></div>");
    style("#x", "color", "blue");
    style("#y", "color", "green");
    page("<style>.parent{&:not(.disabled){color:blue}:is(&,.alternate).active{background-color:lime}}</style><div "
         "class=parent id=x></div><div class='alternate active' id=y></div>");
    style("#x", "color", "blue");
    style("#y", "background-color", "lime");
    page("<style>#p{section:has(> .flag){color:blue}}[data-a='&']{&.active{color:green}}</style><div id=p><section "
         "id=x><span class=flag></span></section></div><div id=y data-a='&' class=active></div>");
    style("#x", "color", "blue");
    style("#y", "color", "green");
    page("<style>.a\\:b{&.active{color:blue}}#x{--tokens:{a:b;c:d};--text:'&;{}';/* & {ignored} "
         "*/&[data-x='{};']{content:'&;{}';color:green}}</style><div class='a:b active' id=y></div><div id=x "
         "data-x='{};'></div>");
    style("#y", "color", "blue");
    style("#x", "color", "green");
    style("#x", "--tokens", "{a:b;c:d}");
    page("<style>.a{color:red;&suffix{color:blue}&.active, ?? {color:green!important}}</style><div id=x class='a "
         "active'></div>");
    style("#x", "color", "red");
    page("<style>.a{color:red;&.active{color:blue!important}color:green!important}</style><div id=x class='a active' "
         "style='color:yellow'></div>");
    style("#x", "color", "blue");
    page("<style>.a{&.active{color:blue}}</style><div id=x class='a active' style='color:yellow'></div>");
    style("#x", "color", "yellow");

    page("<style>@layer a,b;@layer a{#x{--r:20px;&.active{border-radius:var(--r);background:blue}}}@layer "
         "b{#x{&.active{background:lime;border-radius:revert-layer}}}</style><div id=x class=active></div>");
    style("#x", "background-color", "lime");
    style("#x", "border-top-left-radius", "20px 20px");
    page("<style>#x{color:red;@media(width<300px){color:blue;&.active{background:lime}@media(height>100px){width:40px}}"
         "color:green}</style><div id=x class=active></div>");
    style("#x", "color", "green");
    style("#x", "width", "40px");
    style("#x", "background-color", "lime");
    CHECK(xl_view_resize(view.get(), 400, 200) == XL_OK);
    render();
    style("#x", "color", "green");
    size_t ignored = 0;
    CHECK(xl_document_get_style(document.get(), document.query("#x"), "width", nullptr, 0, &ignored) == XL_NOT_FOUND);
    CHECK(xl_view_resize(view.get(), 240, 200) == XL_OK);
    render();
    style("#x", "width", "40px");
    page("<style>#x{width:30px;height:30px;@media(width<200px){&{border:3px solid "
         "blue;border-radius:12px}}}</style><div id=x></div>");
    render();
    CHECK(xl_view_resize(view.get(), 160, 200) == XL_OK);
    render();
    style("#x", "border-top-left-radius", "12px 12px");
    CHECK(xl_view_resize(view.get(), 240, 200) == XL_OK);
    render();
    page("<style>#x{color:red;@unknown "
         "x{color:lime;&{width:100px}}@supports(unknown:value){&{color:blue}}color:green}</style><div id=x></div>");
    style("#x", "color", "green");

    page("<style>#x{--word:'A';&::before{content:var(--word);color:red;@media(width<300px){color:blue}color:green}&::"
         "after{content:'Z'}}#x::before{color:lime}</style><div id=x></div>");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"A\"");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "color") == "lime");
    page("<style>#x::before{content:'A';color:red;@media(width<300px){color:blue}color:green}</style><div id=x></div>");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "color") == "green");
    page("<style>#x{input:checked + span{color:blue}input:focus{background:lime}}</style><div id=x><input "
         "type=checkbox id=c><span id=s></span></div>");
    script("document.getElementById('c').checked=true;getComputedStyle(document.getElementById('s')).getPropertyValue('"
           "color')",
           "blue");
    script("document.getElementById('c').focus();getComputedStyle(document.getElementById('c')).getPropertyValue('"
           "background-color')",
           "lime");

    page("<style "
         "id=css>#x{width:80px;height:80px;background:red;&.active{width:40px;height:40px;background:blue;border-"
         "radius:20px}}</style><div id=x class=active role=button aria-label=Target></div>");
    render();
    CHECK(pixel(1, 1) == 0xffffffff && pixel(20, 20) == 0x0000ffff);
    CHECK(bounds("#x").width == 40 && ax(document.query("#x")).bounds.width == 40);
    CHECK(view.render() == 0);
    xl::AXUpdate previous, batch;
    while (view.take_ax_update(batch))
        previous = std::move(batch);
    CHECK(previous);
    document.attribute(document.query("#x"), "class", "");
    render();
    CHECK(pixel(1, 1) == 0xff0000ff);
    CHECK(bounds("#x").width == 80 && ax(document.query("#x")).bounds.width == 80);
    const auto incremental = pixels();
    CHECK(xl_view_invalidate(view.get(), 0, 0, 240, 200) == XL_OK);
    render();
    CHECK(pixels() == incremental);
    CHECK(view.render() == 0);
    bool old = false;
    for (size_t i = 0; i < previous.info().node_count; ++i)
        if (previous.node(i).node == document.query("#x"))
            old = previous.node(i).bounds.width == 40;
    CHECK(old);
    script("let css=document.getElementById('css');let "
           "original=css.textContent;css.textContent='#x{&{width:60px;height:80px;background:lime}}';getComputedStyle("
           "document.getElementById('x')).getPropertyValue('width')",
           "60px");
    render();
    CHECK(pixel(1, 1) == 0x00ff00ff);
    script("css.textContent=original;let "
           "box=document.getElementById('x');box.className='active';getComputedStyle(box).getPropertyValue('width')",
           "40px");
    render();
    script("css.remove();getComputedStyle(box).getPropertyValue('width')", "");
    script("document.querySelector('head').appendChild(css);getComputedStyle(box).getPropertyValue('width')", "40px");
    render();
    for (int i = 0; i < 16; ++i) {
        document.attribute(document.query("#x"), "class", i % 2 ? "active" : "");
        render();
        CHECK(bounds("#x").width == (i % 2 ? 40 : 80));
        CHECK(view.render() == 0);
    }

    page("<style>#x{&{width:var(--width)}}</style><div id=x></div>");
    for (int i = 0; i < 2060; ++i) {
        const auto value = std::to_string(i) + "px";
        document.attribute(document.query("#x"), "style", "--width:" + value);
        style("#x", "width", value);
    }
    xl::Content first("#x{&.active{width:25px;background:blue}}", "text/css"),
        second("#x{&.active{width:35px;background:lime}}", "text/css");
    runtime.mount("xl-webengine-ui://app/theme.css", first);
    page("<link rel=stylesheet href=theme.css><div id=x class=active></div>");
    style("#x", "width", "25px");
    runtime.mount("xl-webengine-ui://app/theme.css", second);
    style("#x", "width", "35px");
    runtime.unmount("xl-webengine-ui://app/theme.css");
    ignored = 0;
    CHECK(xl_document_get_style(document.get(), document.query("#x"), "width", nullptr, 0, &ignored) == XL_NOT_FOUND);
    page("<style>#x{&{width:20px}}</style><div id=x></div>");
    style("#x", "width", "20px");
    xl_resource_stats resources{};
    CHECK(xl_runtime_resource_stats(runtime.get(), &resources, sizeof(resources)) == XL_OK);
    CHECK(resources.decompressions == 0 && resources.worker_threads == 0);
    if (argc == 2 && std::string_view(argv[1]) != "--gpu") {
        std::ifstream font_file(XL_TEST_FONT, std::ios::binary);
        const std::vector<char> font{std::istreambuf_iterator<char>(font_file), std::istreambuf_iterator<char>()};
        CHECK(!font.empty());
        CHECK(xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
        std::ifstream file(argv[1], std::ios::binary);
        const std::string html{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        CHECK(!html.empty());
        document.html(html);
        render();
        style(".nav-link.active", "font-weight", "600");
        script("let "
               "reference=document.createElement('i');reference.style.color='var(--accent)';document.body.appendChild("
               "reference);getComputedStyle(document.querySelector('.nav-link.active')).getPropertyValue('color')==="
               "getComputedStyle(reference).getPropertyValue('color')",
               "true");
        script("document.querySelector('.nav-link.active').classList.remove('active');getComputedStyle(document."
               "querySelector('.nav-link')).getPropertyValue('font-weight')",
               "500");
        render();
        CHECK(view.render() == 0);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--gpu")
        CHECK(view.graphics_info().active == XL_GRAPHICS_SWIFTSHADER);
    std::cout << "CSS nesting, parent specificity, source order, conditions/layers, pseudo and dynamic cache/pixels/AX "
                 "passed\n";
}
