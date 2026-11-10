#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#include "win_input.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
void render(xl::View& view) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags;
    xl_status status;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < until); std::this_thread::yield();
    }
    CHECK(status == XL_OK);
}
std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats = view.stats();
    std::vector<unsigned char> result(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), result.data(), result.size(), size_t(stats.pixel_width) * 4) == XL_OK); return result;
}
xl_key_event event(const char* key, uint32_t type = XL_KEY_DOWN, uint32_t modifiers = 0, const char* code = "") {
    return {sizeof(xl_key_event), type, modifiers, 0, 0, 0, key, code};
}
}
int main(int argc, char**) {
#ifdef _WIN32
    const bool native = argc > 1;
    if (native && !test_input_desktop_available()) { std::cout << "native keyboard unavailable: noninteractive input desktop\n"; return std::getenv("XL_REQUIRE_NATIVE_INPUT") ? 1 : 77; }
#else
    (void)argc;
#endif
    xl::Runtime runtime;
    xl::Document document(runtime);
    std::ifstream stream(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options;
    CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 320; options.height = 180;
    xl::View view(runtime, &options); view.document(&document);
    const std::string base = "<!doctype html><style>body{margin:0;font:20px/24px test}button,input,a,[tabindex]{display:block;width:100px;height:24px;margin:0}</style>";
    const auto script = [&](const char* code, const char* expected) {
        CHECK(document.evaluate(code) == XL_OK);
        if (document.result() != expected) std::cerr << "script result: " << document.result() << " expected: " << expected << '\n';
        CHECK(document.result() == expected);
    };
    const auto focused = [&] {
        CHECK(document.evaluate("document.activeElement?.id ?? ''") == XL_OK);
        return document.result();
    };
    const auto page = [&](const char* html) { document.html(base + html); render(view); };
    page("<button id=a>A</button><button id=b tabindex=2>B</button><button id=c tabindex=1>C</button>"
         "<button id=disabled disabled>D</button><div hidden><button>X</button></div>"
         "<div inert><button>Y</button></div><div style='display:none'><button>Z</button></div>"
         "<a id=link href='local'>Link</a><div id=negative tabindex=-1>N</div><div role=button>Role</div>");
    CHECK(view.key(event("Tab"))); CHECK(focused() == "c"); render(view);
    CHECK(view.key(event("Tab"))); CHECK(focused() == "b"); render(view);
    CHECK(view.key(event("Tab"))); CHECK(focused() == "a"); render(view);
    CHECK(view.key(event("Tab"))); CHECK(focused() == "link"); render(view);
    CHECK(view.key(event("Tab"))); CHECK(focused() == "c"); render(view);
    CHECK(view.key(event("Tab", XL_KEY_DOWN, XL_MOD_SHIFT))); CHECK(focused() == "link"); render(view);
    script("document.getElementById('disabled').focus();document.activeElement.id", "link");
    script("document.getElementById('negative').focus();document.activeElement.id", "negative");
    CHECK(view.key(event("Tab", XL_KEY_DOWN, XL_MOD_SHIFT))); CHECK(focused() == "link"); render(view);
    script("document.getElementById('link').blur();document.activeElement===document.body", "true");

    page("<fieldset disabled><legend><button id=legend>L</button></legend><button id=no>No</button></fieldset>"
         "<button id=aria aria-disabled=true>Aria</button><input id=hidden type=hidden>");
    CHECK(view.key(event("Tab"))); CHECK(focused() == "legend");
    CHECK(view.key(event("Tab"))); CHECK(focused() == "aria"); render(view);
    CHECK(xl_document_ax_action(document.get(), document.query("#aria"), XL_AX_ACTION_FOCUS) == XL_OK);

    page("<button id=a>A</button><button id=b>B</button><input id=check type=checkbox>");
    script(R"JS(globalThis.log=[];
      document.addEventListener('focus',e=>log.push('capture:'+e.target.id+':'+e.eventPhase),true);
      document.getElementById('a').addEventListener('focus',e=>log.push('focus:'+e.relatedTarget?.id));
      document.addEventListener('focusin',e=>log.push('in:'+e.target.id));
      document.getElementById('a').focus();JSON.stringify(log))JS", "[\"capture:a:1\",\"focus:undefined\",\"in:a\"]");
    script(R"JS(log=[];document.getElementById('a').onblur=e=>log.push('blur:'+e.relatedTarget.id);
      document.getElementById('a').onfocusout=e=>log.push('out');
      document.getElementById('b').focus();JSON.stringify(log))JS", "[\"blur:b\",\"out\",\"capture:b:1\",\"in:b\"]");
    script("document.getElementById('a').onblur=()=>document.getElementById('check').focus();document.getElementById('a').focus();'ready'", "ready");
    CHECK(view.key(event("Tab"))); CHECK(focused() == "check"); render(view);
    script("document.getElementById('a').onblur=null;document.getElementById('a').focus();globalThis.keys=[];'ready'", "ready");
    script(R"JS(document.onkeydown=e=>keys.push('bubble:'+e.key+':'+e.code+':'+e.shiftKey+':'+e.getModifierState('CapsLock'));
      document.getElementById('a').addEventListener('keydown',e=>{if(e.key==='Tab')e.preventDefault();});'ready')JS", "ready");
    auto tab = event("Tab", XL_KEY_DOWN, XL_MOD_SHIFT | XL_MOD_CAPS_LOCK, "Tab");
    CHECK(view.key(tab)); CHECK(focused() == "a");
    script("JSON.stringify(keys)", "[\"bubble:Tab:Tab:true:true\"]");
    auto unicode = event("\xe4\xb8\xad", XL_KEY_DOWN, XL_MOD_CONTROL, "KeyA"); unicode.location = 2;
    CHECK(!view.key(unicode));
    script("keys[keys.length-1]", "bubble:中:KeyA:false:false");

    page("<button id=a>A</button><button id=b>B</button><input id=check type=checkbox>");
    script(R"JS(globalThis.order=[];globalThis.saved=null;const a=document.getElementById('a');a.focus();
      document.addEventListener('keydown',e=>order.push('capture'),true);
      const removed=()=>order.push('removed');
      a.addEventListener('keydown',e=>{order.push('target');a.removeEventListener('keydown',removed);saved=e;},{once:true});
      a.addEventListener('keydown',removed);
      a.addEventListener('keydown',e=>e.preventDefault(),{passive:true});
      document.addEventListener('keydown',e=>order.push('bubble'));
      'ready')JS", "ready");
    CHECK(!view.key(event("x", XL_KEY_DOWN, 0, "KeyX")));
    script("JSON.stringify(order)+':'+saved.defaultPrevented+':'+saved.eventPhase+':'+saved.currentTarget", "[\"capture\",\"target\",\"bubble\"]:false:0:null");
    CHECK(!view.key(event("x"))); script("JSON.stringify(order)", "[\"capture\",\"target\",\"bubble\",\"capture\",\"bubble\"]");
    script("document.getElementById('a').addEventListener('keydown',e=>{order=[];e.stopImmediatePropagation();});document.getElementById('a').addEventListener('keydown',()=>order.push('bad'));'ready'", "ready");
    CHECK(!view.key(event("x"))); script("order.length", "0");

    page("<button id=a>A</button><input id=check type=checkbox>");
    script("globalThis.clicks=0;globalThis.changes=[];document.getElementById('a').onclick=e=>{clicks++;changes.push(e.isTrusted);Promise.resolve().then(()=>changes.push('job'));};document.getElementById('a').focus();'ready'", "ready");
    CHECK(view.key(event("Enter"))); script("clicks+':'+JSON.stringify(changes)", "1:[true,\"job\"]");
    CHECK(view.key(event(" "))); script("clicks", "1");
    auto repeated = event(" "); repeated.repeat = 1; CHECK(view.key(repeated));
    CHECK(view.key(event(" ", XL_KEY_UP))); script("clicks", "2");
    CHECK(!view.key(event(" ", XL_KEY_UP))); script("clicks", "2");
    CHECK(view.key(event(" ")));
    script("document.getElementById('check').focus();document.getElementById('a').focus();'roundtrip'", "roundtrip");
    CHECK(!view.key(event(" ", XL_KEY_UP))); script("clicks", "2");
    script("document.getElementById('a').click();changes[changes.length-1]", "false");
    script("document.getElementById('check').focus();globalThis.states=[];document.getElementById('check').onclick=e=>states.push(e.target.checked);document.getElementById('check').oninput=()=>states.push('input');document.getElementById('check').onchange=()=>states.push('change');'ready'", "ready");
    CHECK(view.key(event(" "))); CHECK(view.key(event(" ", XL_KEY_UP))); render(view);
    script("JSON.stringify(states)+':'+document.getElementById('check').checked", "[true,\"input\",\"change\"]:true");
    size_t count;
    CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    bool checked = false;
    for (size_t i = 0; i < count; ++i) {
        xl_ax_node node; CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
        if (node.node == document.query("#check")) checked = (node.states & XL_AX_CHECKED) && (node.states & XL_AX_FOCUSED);
    }
    CHECK(checked);
    script("document.getElementById('check').onclick=e=>e.preventDefault();'ready'", "ready");
    CHECK(view.key(event(" "))); CHECK(view.key(event(" ", XL_KEY_UP)));
    script("document.getElementById('check').checked", "true");
    script("document.getElementById('check').onkeydown=e=>e.preventDefault();'ready'", "ready");
    CHECK(view.key(event(" "))); CHECK(!view.key(event(" ", XL_KEY_UP)));
    script("document.getElementById('check').checked", "true");

    page("<div id=port tabindex=0 style='height:50px;overflow:auto'><div style='height:200px'></div><button id=deep>Deep</button></div><div style='height:400px'></div>");
    CHECK(view.key(event("Tab"))); render(view); CHECK(focused() == "port");
    CHECK(view.key(event("ArrowDown"))); render(view); CHECK(view.scroll(document.query("#port")).y == 40);
    CHECK(view.key(event("End"))); render(view);
    CHECK(view.scroll(document.query("#port")).y == view.scroll(document.query("#port")).max_y);
    CHECK(view.key(event("Home"))); render(view); CHECK(view.scroll(document.query("#port")).y == 0);
    const auto before = view.stats().rasterized_pixels;
    CHECK(view.key(event("Tab"))); render(view);
    CHECK(focused() == "deep" && view.scroll(document.query("#port")).y > 0);
    CHECK(view.stats().rasterized_pixels - before < 320u * 180u);
    { xl::View full(runtime, &options); full.document(&document); render(full);
      const auto info = view.scroll(document.query("#port")); full.scroll_to(document.query("#port"), info.x, info.y); render(full);
      CHECK(pixels(view) == pixels(full)); }
    CHECK(view.render() == 0);
    auto image_with_focus = pixels(view);
    size_t ax_count;
    CHECK(xl_view_ax_count(view.get(), &ax_count) == XL_OK);
    xl_node_id deep_text = 0;
    for (size_t i = 0; i < ax_count; ++i) {
        xl_ax_node node; CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
        if (node.role == XL_AX_TEXT && node.parent == document.query("#deep")) deep_text = node.node;
    }
    CHECK(deep_text);
    const xl_text_position begin{sizeof(xl_text_position), 0, deep_text, XL_TEXT_DOWNSTREAM, 0};
    auto end = begin; end.offset_utf16 = 4;
    view.selection(begin, end); render(view); view.clear_selection(); render(view);
    CHECK(pixels(view) == image_with_focus);
    script("document.getElementById('deep').remove();document.activeElement===document.body", "true"); render(view);

    document.html(base + "<div style='height:350px'></div><button id=early>Early</button>");
    CHECK(view.key(event("Tab"))); CHECK(focused() == "early"); render(view);
    CHECK(view.scroll().y > 0);
    page("<div style='height:350px'></div><button id=ax>AX</button>");
    CHECK(xl_document_ax_action(document.get(), document.query("#ax"), XL_AX_ACTION_FOCUS) == XL_OK);
    render(view); CHECK(view.scroll().y > 0);
    page("<div style='height:350px'></div><button id=js>JS</button>");
    script("document.getElementById('js').focus();'focused'", "focused"); render(view);
    CHECK(view.scroll().y > 0);

    page("<button id=a>A</button><input id=text type=text><div style='height:500px'></div>");
    script("document.getElementById('text').focus();'ready'", "ready");
    CHECK(view.key(event("ArrowDown"))); render(view); CHECK(view.scroll().y == 0);
    script("document.getElementById('a').focus();document.getElementById('a').onkeydown=()=>{throw new Error('key diagnostic');};'ready'", "ready");
    uint32_t handled = 99; auto key = event("x");
    CHECK(xl_view_key(view.get(), &key, sizeof(key), &handled) == XL_SCRIPT_ERROR && handled == 99);
    CHECK(document.result().find("key diagnostic") != std::string::npos);
    script("document.getElementById('a').onkeydown=()=>{while(true){}};'ready'", "ready");
    CHECK(xl_view_key(view.get(), &key, sizeof(key), &handled) == XL_TIMEOUT && handled == 99);
    script("document.getElementById('a').onkeydown=null;'ready'", "ready");
    key.modifiers = 0x8000; CHECK(xl_view_key(view.get(), &key, sizeof(key), &handled) == XL_INVALID_ARGUMENT);
    key = event("\xc0\xaf"); CHECK(xl_view_key(view.get(), &key, sizeof(key), &handled) == XL_INVALID_ARGUMENT);
    key = event("x"); CHECK(xl_view_key(view.get(), &key, 1, &handled) == XL_ABI_MISMATCH);
    std::thread wrong([&] { CHECK(xl_view_key(view.get(), &key, sizeof(key), &handled) == XL_WRONG_THREAD); }); wrong.join();
    CHECK(xl_view_set_keyboard_input(view.get(), 1) == XL_UNSUPPORTED);

#ifdef _WIN32
    if (native) {
    page("<button id=a>A</button><button id=b>B</button>");
    script("globalThis.nativeKeys=[];globalThis.activations=0;document.onkeydown=e=>nativeKeys.push(e.key+':'+e.code);document.getElementById('b').onclick=()=>activations++;'ready'", "ready");
    const auto previous_foreground = GetForegroundWindow();
    struct RestoreFocus { HWND previous; ~RestoreFocus() { if (previous) SetForegroundWindow(previous); } } restore{previous_foreground};
    options.kind = XL_VIEW_WINDOW; options.flags = 0;
    xl::View window(runtime, &options); window.document(&document); render(window); window.keyboard_input(true);
    uint32_t kind; uintptr_t handle;
    CHECK(xl_view_native_handle(window.get(), &kind, &handle) == XL_OK && kind == XL_NATIVE_WIN32);
    const auto hwnd = reinterpret_cast<HWND>(handle);
    CHECK(test_foreground(hwnd));
    CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(window);
    const auto post = [&](UINT message, WPARAM code) {
        INPUT input{}; input.type = INPUT_KEYBOARD; input.ki.wVk = WORD(code);
        input.ki.dwFlags = message == WM_KEYUP ? KEYEVENTF_KEYUP : 0;
        CHECK(GetForegroundWindow() == hwnd && SendInput(1, &input, sizeof(input)) == 1);
    };
    post(WM_KEYDOWN, VK_TAB); post(WM_KEYUP, VK_TAB); post(WM_KEYDOWN, VK_TAB); post(WM_KEYUP, VK_TAB);
    post(WM_KEYDOWN, VK_RETURN); post(WM_KEYUP, VK_RETURN);
    const auto delivered = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (;;) {
        CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(window);
        CHECK(document.evaluate("activations") == XL_OK);
        if (document.result() == "1") break;
        CHECK(std::chrono::steady_clock::now() < delivered); std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (focused() != "b") {
        CHECK(document.evaluate("document.activeElement.id+':'+nativeKeys.join(',')") == XL_OK);
        std::cerr << "native input: " << document.result() << '\n';
    }
    CHECK(focused() == "b"); script("activations+':'+nativeKeys.join(',')", "1:Tab:Tab,Tab:Tab,Enter:Enter");
    post(WM_KEYDOWN, VK_SPACE);
    CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(window);
    ShowWindow(hwnd, SW_HIDE);
    CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(window);
    CHECK(test_foreground(hwnd));
    post(WM_KEYUP, VK_SPACE);
    CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(window);
    script("activations", "1");
    }
#endif
    std::cout << "keyboard events, focus, activation, scroll and retained focus passed\n";
}
