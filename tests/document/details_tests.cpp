#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <thread>
#include <vector>
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
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
        if (status != XL_OK) std::cerr << "render: " << status << ' ' << document.result() << '\n';
        CHECK(status == XL_OK);
        return flags;
    };
    const auto script = [&](const char* code, const char* expected) {
        const auto status = document.evaluate(code);
        if (status != XL_OK || document.result() != expected) { std::cerr << code << "\nstatus " << status << " expected " << expected << " actual " << document.result() << '\n'; std::exit(1); }
    };
    const auto page = [&](const char* html) { document.html(std::string("<style>body{margin:0;font:16px/20px test}summary{height:20px}button{height:20px}</style>") + html); };
    const auto bounds = [&](const char* selector, bool visible = true) {
        xl_node_bounds value{};
        const auto status = xl_view_node_bounds(view.get(), document.query(selector), &value);
        CHECK(status == (visible ? XL_OK : XL_NOT_FOUND)); return value;
    };
    const auto ax = [&](xl_node_id id) -> std::optional<xl_ax_node> {
        size_t count; CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
        for (size_t i = 0; i < count; ++i) {
            xl_ax_node value{}; CHECK(xl_view_ax_get(view.get(), i, &value, sizeof(value)) == XL_OK);
            if (value.node == id) return value;
        }
        return {};
    };
    const auto key = [&](const char* name, uint32_t type = XL_KEY_DOWN) {
        const xl_key_event event{sizeof(event), type, 0, 0, 0, 0, name, ""}; return view.key(event);
    };
    const auto pixels = [&] {
        std::vector<unsigned char> value(240 * 180 * 4); CHECK(xl_view_copy_rgba(view.get(), value.data(), value.size(), 240 * 4) == XL_OK); return value;
    };
    const auto point = [&](float x, float y, uint32_t type) {
        const xl_pointer_event event{sizeof(event), type, 0, 0, x, y}; return view.pointer(event);
    };
    page("<details id=d>invisible raw text<summary id=s><span id=label>Title</span><button id=inside>Action</button></summary>"
         "<div id=content style='height:30px;background:lime'><button id=child>Hidden</button></div><summary id=later>Later</summary></details><button id=after>After</button>");
    script("let d=document.getElementById('d'),s=document.getElementById('s');let toggles=[];let bubbled=0;document.ontoggle=()=>bubbled++;"
           "d.ontoggle=e=>toggles.push([e.oldState,e.newState,e.bubbles,e.cancelable,e.isTrusted,e instanceof ToggleEvent].join(':'));d.open", "false");
    render(); CHECK(bounds("#d").height == 20); bounds("#content", false); bounds("#later", false);
    const auto summary_id = document.query("#s");
    CHECK(ax(summary_id)->role == XL_AX_BUTTON && (ax(summary_id)->states & XL_AX_COLLAPSED));
    CHECK(ax(summary_id)->actions == (XL_AX_ACTION_FOCUS | XL_AX_ACTION_ACTIVATE | XL_AX_ACTION_EXPAND | XL_AX_ACTION_COLLAPSE));
    CHECK(!ax(document.query("#child")));
    CHECK(xl_document_ax_action(document.get(), document.query("#child"), XL_AX_ACTION_ACTIVATE) == XL_UNSUPPORTED);
    CHECK(key("Tab")); script("document.activeElement.id", "s");
    CHECK(key("Tab")); script("document.activeElement.id", "inside");
    CHECK(key("Tab")); script("document.activeElement.id", "after");
    script("document.getElementById('inside').click();d.open", "false");
    script("s.onclick=e=>e.preventDefault();document.getElementById('label').click();d.open", "false");
    script("s.onclick=null;document.getElementById('label').click();d.open+':'+toggles.length", "true:0");
    int64_t delay; CHECK(xl_view_next_wakeup_ms(view.get(), &delay) == XL_OK && delay == 0);
    render(); bounds("#content"); bounds("#later"); CHECK(bounds("#d").height > 60);
    CHECK(ax(summary_id)->states & XL_AX_EXPANDED);
    script("toggles.join(',')+':'+bubbled", "closed:open:false:false:true:true:0");
    script("document.getElementById('later').click();d.open", "true");
    script("document.getElementById('child').focus();d.open=false;document.activeElement===document.body", "true");
    render(); CHECK(bounds("#d").height == 20);
    script("s.focus();'focused'", "focused"); CHECK(key("Enter")); render(); script("d.open", "true");
    CHECK(key(" ")); script("d.open", "true"); CHECK(key(" ", XL_KEY_UP)); render(); script("d.open", "false");
    script("s.onkeydown=e=>e.preventDefault();'ready'", "ready"); CHECK(key("Enter")); script("d.open", "false");
    script("s.onkeydown=null;d.open=true;d.open=false;d.open=true;toggles=[];bubbled=0;'queued'", "queued");
    render(); script("toggles.join(',')+':'+bubbled", "closed:open:false:false:true:true:0");
    const auto revision = document.stats().revision;
    CHECK(xl_document_ax_action(document.get(), summary_id, XL_AX_ACTION_EXPAND) == XL_OK);
    CHECK(document.stats().revision == revision);
    CHECK(xl_document_ax_action(document.get(), summary_id, XL_AX_ACTION_COLLAPSE) == XL_OK); render();
    const auto closed_revision = document.stats().revision;
    CHECK(xl_document_ax_action(document.get(), summary_id, XL_AX_ACTION_COLLAPSE) == XL_OK);
    CHECK(document.stats().revision == closed_revision); CHECK(view.render() == 0);

    // Marker direction changes actual pixels while its author text and AX name stay intact.
    page("<details id=d><summary id=s>Title</summary></details>"); render(); const auto closed_pixels = pixels();
    script("let d=document.getElementById('d');d.open=true;document.querySelector('summary').textContent", "Title");
    render(); const auto open_pixels = pixels(); CHECK(closed_pixels != open_pixels); CHECK(view.render() == 0);
    page("<details><summary style='list-style:none'>Title</summary></details>"); render(); CHECK(pixels() != closed_pixels);

    page("<details id=a name=group open><summary>A</summary></details><details id=b name=group open><summary>B</summary></details>"
         "<details id=c name=GROUP open><summary>C</summary></details><details id=free name='' open><summary>Free</summary></details>");
    script("let a=document.getElementById('a'),b=document.getElementById('b'),c=document.getElementById('c');[a.open,b.open,c.open].join(',')", "true,false,true");
    script("b.setAttribute('OPEN','false');[a.open,b.open].join(',')", "false,true");
    script("a.open=true;c.name='group';[a.open,b.open,c.open].join(',')", "true,false,false");
    script("let container=document.createElement('div');container.innerHTML='<details name=group open><summary>X</summary></details><details name=group open><summary>Y</summary></details>';"
           "container.children.map(x=>x.open).join(',')", "true,false");
    script("document.body.appendChild(container);container.children.map(x=>x.open).join(',')+':'+a.open", "false,false:true");
    script("let detached=document.createElement('details');detached.name='group';detached.open=true;document.body.appendChild(detached);detached.open+':'+a.open", "false:true");
    script("b.name='';b.open=true;a.open+':'+b.open", "true:true");
    script("a.setAttribute('open','anything');a.open+':'+a.getAttribute('open')", "true:anything");
    // Attribute value changes without presence changes do not queue toggle.
    render(); script("let notices=0;a.ontoggle=()=>notices++;a.setAttribute('open','other');'changed'", "changed"); render(); script("notices", "0");
    script("let clone=a.cloneNode(true);clone.open+':'+clone.name", "true:group");
    script("document.body.appendChild(clone);clone.open+':'+a.open", "false:true");
    script("let f=document.createDocumentFragment(),x=document.createElement('details'),y=document.createElement('details');x.name=y.name='fragment';x.open=y.open=true;f.appendChild(x);f.appendChild(y);x.open+':'+y.open", "true:false");

    // Parsing notifications remain queued until the host drives a task turn.
    page("<details id=d open><summary>S</summary></details>");
    script("let d=document.getElementById('d'),events=[];d.ontoggle=e=>{events.push(e.oldState+':'+e.newState);if(events.length===1)d.open=false};events.length", "0");
    render(); script("events.join(',')", "closed:open"); render(); script("events.join(',')", "closed:open,open:closed");
    CHECK(view.render() == 0);
    script("d.open=true;d.remove();d.ontoggle=e=>events.push('detached:'+e.newState);'removed'", "removed");
    render(); script("events.at(-1)", "detached:open");
    script("d.open=false;'pending'", "pending");
    document.html("<div>New document</div>"); render(); // Old pending tasks cannot reach the new document.
    page("<details id=d><summary>S</summary></details>");
    script("let d=document.getElementById('d'),events=[];d.ontoggle=e=>{events.push(e.oldState+':'+e.newState);Promise.resolve().then(()=>events.push('job'))};"
           "setTimeout(()=>events.push('timer'),0);d.open=true;d.open=false;events.length", "0");
    render(); script("events.join(',')", "timer,closed:closed,job"); CHECK(view.render() == 0);
    script("d.ontoggle=()=>{throw new Error('toggle diagnostic')};d.open=true;'queued'", "queued");
    uint32_t error_flags; CHECK(xl_view_render(view.get(), &error_flags) == XL_SCRIPT_ERROR);
    CHECK(document.result().find("toggle diagnostic") != std::string::npos);
    script("d.ontoggle=null;'recovered'", "recovered"); render();
    // Host/native pointer uses geometry, independent of ARIA visibility. Drag,
    // cancel, navigation and a nested interactive element cannot toggle it.
    page("<details id=d><summary id=s aria-hidden=true>Title<button id=button style='display:inline-block;width:50px;height:20px'>B</button></summary><div id=content style='height:30px'>Extra</div></details>");
    script("let d=document.getElementById('d'),clicks=[];document.getElementById('s').onclick=e=>clicks.push(e.isTrusted);d.open", "false");
    render(); const auto pointer_summary = bounds("#s");
    CHECK(point(pointer_summary.x + 2, pointer_summary.y + 2, XL_POINTER_DOWN)); render();
    CHECK(point(pointer_summary.x + 2, pointer_summary.y + 2, XL_POINTER_UP)); render(); script("d.open+':'+clicks.join(',')", "true:true");
    const auto button_box = bounds("#button");
    CHECK(point(button_box.x + 2, button_box.y + 2, XL_POINTER_DOWN)); render();
    CHECK(point(button_box.x + 2, button_box.y + 2, XL_POINTER_UP)); render(); script("d.open", "true");
    CHECK(point(2, 2, XL_POINTER_DOWN)); render(); CHECK(point(12, 2, XL_POINTER_MOVE)); render(); CHECK(point(2, 2, XL_POINTER_UP)); render(); script("d.open", "true");
    CHECK(point(2, 2, XL_POINTER_DOWN)); render(); CHECK(point(2, 2, XL_POINTER_CANCEL)); CHECK(point(2, 2, XL_POINTER_UP)); render(); script("d.open", "true");
    CHECK(point(2, 2, XL_POINTER_DOWN)); render();
    page("<details id=d><summary>New</summary></details>"); render(); point(2, 2, XL_POINTER_UP); render(); script("document.getElementById('d').open", "false");

    page("<details id=d><div id=content style='height:30px;background:lime'>Content</div></details>");
    render(); bounds("#content", false); const auto fallback_id = document.query("#d");
    CHECK(ax(fallback_id)->role == XL_AX_BUTTON && (ax(fallback_id)->states & XL_AX_COLLAPSED));
    script("document.querySelectorAll('summary').length+':'+document.getElementById('d').children.length", "0:1");
    script("document.getElementById('d').click();document.getElementById('d').open", "false");
    CHECK(xl_document_ax_action(document.get(), fallback_id, XL_AX_ACTION_EXPAND) == XL_OK); render(); bounds("#content");
    CHECK(ax(fallback_id)->bounds.height < bounds("#d").height);
    CHECK(key("Tab")); script("document.activeElement.id", "d"); CHECK(key("Enter")); render(); bounds("#content", false);
    script("document.getElementById('d').innerHTML='<summary id=s>Added</summary><div id=content>Rest</div>';document.querySelectorAll('summary').length", "1");
    render(); CHECK(ax(document.query("#d"))->role == XL_AX_GENERIC); CHECK(ax(document.query("#s"))->role == XL_AX_BUTTON);

    for (const auto* display : {"flex", "grid"}) {
        document.html(std::string("<style>body{margin:0;font:16px test}details{display:") + display + ";width:200px}summary{height:20px}#content{height:30px}</style>"
            "<details id=d>Hidden raw<summary id=s>S</summary><div id=content>Rest</div></details>");
        render(); bounds("#content", false); CHECK(bounds("#d").height == 20);
        script("document.getElementById('d').open=true;'open'", "open"); render(); bounds("#content");
        document.html(std::string("<style>body{margin:0;font:16px test}details{display:") + display + ";width:200px}#content{height:30px}</style>"
            "<details id=d><div id=content>Rest</div></details>");
        render(); bounds("#content", false); CHECK(bounds("#d").height > 0);
        CHECK(xl_document_ax_action(document.get(), document.query("#d"), XL_AX_ACTION_EXPAND) == XL_OK); render(); bounds("#content");
    }
    page("<details id=outer><summary>Outer</summary><details id=inner open><summary>Inner</summary><button id=nested>N</button></details></details>");
    render(); bounds("#inner", false); bounds("#nested", false);
    script("document.getElementById('outer').open=true;'open'", "open"); render(); bounds("#inner"); bounds("#nested");
    // Cross-runtime isolation is independent of group names.
    { xl::Runtime other_runtime; xl::Document other(other_runtime); other.html("<details id=other name=group open></details>");
      script("document.getElementById('outer').name='group';document.getElementById('outer').open=true;'open'", "open");
      CHECK(other.evaluate("document.getElementById('other').open") == XL_OK && other.result() == "true"); }
    if (argc == 2) {
        std::ifstream source(argv[1], std::ios::binary); const std::string html{std::istreambuf_iterator<char>(source), std::istreambuf_iterator<char>()}; CHECK(!html.empty());
        document.html(html); render();
        script("let first=document.querySelector('details'),closed=document.querySelectorAll('details')[1];first.open+':'+closed.open", "true:false");
        const auto paragraph = document.query("details:nth-of-type(2) p"); xl_node_bounds value{};
        CHECK(paragraph && xl_view_node_bounds(view.get(), paragraph, &value) == XL_NOT_FOUND);
        script("closed.querySelector('summary').click();closed.open", "true"); render(); CHECK(xl_view_node_bounds(view.get(), paragraph, &value) == XL_OK);
        script("first.querySelector('summary').click();first.open", "false"); render(); CHECK(view.render() == 0);
    }
}
