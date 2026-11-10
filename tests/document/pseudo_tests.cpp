#include <XLWebEngineUI/xl.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
struct Entry { xl_ax_node node; std::string name; };
int main(int argc, char** argv) {
    xl::Runtime runtime; xl::Document document(runtime);
    std::ifstream font_file(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(font_file), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 240; options.height = 180;
    xl::View view(runtime, &options); view.document(&document); view.ax_updates(true);
    const auto render = [&] {
        uint32_t flags; xl_status status;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) { CHECK(std::chrono::steady_clock::now() < end); std::this_thread::yield(); }
        if (status != XL_OK) std::cerr << "render status " << status << '\n'; CHECK(status == XL_OK); return flags;
    };
    const auto script = [&](const char* code, const char* expected) {
        const auto status = document.evaluate(code);
        if (status != XL_OK || document.result() != expected) { std::cerr << code << "\nstatus " << status << " expected " << expected << " actual " << document.result() << '\n'; std::exit(1); }
    };
    const auto page = [&](std::string html) { document.html("<style>body{margin:0;font:16px/20px test}</style>" + html); };
    const auto entries = [&] {
        size_t count = 0; CHECK(xl_view_ax_count(view.get(), &count) == XL_OK); std::vector<Entry> result;
        for (size_t i = 0; i < count; ++i) {
            xl_ax_node node{}; CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
            size_t size = 0; const auto status = xl_view_ax_name(view.get(), i, nullptr, 0, &size); CHECK(status == XL_OK || status == XL_BUFFER_TOO_SMALL);
            std::string name(size, '\0'); CHECK(xl_view_ax_name(view.get(), i, name.data(), name.size(), &size) == XL_OK); result.push_back({node, name});
        }
        return result;
    };
    const auto named = [&](const char* name, uint32_t role = XL_AX_TEXT) {
        for (const auto& entry : entries()) if (entry.name == name && entry.node.role == role) return entry.node;
        std::cerr << "AX name missing: " << name << '\n'; for (const auto& entry : entries()) std::cerr << entry.node.role << ' ' << entry.name << '\n'; std::exit(1);
    };
    const auto bounds = [&](const char* selector) { xl_node_bounds value{}; CHECK(xl_view_node_bounds(view.get(), document.query(selector), &value) == XL_OK); return value; };
    const auto pixels = [&] { std::vector<unsigned char> value(240 * 180 * 4); CHECK(xl_view_copy_rgba(view.get(), value.data(), value.size(), 240 * 4) == XL_OK); return value; };
    const auto point = [&](float x, float y, uint32_t type) { const xl_pointer_event event{sizeof(event), type, 0, 0, x, y}; return view.pointer(event); };
    const auto pixel = [&](int x, int y) { const auto image = pixels(); const auto offset = size_t(y * 240 + x) * 4; return uint32_t(image[offset]) << 24 | uint32_t(image[offset + 1]) << 16 | uint32_t(image[offset + 2]) << 8 | image[offset + 3]; };

    // Empty generated block paint does not eagerly prepare/decompress ICU.
    page("<style>div::before{content:'';display:block;width:20px;height:10px;background:red}</style><div></div>"); render(); CHECK(pixel(2,2) == 0xff0000ff);
    xl_resource_stats lazy{}; CHECK(xl_runtime_resource_stats(runtime.get(), &lazy, sizeof(lazy)) == XL_OK);
    CHECK(lazy.decompressions == 0);

    page("<style>#x::before{content:'Before ';color:red}#x::after{content:' After';color:blue}</style><div id=x>Body</div>"); render();
    const auto before = named("Before "), body = named("Body"), after = named(" After");
    CHECK(before.bounds.x < body.bounds.x && body.bounds.x < after.bounds.x);
    script("document.getElementById('x').textContent+':'+document.getElementById('x').childNodes.length+':'+document.querySelectorAll('span').length+':'+document.querySelectorAll('#x::before').length", "Body:1:0:0");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "color") == "red");
    script("getComputedStyle(document.getElementById('x'),'::after').getPropertyValue('content')", "\" After\"");
    const xl_text_position generated{sizeof(generated), 0, before.node, XL_TEXT_DOWNSTREAM, 0};
    xl_text_selection selected{sizeof(selected), 0, generated, generated};
    CHECK(xl_view_set_selection(view.get(), &selected, sizeof(selected)) == XL_NOT_FOUND ||
          xl_view_set_selection(view.get(), &selected, sizeof(selected)) == XL_UNSUPPORTED);
    CHECK(xl_document_set_text(document.get(), before.node, "bad", 3) == XL_INVALID_ARGUMENT);
    const auto hit = view.hit_text(before.bounds.x + 2, before.bounds.y + 2); CHECK(hit.node == body.node);
    CHECK(view.render() == 0);

    page("<style>#x{--word:'Inherited';color:green}#x::before{content:var(--word);--word:'Local';color:inherit}#x::after{content:var(--word)}</style><button id=x>Button</button>"); render();
    named("LocalButtonInherited", XL_AX_BUTTON); named("Local"); named("Inherited");
    CHECK(document.pseudo_style(document.query("#x"), "::BEFORE", "color") == "green");
    script("getComputedStyle(document.getElementById('x'),'::before').getPropertyValue('--word').trim()", "'Local'");
    script("document.getElementById('x').style.setProperty('--word','\"Changed\"');'changed'", "changed"); render(); named("LocalButtonChanged", XL_AX_BUTTON);
    page("<style>#x::before{content:'First'}div::before{content:'Important'!important}#x::after{content:'A';content:bogus}#x::after{content:var(--absent)}</style><div id=x></div>"); render(); named("Important");
    script("getComputedStyle(document.getElementById('x'),'::after').getPropertyValue('content')", "none");
    page("<style>div::before{content:'old';content:bogus}#x:before{content:'new'}div::after{content:'ignored';display:none}</style><div id=x></div>"); render(); named("new");

    page("<style>#x::before{content:'[' attr(data-word,'fallback') ']\\1f642 ' / attr(data-alt,'spoken')}#x::after{content:attr(data-missing)}</style><button id=x data-word=Value data-alt=Read>Body</button>"); render();
    named("ReadBody", XL_AX_BUTTON); named("Read");
    CHECK(document.pseudo_style(document.query("#x"), "::before", "content") == "\"[Value]\xf0\x9f\x99\x82\" / \"Read\"");
    xl::AXUpdate old_update, batch;
    while (view.take_ax_update(batch)) old_update = std::move(batch);
    CHECK(old_update);
    script("document.getElementById('x').setAttribute('data-word','A\";color:red');document.getElementById('x').setAttribute('data-alt','Different');'changed'", "changed"); render(); named("DifferentBody", XL_AX_BUTTON);
    bool retained = false;
    for (size_t i = 0; i < old_update.info().node_count; ++i) if (old_update.name(i) == "Read") retained = true;
    CHECK(retained);
    page("<style>#x::before{content:'';display:block;width:30px;height:12px;background:lime}#x::after{content:none;width:99px;height:99px}</style><div id=x></div>"); render(); CHECK(bounds("#x").height == 12); CHECK(pixel(2,2) == 0x00ff00ff);
    page("<style>#x{display:flex}#y::before{content:url('missing')}</style><div id=x><div id=y>Body</div></div>"); render(); CHECK(document.stats().unsupported_layout_nodes > 0);

    for (const auto* display : {"block", "flex", "grid", "inline-block"}) {
        page(std::string("<style>#x{display:") + display + ";width:120px}#x::before{content:'';display:block;width:20px;height:20px;background:red}#x::after{content:'';display:block;width:20px;height:20px;background:blue}#real{display:block;width:20px;height:20px;background:lime}</style><div id=x><span id=real></span></div>"); render();
        const auto actual = bounds("#real"); CHECK(actual.width == 20 && actual.height == 20);
        if (std::string(display) == "flex") { CHECK(actual.x == 20 && actual.y == 0); CHECK(pixel(2,2) == 0xff0000ff); CHECK(pixel(22,2) == 0x00ff00ff); CHECK(pixel(42,2) == 0x0000ffff); }
        else { CHECK(std::abs(actual.y - bounds("#x").y - 20) < .01f); CHECK(pixel(2,2) == 0xff0000ff); CHECK(pixel(2,22) == 0x00ff00ff); CHECK(pixel(2,42) == 0x0000ffff); }
        script("document.getElementById('x').children.length+':'+document.querySelector('#real:first-child')?.id+':'+document.querySelectorAll('span').length", "1:real:1");
    }
    page("<style>summary{display:flex;width:180px;height:24px;justify-content:space-between}summary::after{content:'+'}details[open] summary::after{content:'-'}</style><details id=d><summary id=s>Title</summary><p>Content</p></details>"); render();
    const auto plus = named("+"); CHECK(plus.bounds.x > 150);
    script("let seen=[];document.getElementById('s').onclick=e=>seen.push(e.target.id);'ready'", "ready");
    CHECK(point(plus.bounds.x + 2, plus.bounds.y + 2, XL_POINTER_DOWN)); render(); CHECK(point(plus.bounds.x + 2, plus.bounds.y + 2, XL_POINTER_UP)); render();
    script("document.getElementById('d').open+':'+seen.join(',')", "true:s"); named("-");
    const auto minus = named("-"); CHECK(minus.node == plus.node);
    script("document.getElementById('d').open=false;'closed'", "closed"); render(); named("+");
    page("<style>input:checked+label::after{content:'checked'}label::after{content:'off'}label:focus::before{content:'focus'}</style><input id=c type=checkbox><label id=x tabindex=0 for=c>Label</label>"); render(); named("off");
    script("document.getElementById('c').checked=true;document.getElementById('x').focus();'changed'", "changed"); render(); named("checked"); named("focus");

    // Private render boxes are stable across recomputation and never acquired
    // by selectors, cloneNode or innerHTML. Detached authored trees still work.
    page("<style>.item::before{content:attr(data-label,'default')}</style><div id=x class=item data-label=One>Body</div>"); render(); const auto stable = named("One").node;
    for (unsigned i = 0; i < 20; ++i) { document.attribute(document.query("#x"), "data-label", i % 2 ? "One" : "Two"); render(); CHECK(named(i % 2 ? "One" : "Two").node == stable); CHECK(view.render() == 0); }
    script("let x=document.getElementById('x');x.remove();x.style.setProperty('--new','1');document.body.appendChild(x);x.textContent+':'+x.children.length", "Body:0"); render(); CHECK(named("One").node == stable);
    script("let clone=x.cloneNode(true);clone.id='copy';document.body.appendChild(clone);clone.childNodes.length", "1"); render(); CHECK(named("One").node != 0);
    script("x.innerHTML='<span>New</span>';x.childNodes.length", "1"); render(); named("New");

    page("<style>#port{width:80px;height:20px;overflow:hidden}#x::before{content:'';display:block;width:120px;height:40px;background:red}</style><div id=port><div id=x></div></div>"); render();
    CHECK(pixel(2,2) == 0xff0000ff); CHECK(pixel(2,22) == 0xffffffff); CHECK(pixel(82,2) == 0xffffffff);
    view.scroll_to(document.query("#port"), 0, 20); render(); CHECK(pixel(2,2) == 0xff0000ff); CHECK(view.render() == 0);
    // Disabled control descendants cannot focus a wrapping summary or dispatch
    // clicks; enabling the same author control restores the pointer path.
    page("<details id=d><summary id=s>Title<button id=b disabled><span id=inner>Press</span></button></summary></details><button id=focused>Focus</button>"); render();
    script("let count=0;document.getElementById('s').onclick=()=>count++;document.getElementById('focused').focus();'focused'", "focused"); render();
    const auto inner = bounds("#inner"); CHECK(point(inner.x + 2, inner.y + 2, XL_POINTER_DOWN)); render(); CHECK(point(inner.x + 2, inner.y + 2, XL_POINTER_UP)); render();
    script("document.activeElement.id+':'+count+':'+document.getElementById('d').open", "focused:0:false");
    script("document.getElementById('b').disabled=false;'enabled'", "enabled"); render();
    CHECK(point(inner.x + 2, inner.y + 2, XL_POINTER_DOWN)); render(); CHECK(point(inner.x + 2, inner.y + 2, XL_POINTER_UP)); render();
    script("document.activeElement.id+':'+count+':'+document.getElementById('d').open", "b:1:false");
    page("<fieldset disabled><legend><button id=allowed><span id=ok>Allowed</span></button></legend><button id=blocked><span id=no>Blocked</span></button></fieldset>"); render();
    script("let clicks=[];document.getElementById('allowed').onclick=()=>clicks.push('allowed');document.getElementById('blocked').onclick=()=>clicks.push('blocked');'ready'", "ready");
    for (const auto* selector : {"#ok", "#no"}) {
        const auto box = bounds(selector); CHECK(point(box.x + 2, box.y + 2, XL_POINTER_DOWN)); render(); CHECK(point(box.x + 2, box.y + 2, XL_POINTER_UP)); render();
    }
    script("clicks.join(',')+':'+document.activeElement.id", "allowed:allowed");

    const xl::Content first_css(".item::after{content:'Mounted'}", "text/css");
    const xl::Content next_css(".item::after{content:'Replaced'}", "text/css");
    runtime.mount("xl-webengine-ui://app/theme.css", first_css);
    page("<link rel=stylesheet href='theme.css'><div class=item>Body</div>"); render(); named("Mounted");
    { xl::Runtime separate_runtime; xl::Document separate(separate_runtime); separate.html("<link rel=stylesheet href='theme.css'><div class=item>Body</div>");
      CHECK(separate.pseudo_style(separate.query(".item"), "::after", "content") == "none"); }
    runtime.mount("xl-webengine-ui://app/theme.css", next_css); render(); named("Replaced");
    runtime.unmount("xl-webengine-ui://app/theme.css"); render();
    for (const auto& entry : entries()) CHECK(entry.name != "Replaced");
    if (argc == 2) {
        std::ifstream source(argv[1], std::ios::binary); const std::string html{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()}; CHECK(!html.empty()); document.html(html); render();
        script("getComputedStyle(document.querySelectorAll('details')[1].querySelector('summary'),'::after').getPropertyValue('content')", "\"＋\"");
        script("document.querySelectorAll('details')[1].querySelector('summary').click();getComputedStyle(document.querySelectorAll('details')[1].querySelector('summary'),'::after').getPropertyValue('content')", "\"－\""); render(); named("－");
    }
}
