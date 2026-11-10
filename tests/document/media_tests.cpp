#include <XLWebEngineUI/xl.hpp>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>
#include <vector>
#ifdef _WIN32
#include "win_input.h"
#endif
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
    options.width = 800;
    options.height = 600;
    xl::View view(runtime, &options);
    view.document(&document);
    view.ax_updates(true);
    const auto script = [&](const char *source, const std::string &expected,
                            const std::source_location &at = std::source_location::current()) {
        const auto status = document.evaluate(source);
        if (status != XL_OK || document.result() != expected) {
            std::cerr << at.line() << " status " << status << ' ' << source << " expected " << expected << " got "
                      << document.result() << '\n';
            std::exit(1);
        }
    };
    const auto style = [&](const char *selector, const char *name, const std::string &expected,
                           const std::source_location &at = std::source_location::current()) {
        size_t size = 0;
        const auto id = document.query(selector);
        auto status = xl_document_get_style(document.get(), id, name, nullptr, 0, &size);
        if (status != XL_OK && status != XL_BUFFER_TOO_SMALL) {
            std::cerr << at.line() << ' ' << selector << ' ' << name << " query status " << status << '\n';
            std::exit(1);
        }
        std::string actual(size, '\0');
        CHECK(xl_document_get_style(document.get(), id, name, actual.data(), actual.size(), &size) == XL_OK);
        if (actual != expected) {
            std::cerr << at.line() << ' ' << selector << ' ' << name << " expected " << expected << " got " << actual
                      << '\n';
            std::exit(1);
        }
    };
    const auto render = [&] {
        uint32_t flags;
        xl_status status;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        if (status != XL_OK)
            std::cerr << "render status " << status << '\n';
        CHECK(status == XL_OK);
        return flags;
    };
    const auto page = [&](const std::string &html) { document.html("<style>body{margin:0}</style>" + html); };
    const auto resize = [&](int width, int height) { CHECK(xl_view_resize(view.get(), width, height) == XL_OK); };
    const auto pixel = [&](int x, int y) {
        const auto stats = view.stats();
        std::vector<unsigned char> image(size_t(stats.pixel_width) * stats.pixel_height * 4);
        CHECK(xl_view_copy_rgba(view.get(), image.data(), image.size(), size_t(stats.pixel_width) * 4) == XL_OK);
        const auto i = size_t(y * stats.pixel_width + x) * 4;
        return uint32_t(image[i]) << 24 | uint32_t(image[i + 1]) << 16 | uint32_t(image[i + 2]) << 8 | image[i + 3];
    };
    page("<style>#box{width:30px;height:20px;background:red}@media screen and (width >= "
         "700px){#box{background:lime}}@media (width < 700px){#box{background:blue;width:40px}}@media "
         "print{#box{background:black}}</style><div id=box></div>");
    style("#box", "background-color", "lime");
    render();
    CHECK(pixel(2, 2) == 0x00ff00ff);
    CHECK(view.render() == 0);
    resize(600, 600);
    CHECK(document.matches_media("(width:600px)"));
    style("#box", "width", "40px");
    render();
    CHECK(pixel(2, 2) == 0x0000ffff);
    CHECK(view.render() == 0);
    resize(800, 600);
    style("#box", "background-color", "lime");
    render();
    CHECK(pixel(2, 2) == 0x00ff00ff);
    xl_resource_stats resources{};
    CHECK(xl_runtime_resource_stats(runtime.get(), &resources, sizeof(resources)) == XL_OK);
    CHECK(resources.decompressions == 0);
    page("<style>#box{width:20px;height:20px;background:red}@media (width >= 700px){@media "
         "(orientation:landscape){#box{background:blue}}}#box{background:lime}@media (future:yes), "
         "(height:600px){#box{width:33px}}@media not (future:yes){#box{height:999px}}</style><div id=box></div>");
    style("#box", "background-color", "lime");
    style("#box", "width", "33px");
    style("#box", "height", "20px");

    page("<style>@layer first,second;@layer second{#x{color:blue;--word:'B'}}@layer "
         "first{#x{color:red;--word:'A'}}@media (min-width:700px){@layer "
         "first{#x{color:green}}}#x::before{content:var(--word)}</style><div id=x></div>");
    style("#x", "color", "blue");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"B\"");
    page("<style>@layer first,second;@layer first{#x{color:red!important;--word:'A'!important}}@layer "
         "second{#x{color:blue!important;--word:'B'!important}}#x{color:green!important;--word:'C'!important}#x::"
         "before{content:var(--word)}</style><div id=x></div>");
    style("#x", "color", "red");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"A\"");
    document.attribute(document.query("#x"), "style", "color:purple!important;--word:'D'!important");
    style("#x", "color", "purple");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"D\"");
    page("<style>@layer first{#x{color:red!important;background:red}}#x{color:green;background:lime}</style><div id=x "
         "style='color:blue;background:blue'></div>");
    style("#x", "color", "red");
    style("#x", "background-color", "blue");
    page("<style>@layer parent{ @layer child{#x{color:red;background:red!important}} "
         "#x{color:blue;background:blue!important}}@layer last{#x{color:green;background:green!important}}</style><div "
         "id=x></div>");
    style("#x", "color", "green");
    style("#x", "background-color", "red");
    page("<style>@layer first.child,first.other,last;@layer first.other{#x{color:red}}@layer "
         "first.child{#x{color:blue}}@layer first{#x{background:lime}}@layer "
         "last{#x{background:blue}}@layer{#x{color:green}}@layer{#x{color:purple}}</style><div id=x></div>");
    style("#x", "color", "purple");
    style("#x", "background-color", "blue");
    page("<style>@layer "
         "theme{#x{color:red;--word:'Theme'}}#x{color:blue;--word:'Outside'}#x::before{content:var(--word)}</"
         "style><div id=x></div>");
    style("#x", "color", "blue");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"Outside\"");
    page("<style>@layer a,b;@layer a{#x{border:5px solid red;font:20px/24px test}}@layer "
         "b{#x{border-color:blue;font-size:24px}}</style><div id=x></div>");
    style("#x", "border-top-color", "blue");
    style("#x", "font-size", "24px");
    page("<style>@layer a,b;@layer a{#x{color:red;--word:'A';margin:4px;border:2px solid red}}@layer "
         "b{#x{color:blue;--word:'B';margin:8px;border:4px solid "
         "blue}#x{color:revert-layer;--word:revert-layer;margin-top:revert-layer;border-color:revert-layer}}#x::before{"
         "content:var(--word)}</style><div id=x></div>");
    style("#x", "color", "red");
    style("#x", "margin-top", "4px");
    style("#x", "margin-left", "8px");
    style("#x", "border-top-color", "red");
    style("#x", "border-top-width", "4px");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"A\"");
    page("<style>@layer a,b,c;@layer a{#x{color:red;--word:'A'}}@layer "
         "b{#x{color:blue;--word:'B';color:revert-layer!important;--word:revert-layer!important}}@layer "
         "c{#x{color:green!important;--word:'C'!important}}#x::before{content:var(--word)}</style><div id=x></div>");
    style("#x", "color", "red");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"A\"");
    page("<style>@layer a{#x{color:red!important}}#x{color:blue!important}</style><div id=x "
         "style='color:revert-layer!important'></div>");
    style("#x", "color", "red");
    page("<style>@layer a{#x{color:red}}#x{color:blue;color:revert-layer}</style><div id=x></div>");
    style("#x", "color", "red");
    page("<style>@layer a,b;@layer a{#x{color:red}}@layer "
         "b{#x{color:blue;color:var(--missing,revert-layer)}}</style><div id=x></div>");
    style("#x", "color", "red");
    page("<style>@media(width<700px){@layer hidden{#x{color:purple}}}@layer other{#x{color:blue}}@layer "
         "hidden{#x{color:red}}</style><div id=x></div>");
    style("#x", "color", "red");
    resize(600, 600);
    style("#x", "color", "blue");
    resize(800, 600);
    style("#x", "color", "red");

    const xl::Content css("@layer external{#x{background:blue}}@media (width < 700px){#x{height:27px}}", "text/css");
    runtime.mount("xl-webengine-ui://app/theme.css", css);
    page("<style>#x{width:20px;height:20px;background:red}</style><link id=sheet rel=stylesheet media='(width < "
         "700px)' href=theme.css><style media='(width >= 700px)'>#x{background:lime}</style><div id=x></div>");
    render();
    CHECK(pixel(2, 2) == 0x00ff00ff);
    resize(600, 600);
    render();
    style("#x", "height", "27px");
    CHECK(pixel(2, 2) == 0xff0000ff); // Unlayered rule outranks external layer.
    script("document.getElementById('sheet').setAttribute('media','print');'changed'", "changed");
    render();
    style("#x", "height", "20px");
    script("document.getElementById('sheet').setAttribute('media','all');document.getElementById('sheet').setAttribute("
           "'disabled','');'changed'",
           "changed");
    render();
    style("#x", "height", "20px");
    script("document.getElementById('sheet').removeAttribute('disabled');'changed'", "changed");
    render();
    style("#x", "height", "27px");
    runtime.unmount("xl-webengine-ui://app/theme.css");
    render();
    style("#x", "height", "20px");
    page("<style media='(width < 700px)'>#x{background:red}</style><style id=style media='(width >= "
         "700px)'>#x{background:blue}</style><div id=x></div>");
    style("#x", "background-color", "red");
    script("document.getElementById('style').setAttribute('media','all');'changed'", "changed");
    style("#x", "background-color", "blue");

    resize(800, 600);
    page("<style>body{--label:'Light'}@media (prefers-color-scheme:dark){body{--label:'Dark'}}@media "
         "(prefers-reduced-motion:reduce){#x{width:42px}}@media "
         "(prefers-contrast:more){#x{height:23px}}#x::before{content:var(--label)}</style><div id=x></div>");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"Light\"");
    xl_media_preferences prefs{sizeof(prefs),          XL_MEDIA_COLOR_DARK,    XL_MEDIA_MOTION_REDUCE,
                               XL_MEDIA_CONTRAST_MORE, XL_MEDIA_FORCED_ACTIVE, 0};
    document.media_preferences(&prefs);
    CHECK(document.matches_media("(prefers-color-scheme:dark) and (forced-colors:active)"));
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"Dark\"");
    style("#x", "width", "42px");
    style("#x", "height", "23px");
    document.media_preferences(nullptr);
    CHECK(document.matches_media("(prefers-color-scheme:light)"));
    prefs.reserved = 1;
    CHECK(xl_document_set_media_preferences(document.get(), &prefs, sizeof(prefs)) == XL_INVALID_ARGUMENT);
    prefs.reserved = 0;
    CHECK(xl_document_set_media_preferences(document.get(), &prefs, sizeof(prefs) - 1) == XL_ABI_MISMATCH);
    uint32_t match = 9;
    CHECK(xl_document_matches_media(document.get(), nullptr, 1, &match) == XL_INVALID_ARGUMENT);

    page("<div id=x></div>");
    render();
    script("globalThis.events=[];globalThis.mq=matchMedia('(max-width: "
           "700px)');globalThis.handler=function(e){events.push(e.matches+':'+e.media+':'+(e.target===mq)+':'+(this==="
           "mq)+':'+e.isTrusted+':'+e.bubbles+':'+e.cancelable)};mq.addEventListener('change',handler);mq.matches+':'+"
           "mq.media",
           "false:(max-width: 700px)");
    resize(600, 600);
    script("mq.matches+':'+events.length", "true:0");
    render();
    script("events.join('|')", "true:(max-width: 700px):true:true:true:false:false");
    CHECK(view.render() == 0);
    script("mq.removeEventListener('change',handler);mq.onchange=e=>events.push('on:'+e.matches);'ready'", "ready");
    resize(800, 600);
    render();
    script("events.at(-1)", "on:false");
    script("mq.onchange=null;globalThis.once=0;mq.addEventListener('change',()=>once++,{once:true});'ready'", "ready");
    resize(600, 600);
    render();
    resize(800, 600);
    render();
    script("once", "1");
    script("globalThis.legacy=0;globalThis.old=e=>legacy++;mq.addListener(old);mq.addListener(old);'ready'", "ready");
    resize(600, 600);
    render();
    script("legacy", "1");
    script("mq.removeListener(old);'ready'", "ready");
    resize(800, 600);
    render();
    script("legacy", "1");
    script("globalThis.coalesced=0;mq.onchange=e=>coalesced++;'ready'", "ready");
    resize(600, 600);
    resize(800, 600);
    render();
    script("coalesced", "0");
    script("globalThis.resizes=0;addEventListener('resize',()=>resizes++);globalThis.snapshot=new "
           "MediaQueryListEvent('change',{matches:true,media:'test'});snapshot.matches+':'+snapshot.media",
           "true:test");
    resize(750, 610);
    resize(740, 620);
    render();
    script("resizes+':'+innerWidth+':'+innerHeight", "1:740:620");
    script("globalThis.objectListener={handleEvent(e){events.push('object:'+e.matches)}};mq.addEventListener('change',"
           "objectListener);'ready'",
           "ready");
    resize(650, 620);
    render();
    script("events.at(-1)", "object:true");
    script("mq.onchange=null;mq.removeEventListener('change',objectListener);globalThis.many=Array.from({length:70},()="
           ">matchMedia('(width<700px)'));globalThis.delivered=0;for(let q of many)q.onchange=()=>delivered++;'ready'",
           "ready");
    resize(800, 620);
    render();
    script("delivered", "64");
    render();
    script("delivered", "70");
    script("for(let q of many)q.onchange=null;many=[];'cleared'", "cleared");
    script("globalThis.theme=matchMedia('(prefers-color-scheme:dark)');globalThis.themeEvents=[];theme.onchange=e=>"
           "themeEvents.push(e.matches);'ready'",
           "ready");
    prefs = {sizeof(prefs), XL_MEDIA_COLOR_DARK, 0, 0, 0, 0};
    document.media_preferences(&prefs);
    script("theme.matches+':'+themeEvents.length", "true:0");
    render();
    script("themeEvents.join(',')", "true");
    document.media_preferences(nullptr);
    render();
    script("themeEvents.join(',')", "true,false");
    // Navigation releases subscriptions and queued events, retaining View inputs.
    resize(800, 600);
    page("<style>@media (min-width:700px){#x{width:29px}}</style><div id=x></div>");
    style("#x", "width", "29px");
    render();
    script("typeof mq+':'+innerWidth", "undefined:800");
    {
        xl::Runtime separate;
        xl::Document isolated(separate);
        CHECK(isolated.matches_media("(width:640px)"));
        CHECK(isolated.matches_media("(prefers-color-scheme:light)"));
    }
    // One authored document may be rendered at two View sizes without reusing
    // another View's conditional generation or pending frame.
    xl_view_options narrow_options = options;
    narrow_options.width = 500;
    narrow_options.height = 600;
    xl::View second(runtime, &narrow_options);
    second.document(&document);
    uint32_t flags = 0;
    CHECK(xl_view_render(second.get(), &flags) == XL_OK);
    CHECK(document.matches_media("(width:500px)"));
    render();
    style("#x", "width", "29px");
    CHECK(document.matches_media("(width:800px)"));

    std::ifstream font_file(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(font_file), std::istreambuf_iterator<char>()};
    CHECK(!font.empty());
    CHECK(xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    page("<style>body{font:16px/20px test}#x::before{content:'Wide "
         "';color:red}@media(width<700px){#x::before{content:'Small '}#hidden{display:none}}</style><button "
         "id=x>Action</button><div id=hidden>Reading</div>");
    render();
    const auto ax_names = [&] {
        std::vector<std::string> result;
        size_t count = 0;
        CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
        for (size_t i = 0; i < count; ++i) {
            size_t bytes = 0;
            const auto status = xl_view_ax_name(view.get(), i, nullptr, 0, &bytes);
            CHECK(status == XL_OK || status == XL_BUFFER_TOO_SMALL);
            std::string name(bytes, '\0');
            CHECK(xl_view_ax_name(view.get(), i, name.data(), name.size(), &bytes) == XL_OK);
            result.push_back(name);
        }
        return result;
    };
    auto current = ax_names();
    CHECK(std::find(current.begin(), current.end(), "Wide Action") != current.end());
    CHECK(std::find(current.begin(), current.end(), "Reading") != current.end());
    xl::AXUpdate old, batch;
    while (view.take_ax_update(batch))
        old = std::move(batch);
    CHECK(old);
    resize(600, 600);
    render();
    current = ax_names();
    CHECK(std::find(current.begin(), current.end(), "Small Action") != current.end());
    CHECK(std::find(current.begin(), current.end(), "Reading") == current.end());
    CHECK(view.render() == 0);
    bool old_retained = false;
    for (size_t i = 0; i < old.info().node_count; ++i)
        old_retained |= old.name(i) == "Wide Action";
    CHECK(old_retained);
    resize(800, 600);

    if (argc == 2) {
#ifdef _WIN32
        if (std::string(argv[1]) == "--native") {
            xl::Document native_document(runtime);
            native_document.html("<style>#x{width:17px}@media(width<500px){#x{width:33px}}</style><div id=x></div>");
            xl_view_options native_options = options;
            native_options.kind = XL_VIEW_WINDOW;
            native_options.flags = XL_VIEW_HIDDEN | XL_VIEW_RESIZABLE;
            native_options.width = 800;
            native_options.height = 600;
            xl::View native(runtime, &native_options);
            native.document(&native_document);
            native.render();
            CHECK(native_document.matches_media("screen and (width>=500px)"));
            uint32_t kind;
            uintptr_t handle;
            CHECK(xl_view_native_handle(native.get(), &kind, &handle) == XL_OK && kind == XL_NATIVE_WIN32);
            CHECK(SetWindowPos(reinterpret_cast<HWND>(handle), nullptr, 0, 0, 320, 240,
                               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE));
            native.render();
            CHECK(native_document.matches_media("(width<500px)"));
            CHECK(native_document.evaluate("innerWidth<500 && matchMedia('(width<500px)').matches") == XL_OK &&
                  native_document.result() == "true");
            prefs = {sizeof(prefs), XL_MEDIA_COLOR_DARK, 0, 0, 0, 0};
            native_document.media_preferences(&prefs);
            native.render();
            CHECK(native_document.matches_media("(prefers-color-scheme:dark)"));
            std::cout << "native window resize and media preferences passed\n";
            return 0;
        }
#endif
        std::ifstream file(argv[1], std::ios::binary);
        std::string html{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        CHECK(!html.empty());
        document.html(html);
        style(".sidebar", "flex-direction", "column");
        resize(600, 600);
        style(".sidebar", "flex-direction", "row");
        style(".sidebar-foot", "display", "flex");
        script("getComputedStyle(document.documentElement).getPropertyValue('--bg').trim()", "#eef2f8");
        prefs = {sizeof(prefs), XL_MEDIA_COLOR_DARK, 0, 0, 0, 0};
        document.media_preferences(&prefs);
        script("getComputedStyle(document.documentElement).getPropertyValue('--bg').trim()", "#0a0e15");
    }
    std::cout << "conditional CSS, layer ordering, viewport/preferences, media events, pixels and original user page "
                 "passed\n";
}
