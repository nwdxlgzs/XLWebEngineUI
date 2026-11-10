#include <XLWebEngineUI/xl.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#include <source_location>
#include "win_input.h"
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
namespace {
void render(xl::View& view, const std::source_location& location = std::source_location::current()) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags; xl_status status;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < until); std::this_thread::yield();
    }
    if (status != XL_OK) std::cerr << "render at line " << location.line() << ": " << xl_status_string(status) << '\n';
    CHECK(status == XL_OK);
}
xl_key_event event(const char* key, uint32_t modifiers = 0) { return {sizeof(xl_key_event), XL_KEY_DOWN, modifiers, 0, 0, 0, key, ""}; }
std::vector<unsigned char> pixels(xl::View& view) {
    const auto stats = view.stats(); std::vector<unsigned char> data(size_t(stats.pixel_width) * stats.pixel_height * 4);
    CHECK(xl_view_copy_rgba(view.get(), data.data(), data.size(), size_t(stats.pixel_width) * 4) == XL_OK); return data;
}
}
int main(int argc, char**) {
#ifdef _WIN32
    const bool native = argc > 1;
    if (native && !test_input_desktop_available()) { std::cout << "native text input unavailable: noninteractive input desktop\n"; return std::getenv("XL_REQUIRE_NATIVE_INPUT") ? 1 : 77; }
#else
    (void)argc;
#endif
    xl::Runtime runtime; xl::Document document(runtime);
    std::ifstream stream(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    const std::string font_path = XL_TEST_FONT;
    const auto font_directory = font_path.substr(0, font_path.find_last_of("/\\") + 1);
    // Explicit, identical coverage on both targets; no system fallback assumption.
    // The locked Skia CJK subset contains U+662F, and colr.ttf contains U+1F600.
    for (const auto& [file, family] : {std::pair{"colr.ttf", "emoji"}, {"NotoSansCJK-VF-subset.otf.ttc", "cjk"}}) {
        std::ifstream fallback(font_directory + file, std::ios::binary);
        const std::vector<char> bytes{std::istreambuf_iterator<char>(fallback), std::istreambuf_iterator<char>()};
        CHECK(!bytes.empty() && xl_document_add_font(document.get(), family, bytes.data(), bytes.size()) == XL_OK);
    }
    xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 320; options.height = 200;
    xl::View view(runtime, &options); view.document(&document);
    const std::string base = "<!doctype html><style>body{margin:0;font:20px/24px test}input,textarea{display:block;width:110px;height:30px;padding:2px;border:1px solid gray;margin:0}</style>";
    const auto page = [&](const char* html) { document.html(base + html); render(view); };
    const auto script = [&](const char* code, const char* expected) {
        CHECK(document.evaluate(code) == XL_OK);
        if (document.result() != expected) std::cerr << "result " << document.result() << " expected " << expected << '\n';
        CHECK(document.result() == expected);
    };
    const auto focus = [&](const char* id) {
        const auto code = std::string("document.getElementById('") + id + "').focus()";
        CHECK(document.evaluate(code) == XL_OK); render(view);
    };
    page("<input id=a value='old'><textarea id=t>first\nsecond</textarea><input id=empty placeholder='hint'>");
    auto id = document.query("#a");
    CHECK(document.value(id) == "old" && document.value(document.query("#t")) == "first\nsecond");
    script("let a=document.getElementById('a');a.value='new';a.getAttribute('value')+':'+a.textContent+':'+a.value", "old::new");
    CHECK(document.value(id) == "new");
    script("a.setAttribute('value','default');a.value", "new");
    CHECK(document.edit_state(id).focus_utf16 == 3);
    document.value(id, "A\xf0\x9f\x98\x80" "e\xcc\x81Z");
    CHECK(document.edit_state(id).focus_utf16 == 6);
    focus("a");
    CHECK(view.key(event("Backspace"))); CHECK(document.value(id) == "A\xf0\x9f\x98\x80" "e\xcc\x81");
    CHECK(view.key(event("Backspace"))); CHECK(document.value(id) == "A\xf0\x9f\x98\x80");
    CHECK(view.key(event("ArrowLeft"))); CHECK(document.edit_state(id).focus_utf16 == 1);
    CHECK(view.key(event("Delete"))); CHECK(document.value(id) == "A");
    CHECK(view.key(event("z", XL_MOD_CONTROL))); CHECK(document.value(id) == "A\xf0\x9f\x98\x80");
    CHECK(view.key(event("y", XL_MOD_CONTROL))); CHECK(document.value(id) == "A");
    document.value(id, "one two three"); document.edit_selection(id, 4, 7);
    CHECK(view.text_input("TWO")); CHECK(document.value(id) == "one TWO three");
    CHECK(view.key(event("ArrowLeft", XL_MOD_CONTROL))); CHECK(document.edit_state(id).focus_utf16 == 4);
    CHECK(view.key(event("ArrowRight", XL_MOD_SHIFT | XL_MOD_CONTROL)));
    CHECK(document.edit_state(id).anchor_utf16 == 4 && document.edit_state(id).focus_utf16 == 7);
    script("a.selectionStart+':'+a.selectionEnd+':'+a.selectionDirection", "4:7:forward");
    script("a.setSelectionRange(1,3,'backward');a.selectionDirection", "backward");
    CHECK(document.edit_state(id).anchor_utf16 == 3 && document.edit_state(id).focus_utf16 == 1);
    document.value(id, "\xf0\x9f\x98\x80");
    CHECK(xl_document_set_edit_selection(document.get(), id, 1, 1) == XL_INVALID_ARGUMENT);
    CHECK(document.edit_state(id).focus_utf16 == 2);
    document.value(id, "before"); document.edit_selection(id, 0, 6);
    script("globalThis.events=[];a.onbeforeinput=e=>events.push('before:'+e.inputType+':'+e.data+':'+e.isComposing);a.oninput=e=>events.push('input:'+e.inputType+':'+e.data);a.onchange=()=>events.push('change');'ready'", "ready");
    CHECK(view.text_input("after")); script("events.join('|')", "before:insertText:after:false|input:insertText:after");
    focus("t"); script("events[events.length-1]", "change"); focus("a");
    script("a.onbeforeinput=e=>e.preventDefault();'ready'", "ready");
    const auto state = document.edit_state(id);
    CHECK(view.text_input("blocked")); CHECK(document.value(id) == "after");
    CHECK(view.key(event("Backspace"))); CHECK(document.edit_state(id).focus_utf16 == state.focus_utf16);
    script("a.onbeforeinput=e=>{a.value='handler';};'ready'", "ready");
    CHECK(view.text_input("stale")); CHECK(document.value(id) == "handler");
    script("a.onbeforeinput=null;a.maxLength=3;a.setAttribute('maxlength','3');a.value='';'ready'", "ready");
    CHECK(view.text_input("A\xf0\x9f\x98\x80Z")); CHECK(document.value(id) == "A\xf0\x9f\x98\x80");
    script("a.removeAttribute('maxlength');a.readOnly=true;'ready'", "ready");
    CHECK(!view.text_input("no") && !view.key(event("Backspace")));
    CHECK(view.key(event("a", XL_MOD_CONTROL))); CHECK(document.edit_state(id).anchor_utf16 == 0);
    script("a.readOnly=false;'ready'", "ready");
    document.value(id, "guard"); CHECK(view.text_input("!"));
    script("a.onbeforeinput=e=>{if(e.inputType==='historyUndo')a.readOnly=true;};'ready'", "ready");
    CHECK(view.key(event("z", XL_MOD_CONTROL))); CHECK(document.value(id) == "guard!");
    script("a.readOnly=false;a.onbeforeinput=null;'ready'", "ready");
    document.value(id, "ABCDE"); document.edit_selection(id, 1, 4);
    script("events=[];a.oncompositionstart=e=>events.push('start');a.oncompositionupdate=e=>events.push('update:'+e.data);a.oncompositionend=e=>events.push('end:'+e.data);'ready'", "ready");
    const std::string preedit = "\xe6\x98\xaf\xf0\x9f\x98\x80";
    xl_composition update{sizeof(update), XL_COMPOSITION_UPDATE, 1, 3, preedit.data(), preedit.size()};
    CHECK(view.composition(update)); CHECK(document.value(id) == "A" + preedit + "E");
    auto editing = document.edit_state(id);
    CHECK((editing.flags & XL_EDIT_COMPOSING) && editing.composition_start_utf16 == 1 && editing.composition_end_utf16 == 4);
    CHECK(editing.anchor_utf16 == 2 && editing.focus_utf16 == 4); render(view);
    CHECK(view.render() == 0);
    CHECK(view.text_input("OK")); CHECK(document.value(id) == "AOKE");
    CHECK(!(document.edit_state(id).flags & XL_EDIT_COMPOSING));
    CHECK(view.key(event("z", XL_MOD_CONTROL))); CHECK(document.value(id) == "ABCDE");
    CHECK(view.key(event("y", XL_MOD_CONTROL))); CHECK(document.value(id) == "AOKE");
    document.edit_selection(id, 1, 3); update.selection_start_utf16 = update.selection_end_utf16 = 1;
    CHECK(view.composition(update)); CHECK(view.key(event("Escape"))); CHECK(document.value(id) == "AOKE");
    CHECK(view.composition(update)); focus("t"); CHECK(document.value(id) == "AOKE");
    focus("a");
    const auto original = document.value(id);
    CHECK(view.composition(update)); document.edit_selection(id, 0, 0);
    CHECK(!(document.edit_state(id).flags & XL_EDIT_COMPOSING));
    CHECK(view.key(event("z", XL_MOD_CONTROL))); CHECK(document.value(id) == original);
    update.selection_start_utf16 = update.selection_end_utf16 = 2;
    uint32_t handled = 91;
    CHECK(xl_view_set_composition(view.get(), &update, sizeof(update), &handled) == XL_INVALID_ARGUMENT && handled == 91);
    CHECK(!(document.edit_state(id).flags & XL_EDIT_COMPOSING));

    const auto textarea = document.query("#t");
    focus("t"); document.value(textarea, "one\r\ntwo\rthree"); CHECK(document.value(textarea) == "one\ntwo\nthree");
    CHECK(view.key(event("Enter"))); CHECK(document.value(textarea) == "one\ntwo\nthree\n"); render(view);
    CHECK(view.key(event("ArrowUp"))); CHECK(document.edit_state(textarea).focus_utf16 < 14); render(view);
    CHECK(view.key(event("Home", XL_MOD_CONTROL))); CHECK(document.edit_state(textarea).focus_utf16 == 0);
    CHECK(view.text_input("start\n")); render(view);
    CHECK(view.key(event("End", XL_MOD_CONTROL))); render(view);
    CHECK(view.scroll(textarea).y > 0);
    { xl::View full(runtime, &options); full.document(&document); render(full);
      const auto scroll = view.scroll(textarea); full.scroll_to(textarea, scroll.x, scroll.y); render(full);
      CHECK(pixels(view) == pixels(full)); }
    document.value(id, "one\r\ntwo\n"); CHECK(document.value(id) == "onetwo");
    focus("empty"); CHECK(view.text_input("visible")); render(view);
    const auto empty = document.query("#empty"); CHECK(document.value(empty) == "visible");
    const auto caret = view.caret({sizeof(xl_text_position), 3, empty, XL_TEXT_DOWNSTREAM, 0});
    view.select_at(caret.x + .1f, caret.y + caret.height * .5f); render(view);
    CHECK(std::abs(int(document.edit_state(empty).focus_utf16) - 3) <= 1);

    page("<input id=p type=password value='secret' aria-label='Password'>"); id = document.query("#p"); focus("p");
    size_t count; CHECK(xl_view_ax_count(view.get(), &count) == XL_OK); bool found = false;
    for (size_t i = 0; i < count; ++i) {
        xl_ax_node node; CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
        if (node.node != id) continue;
        found = true; CHECK(node.states & XL_AX_PROTECTED);
        size_t required; CHECK(xl_view_ax_value(view.get(), i, nullptr, 0, &required) == XL_BUFFER_TOO_SMALL);
        std::string value(required, '\0'); CHECK(xl_view_ax_value(view.get(), i, value.data(), value.size(), &required) == XL_OK);
        CHECK(value.find("secret") == std::string::npos && required == 18);
    }
    CHECK(found);
    document.value(id, "A\xf0\x9f\x98\x80" "B"); render(view);
    const auto emoji_caret = view.caret({sizeof(xl_text_position), 3, id, XL_TEXT_DOWNSTREAM, 0});
    const auto emoji_hit = view.hit_text(emoji_caret.x - .1f, emoji_caret.y + emoji_caret.height * .5f);
    CHECK(emoji_hit.node == id && emoji_hit.offset_utf16 != 2);
    document.value(id, "secret");
    document.edit_selection(id, 0, 6); CHECK(view.text_input("changed")); render(view);
    { xl::View full(runtime, &options); full.document(&document); render(full);
      CHECK(pixels(full) == pixels(view)); }
    CHECK(view.render() == 0);
    handled = 91; CHECK(xl_view_text_input(view.get(), "\xc0\xaf", 2, &handled) == XL_INVALID_ARGUMENT && handled == 91);
    CHECK(xl_document_set_value(document.get(), id, "a\0b", 3) == XL_INVALID_ARGUMENT);
    char untouched = 'x'; size_t required;
    CHECK(xl_document_get_value(document.get(), id, &untouched, 1, &required) == XL_BUFFER_TOO_SMALL && untouched == 'x');
    std::thread wrong([&] { CHECK(xl_view_text_input(view.get(), "x", 1, &handled) == XL_WRONG_THREAD); }); wrong.join();
#ifdef _WIN32
    if (native) {
    page("<input id=n value=''>"); focus("n");
    const auto previous = GetForegroundWindow();
    struct Restore { HWND previous; ~Restore() { if (previous) SetForegroundWindow(previous); } } restore{previous};
    options.kind = XL_VIEW_WINDOW; options.flags = 0;
    xl::View window(runtime, &options); window.document(&document); render(window); window.keyboard_input(true); render(window);
    uint32_t kind; uintptr_t native;
    CHECK(xl_view_native_handle(window.get(), &kind, &native) == XL_OK && kind == XL_NATIVE_WIN32);
    const auto hwnd = reinterpret_cast<HWND>(native); CHECK(test_foreground(hwnd));
    CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(window);
    CHECK(PostMessageW(hwnd, WM_CHAR, 'A', 1));
    CHECK(PostMessageW(hwnd, WM_CHAR, 0x4e2d, 1));
    CHECK(PostMessageW(hwnd, WM_CHAR, 0xd83d, 1)); CHECK(PostMessageW(hwnd, WM_CHAR, 0xde00, 1));
    CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); CHECK(window.stats().needs_frame); render(window);
    id = document.query("#n"); CHECK(document.value(id) == "A\xe4\xb8\xad\xf0\x9f\x98\x80");
    CHECK(document.edit_state(id).focus_utf16 == 4);
    CHECK(PostMessageW(hwnd, WM_KEYDOWN, VK_BACK, LPARAM(1u | (MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC) << 16))));
    CHECK(PostMessageW(hwnd, WM_KEYUP, VK_BACK, LPARAM(1u | (MapVirtualKeyW(VK_BACK, MAPVK_VK_TO_VSC) << 16) | 0xc0000000u)));
    CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(window);
    CHECK(document.value(id) == "A\xe4\xb8\xad");
    }
#endif
    std::cout << "text fields, Unicode edits, input events, IME composition and retained geometry passed\n";
}
