#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>
#include <vector>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
void render(xl::View& view) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags; xl_status status;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < until); std::this_thread::yield();
    }
    CHECK(status == XL_OK);
}
std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats = view.stats(); std::vector<unsigned char> data(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), data.data(), data.size(), size_t(stats.pixel_width) * 4) == XL_OK); return data;
}
xl_key_event key(const char* text) { return {sizeof(xl_key_event), XL_KEY_DOWN, 0, 0, 0, 0, text, ""}; }
}
int main() {
    xl::Runtime runtime; xl::Document document(runtime);
    std::ifstream stream(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 320; options.height = 200;
    xl::View view(runtime, &options), full(runtime, &options);
    view.document(&document); full.document(&document);
    const auto script = [&](const char* source, const char* expected, uint32_t budget = 100,
                            const std::source_location& location = std::source_location::current()) {
        const auto status = document.evaluate(source, budget);
        if (status != XL_OK) std::cerr << "script status at " << location.line() << ": " << xl_status_string(status) << ": " << document.result() << '\n';
        CHECK(status == XL_OK);
        if (document.result() != expected) std::cerr << "script at " << location.line() << ": " << document.result() << " expected " << expected << '\n';
        CHECK(document.result() == expected);
    };
    const auto consistent = [&] {
        render(view); render(full);
        CHECK(pixels(view) == pixels(full)); CHECK(view.render() == 0 && full.render() == 0);
    };
    document.html("<!doctype html><style>body{margin:0;font:20px/24px test}div{background-color:white}button{display:block;width:80px;height:30px;background-color:gray}"
        "button:focus{background-color:lime}section:focus-within{color:red}section:not(:focus-within){color:blue}"
        "button:focus-visible{border:3px solid red}button:not(:focus-visible){border:1px solid blue}"
        "button:focus+span{color:green}.test:has(>button:focus) span{background-color:yellow}"
        "button:where(:focus){padding-left:7px}button:is(:focus){padding-right:9px}"
        "button{padding-left:3px;padding-right:4px}"
        "button{margin-left:0}button:focus{margin-left:10px}button.later{margin-left:20px}"
        "</style><section id=outer><div id=group class=test><button id=a>A</button><span id=next>next</span><button id=b focus>B</button></div></section>"
        "<input id=editor value='text'><p id=pointer>pointer target</p><input id=bare readonly disabled>");
    consistent();
    script("document.querySelector(':focus')===null", "true");
    script("document.querySelectorAll(':focus-within').length", "0");
    script("let a=document.getElementById('a'),b=document.getElementById('b'),group=document.getElementById('group'),"
        "outer=document.getElementById('outer'),next=document.getElementById('next');a.focus();document.querySelector(':focus')===a", "true");
    script("document.querySelectorAll(':focus-within').map(x=>x.tagName).join(',')", "HTML,BODY,SECTION,DIV,BUTTON");
    script("document.querySelector(':focus-visible')===a", "true");
    script("getComputedStyle(a).getPropertyValue('background-color')", "lime");
    script("getComputedStyle(outer).getPropertyValue('color')", "red");
    script("getComputedStyle(next).getPropertyValue('color')", "green");
    script("getComputedStyle(next).getPropertyValue('background-color')", "yellow");
    script("getComputedStyle(a).getPropertyValue('padding-left')+':'+getComputedStyle(a).getPropertyValue('padding-right')", "3px:9px");
    script("document.querySelector(':FOCUS-WITHIN')===document.querySelector('html') && group.querySelector(':focus')===a", "true");
    script("a.className='later';getComputedStyle(a).getPropertyValue('margin-left')", "20px");
    script("document.querySelector('div:has(> :focus)')===group", "true");
    script("document.querySelector('button:nth-child(1 of :focus)')===a", "true");
    script("document.querySelector('button:not(:focus)')===b", "true");
    script("document.querySelector(':is(:not(:focus-within),:focus)')!==null", "true");
    script("a.hasAttribute('focus')+':'+b.hasAttribute('focus')", "false:true");
    script("let bare=document.getElementById('bare');bare.readOnly+':'+bare.disabled+':'+bare.getAttribute('readonly')", "true:true:");
    size_t required = 99;
    CHECK(xl_document_get_attribute(document.get(), document.query("#b"), "focus", nullptr, 0, &required) == XL_OK && required == 0);
    consistent();
    const auto before = pixels(view);
    CHECK(view.key(key("Tab"))); consistent();
    script("document.querySelector(':focus')===b", "true");
    CHECK(pixels(view) != before);
    script("b.onfocus=()=>a.focus();a.focus();b.focus();document.querySelector(':focus')===a", "true");
    consistent();
    script("b.onfocus=null;a.remove();document.querySelector(':focus')===null", "true");
    script("document.querySelectorAll(':focus-within').length", "0");
    consistent();
    script("group.appendChild(a);a.focus();document.querySelectorAll(':focus-within').map(x=>x.id).join(',')", ",,outer,group,a");
    script("a.disabled=true;document.querySelector(':focus')===null", "true");
    // activeElement validates disconnected/disabled focus; style matching must
    // agree even before the next rendered snapshot.
    script("document.activeElement===document.body", "true"); consistent();
    script("a.disabled=false;b.focus();group.setAttribute('inert','');document.querySelector(':focus')===null", "true");
    script("group.removeAttribute('inert');b.focus();b.setAttribute('style','display:none');document.activeElement===document.body", "true");
    consistent();
    script("b.setAttribute('style','');a.focus();'ok'", "ok"); render(view);
    // A real pointer-selection API updates modality; no synthetic focus attr.
    xl_node_bounds pointer{}; CHECK(xl_view_node_bounds(view.get(), document.query("#pointer"), &pointer) == XL_OK);
    view.select_at(pointer.x + 4, pointer.y + pointer.height / 2);
    view.clear_selection();
    script("a.focus();document.querySelector(':focus')===a && document.querySelector(':focus-visible')===null", "true");
    consistent();
    CHECK(!view.key(key("F2"))); // Key input changes focus-visible even without a button default.
    script("document.querySelector(':focus-visible')===a", "true"); consistent();
    script("let editor=document.getElementById('editor');editor.focus();'ok'", "ok"); render(view);
    xl_node_bounds editor{}; CHECK(xl_view_node_bounds(view.get(), document.query("#editor"), &editor) == XL_OK);
    view.select_at(editor.x + 8, editor.y + editor.height / 2);
    script("document.querySelector(':focus-visible')===editor", "true"); consistent();
    document.html("<!doctype html><style>button:focus{background-color:red}</style><button focus>new</button>");
    script("document.querySelector(':focus')===null && document.querySelectorAll(':focus-within').length===0", "true");
    consistent();
    // Repeated temporary AST adaptations must restore authored selectors and
    // release parser arenas; nested :is/:has queries must stay independent.
    script("let c=document.querySelector('button');for(let i=0;i<100;i++){c.focus();if(document.querySelector(':is(:focus)')!==c)throw Error('focus');"
        "c.blur();if(document.querySelector(':focus'))throw Error('blur');}'ok'", "ok", 1000);
    consistent();
    std::cout << "dynamic focus selectors, modality, cascade and retained rendering passed\n";
}
