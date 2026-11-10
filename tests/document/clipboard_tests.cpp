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
xl_key_event key(const char* text, uint32_t modifiers = XL_MOD_CONTROL) {
    return {sizeof(xl_key_event), XL_KEY_DOWN, modifiers, 0, 0, 0, text, ""};
}
struct Clipboard {
    std::string text, mime = "text/plain;charset=utf-8";
    xl_status read_status = XL_OK, write_status = XL_OK;
    unsigned reads = 0, writes = 0;
    bool reenter = false, null_result = false;
    xl_view* view = nullptr;
    xl_view* other = nullptr;
    xl_document* document = nullptr;
    xl_node_id node = 0;
    void guard() {
        if (!reenter) return;
        uint32_t handled = 93;
        CHECK(xl_view_clipboard(view, XL_CLIPBOARD_COPY, &handled) == XL_BUSY && handled == 93);
        CHECK(xl_view_render(other, &handled) == XL_BUSY && handled == 0);
        CHECK(xl_view_destroy(other) == XL_BUSY);
        CHECK(xl_document_set_value(document, node, "stale", 5) == XL_BUSY);
        CHECK(xl_view_set_clipboard_handler(view, nullptr, 0) == XL_BUSY);
    }
    static xl_status read(void* context, xl_content** result) {
        auto& self = *static_cast<Clipboard*>(context); ++self.reads; self.guard();
        *result = nullptr;
        if (self.read_status != XL_OK) return self.read_status;
        if (self.null_result) return XL_OK;
        return xl_content_create(self.text.data(), self.text.size(), self.mime.c_str(), result);
    }
    static xl_status write(void* context, const char* data, size_t size) {
        auto& self = *static_cast<Clipboard*>(context); ++self.writes; self.guard();
        if (self.write_status != XL_OK) return self.write_status;
        self.text.assign(data, size); return XL_OK;
    }
    xl_clipboard_handler handler() { return {sizeof(xl_clipboard_handler), 0, this, read, write}; }
};
}
int main() {
    xl::Runtime runtime; xl::Document document(runtime);
    std::ifstream stream(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
    xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
    options.kind = XL_VIEW_OFFSCREEN; options.width = 320; options.height = 200;
    Clipboard clipboard;
    xl::View view(runtime, &options), other(runtime, &options);
    view.document(&document); other.document(&document);
    clipboard.view = view.get(); clipboard.other = other.get(); clipboard.document = document.get();
    auto handler = clipboard.handler(); view.clipboard_handler(&handler);
    document.html("<!doctype html><style>body{margin:0;font:20px/24px test}input,textarea{font:20px test;display:block;width:150px}</style>"
        "<input id=a value='abcdef'><textarea id=t>one</textarea><input id=p type=password value='secret'><p id=r>reading text</p>");
    render(view);
    const auto a = document.query("#a"), t = document.query("#t"), p = document.query("#p");
    clipboard.node = a;
    const auto script = [&](const char* source, const char* expected, const std::source_location& location = std::source_location::current()) {
        CHECK(document.evaluate(source) == XL_OK);
        if (document.result() != expected) std::cerr << "script at " << location.line() << ": " << document.result() << " expected " << expected << '\n';
        CHECK(document.result() == expected);
    };
    script("let a=document.getElementById('a'),t=document.getElementById('t'),p=document.getElementById('p');a.focus();'ok'", "ok");
    document.edit_selection(a, 4, 1);
    CHECK(view.clipboard(XL_CLIPBOARD_COPY) && clipboard.text == "bcd");
    CHECK(document.value(a) == "abcdef" && document.edit_state(a).anchor_utf16 == 4);
    CHECK(view.key(key("x")) && clipboard.text == "bcd" && document.value(a) == "aef");
    CHECK(view.key(key("z")) && document.value(a) == "abcdef");
    CHECK(view.key(key("y")) && document.value(a) == "aef");
    clipboard.text = "XY\r\nZ"; CHECK(view.key(key("v", XL_MOD_META)) && document.value(a) == "aXYZef");
    CHECK(view.key(key("z")) && document.value(a) == "aef");
    document.value(a, "A\xf0\x9f\x98\x80" "e\xcc\x81Z"); document.edit_selection(a, 1, 5);
    CHECK(view.clipboard(XL_CLIPBOARD_COPY) && clipboard.text == "\xf0\x9f\x98\x80" "e\xcc\x81");
    CHECK(view.clipboard(XL_CLIPBOARD_CUT) && document.value(a) == "AZ");
    CHECK(view.clipboard(XL_CLIPBOARD_PASTE) && document.value(a) == "A\xf0\x9f\x98\x80" "e\xcc\x81Z");
    document.value(a, "abcdef"); document.edit_selection(a, 1, 4);
    script("globalThis.events=[];a.oncopy=e=>events.push('copy:'+e.clipboardData.types.length+':'+e.clipboardData.getData('text/plain'));"
        "a.oncut=e=>events.push('cut');a.onpaste=e=>events.push('paste:'+e.clipboardData.getData('text'));"
        "a.onbeforeinput=e=>events.push('before:'+e.inputType+':'+e.data);a.oninput=e=>events.push('input:'+e.inputType);'ok'", "ok");
    CHECK(view.clipboard(XL_CLIPBOARD_COPY));
    CHECK(view.clipboard(XL_CLIPBOARD_CUT));
    clipboard.text = "!"; CHECK(view.clipboard(XL_CLIPBOARD_PASTE));
    script("events.join('|')", "copy:0:|cut|before:deleteByCut:null|input:deleteByCut|paste:!|before:insertFromPaste:!|input:insertFromPaste");
    script("a.oncopy=e=>{e.clipboardData.setData('text/plain','replacement');};'ok'", "ok");
    document.value(a, "abcdef"); document.edit_selection(a, 1, 4);
    CHECK(view.clipboard(XL_CLIPBOARD_COPY) && clipboard.text == "bcd"); // setData alone does not override default.
    script("a.oncopy=e=>{e.clipboardData.setData('text/html','ignored');e.clipboardData.setData('Text','replacement');"
        "globalThis.saved=e.clipboardData;globalThis.during=e.clipboardData.types.join(',');e.preventDefault();"
        "Promise.resolve().then(()=>{saved.setData('text','late');globalThis.late=saved.getData('text');});};'ok'", "ok");
    CHECK(view.clipboard(XL_CLIPBOARD_COPY) && clipboard.text == "replacement");
    script("during+':'+saved.types.length+':'+saved.getData('text')+':'+late", "text/plain:0::");
    script("a.oncopy=e=>{e.clipboardData.setData('text','discard');e.clipboardData.clearData('text/plain');e.preventDefault();};'ok'", "ok");
    const auto writes = clipboard.writes;
    CHECK(view.clipboard(XL_CLIPBOARD_COPY) && clipboard.writes == writes);
    script("a.oncut=e=>{e.clipboardData.setData('text','custom cut');e.preventDefault();};'ok'", "ok");
    CHECK(view.clipboard(XL_CLIPBOARD_CUT) && clipboard.text == "custom cut" && document.value(a) == "abcdef");
    script("a.onpaste=e=>{globalThis.pasteSeen=e.clipboardData.types.join(',')+':'+e.clipboardData.getData('TEXT');"
        "e.clipboardData.setData('text','bad');e.clipboardData.clearData();globalThis.saved=e.clipboardData;};'ok'", "ok");
    clipboard.text = "paste"; CHECK(view.clipboard(XL_CLIPBOARD_PASTE) && document.value(a) == "apasteef");
    script("pasteSeen+':'+saved.getData('text')+':'+saved.types.length", "text/plain:paste::0");
    script("a.onpaste=e=>e.preventDefault();'ok'", "ok");
    CHECK(view.clipboard(XL_CLIPBOARD_PASTE) && document.value(a) == "apasteef");
    script("a.oncopy=null;a.oncut=null;a.onpaste=null;a.onbeforeinput=e=>e.preventDefault();'ok'", "ok");
    document.edit_selection(a, 1, 6); CHECK(view.clipboard(XL_CLIPBOARD_CUT));
    CHECK(clipboard.text == "paste" && document.value(a) == "apasteef");
    CHECK(view.clipboard(XL_CLIPBOARD_PASTE) && document.value(a) == "apasteef");
    script("a.onbeforeinput=null;a.onpaste=e=>{a.value='callback';};'ok'", "ok");
    CHECK(view.clipboard(XL_CLIPBOARD_PASTE) && document.value(a) == "callback");
    script("a.onpaste=e=>{t.focus();a.focus();};a.select();'ok'", "ok");
    CHECK(view.clipboard(XL_CLIPBOARD_PASTE) && document.value(a) == "callback");
    script("a.onpaste=e=>a.setSelectionRange(0,0);'ok'", "ok");
    CHECK(view.clipboard(XL_CLIPBOARD_PASTE) && document.value(a) == "callback");
    script("a.onpaste=e=>a.readOnly=true;'ok'", "ok");
    CHECK(!view.clipboard(XL_CLIPBOARD_PASTE) && document.value(a) == "callback");
    script("a.onpaste=null;a.select();'ok'", "ok");
    CHECK(view.clipboard(XL_CLIPBOARD_COPY) && clipboard.text == "callback");
    CHECK(!view.clipboard(XL_CLIPBOARD_CUT) && !view.clipboard(XL_CLIPBOARD_PASTE));
    script("a.readOnly=false;a.oncut=e=>{a.value='cut handler';};a.select();'ok'", "ok");
    const auto before_guarded = clipboard.writes;
    CHECK(view.clipboard(XL_CLIPBOARD_CUT) && clipboard.writes == before_guarded && document.value(a) == "cut handler");
    script("a.oncut=null;a.select();'ok'", "ok");
    uint32_t handled = 91;
    clipboard.write_status = XL_CANCELED;
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_CUT, &handled) == XL_CANCELED && handled == 91);
    CHECK(document.value(a) == "cut handler" && document.edit_state(a).anchor_utf16 == 0);
    clipboard.write_status = XL_OK; clipboard.read_status = XL_BUSY;
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_PASTE, &handled) == XL_BUSY && handled == 91);
    clipboard.read_status = XL_OK; clipboard.null_result = true;
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_PASTE, &handled) == XL_INVALID_ARGUMENT && handled == 91);
    clipboard.null_result = false; clipboard.mime = "text/html";
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_PASTE, &handled) == XL_INVALID_ARGUMENT && handled == 91);
    clipboard.mime = "text/plain";
    for (const auto& invalid : {std::string("bad\0text", 8), std::string("\xff"), std::string(4 * 1024 * 1024 + 1, 'x')}) {
        clipboard.text = invalid;
        CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_PASTE, &handled) == XL_INVALID_ARGUMENT && handled == 91);
        CHECK(document.value(a) == "cut handler");
    }
    clipboard.text = "safe"; clipboard.reenter = true;
    CHECK(view.clipboard(XL_CLIPBOARD_COPY)); clipboard.text = "safe"; CHECK(view.clipboard(XL_CLIPBOARD_PASTE));
    clipboard.reenter = false; CHECK(document.value(a) == "safe");
    // Script failures and timeouts do not reach clipboard writes or delete text.
    script("a.select();a.oncut=e=>{e.clipboardData.setData('text','uncommitted');throw Error('cut failure');};'ok'", "ok");
    const auto before_error = clipboard.writes;
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_CUT, &handled) == XL_SCRIPT_ERROR && handled == 91);
    CHECK(document.value(a) == "safe" && clipboard.writes == before_error);
    script("a.oncut=()=>{while(true){}};'ok'", "ok");
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_CUT, &handled) == XL_TIMEOUT && handled == 91);
    CHECK(document.value(a) == "safe" && clipboard.writes == before_error);
    script("a.oncut=null;a.oncopy=e=>{e.clipboardData.setData('text','\\u0000');e.preventDefault();};'ok'", "ok");
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_COPY, &handled) == XL_INVALID_ARGUMENT && handled == 91);
    script("a.oncopy=null;a.onkeydown=e=>{if(e.ctrlKey)e.preventDefault();};'ok'", "ok");
    const auto reads = clipboard.reads;
    CHECK(view.key(key("v")) && clipboard.reads == reads);
    script("a.onkeydown=null;p.focus();p.select();'ok'", "ok");
    const auto before_password = clipboard.writes;
    CHECK(view.clipboard(XL_CLIPBOARD_COPY) && view.clipboard(XL_CLIPBOARD_CUT));
    CHECK(clipboard.writes == before_password && document.value(p) == "secret");
    clipboard.text = "new"; CHECK(view.clipboard(XL_CLIPBOARD_PASTE) && document.value(p) == "new");
    script("p.blur();'ok'", "ok"); render(view);
    const xl_text_position password_first{sizeof(password_first), 0, p, XL_TEXT_DOWNSTREAM, 0};
    const xl_text_position password_last{sizeof(password_last), 3, p, XL_TEXT_DOWNSTREAM, 0};
    view.selection(password_first, password_last);
    const auto before_reading_password = clipboard.writes;
    CHECK(!view.clipboard(XL_CLIPBOARD_COPY) && clipboard.writes == before_reading_password);
    view.clear_selection();
    script("a.focus();a.select();'ok'", "ok");
    xl_composition update{sizeof(update), XL_COMPOSITION_UPDATE, 0, 1, "x", 1};
    CHECK(view.composition(update)); const auto during_composition = clipboard.writes;
    CHECK(!view.clipboard(XL_CLIPBOARD_COPY) && !view.clipboard(XL_CLIPBOARD_CUT) && !view.clipboard(XL_CLIPBOARD_PASTE));
    CHECK(clipboard.writes == during_composition && view.key(key("v")));
    xl_composition cancel{sizeof(cancel), XL_COMPOSITION_CANCEL, 0, 0, nullptr, 0}; CHECK(view.composition(cancel));
    script("t.focus();t.select();'ok'", "ok");
    clipboard.text = "two\r\nthree\rfour";
    CHECK(view.key(key("Insert", XL_MOD_SHIFT)) && document.value(t) == "two\nthree\nfour");
    document.edit_selection(t, 0, 3); CHECK(view.key(key("Insert")) && clipboard.text == "two");
    CHECK(view.key(key("Delete", XL_MOD_SHIFT)) && document.value(t) == "\nthree\nfour");
    CHECK(view.key(key("z")) && document.value(t) == "two\nthree\nfour");
    script("t.blur();'ok'", "ok"); render(view);
    xl_node_id text_node = 0;
    size_t count = 0; CHECK(xl_view_ax_count(view.get(), &count) == XL_OK);
    for (size_t i = 0; i < count; ++i) {
        xl_ax_node node{}; CHECK(xl_view_ax_get(view.get(), i, &node, sizeof(node)) == XL_OK);
        if (node.role == XL_AX_TEXT) text_node = node.node;
    }
    CHECK(text_node);
    const xl_text_position first{sizeof(first), 0, text_node, XL_TEXT_DOWNSTREAM, 0};
    const xl_text_position last{sizeof(last), 7, text_node, XL_TEXT_DOWNSTREAM, 0};
    view.selection(first, last); CHECK(view.key(key("c")) && clipboard.text == "reading");
    CHECK(!view.clipboard(XL_CLIPBOARD_CUT));
    document.text(document.query("#r"), "changed");
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_COPY, &handled) == XL_BUSY && handled == 91);
    render(view); view.clear_selection();
    CHECK(!view.clipboard(XL_CLIPBOARD_COPY));
    script("a.focus();a.value='shared';a.select();'ok'", "ok");
    uint32_t other_handled = 42;
    CHECK(xl_view_clipboard(other.get(), XL_CLIPBOARD_COPY, &other_handled) == XL_UNSUPPORTED && other_handled == 42);
    Clipboard isolated; auto isolated_handler = isolated.handler(); other.clipboard_handler(&isolated_handler);
    CHECK(other.clipboard(XL_CLIPBOARD_COPY) && isolated.text == "shared");
    isolated.text = "peer"; CHECK(other.clipboard(XL_CLIPBOARD_PASTE) && document.value(a) == "peer");
    CHECK(clipboard.text == "reading");
    render(view); render(other); CHECK(view.render() == 0 && other.render() == 0);
    handler.read = nullptr; view.clipboard_handler(&handler);
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_PASTE, &handled) == XL_UNSUPPORTED && handled == 91);
    view.clipboard_handler(nullptr); document.edit_selection(a, 0, 4);
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_COPY, &handled) == XL_UNSUPPORTED && handled == 91);
    CHECK(xl_view_clipboard(view.get(), 0, &handled) == XL_INVALID_ARGUMENT && handled == 91);
    CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_COPY, nullptr) == XL_INVALID_ARGUMENT);
    CHECK(xl_view_set_clipboard_handler(view.get(), &handler, 1) == XL_ABI_MISMATCH);
    handler.reserved = 1; CHECK(xl_view_set_clipboard_handler(view.get(), &handler, sizeof(handler)) == XL_INVALID_ARGUMENT);
    CHECK(xl_view_set_clipboard_handler(view.get(), nullptr, 1) == XL_INVALID_ARGUMENT);
    std::thread wrong([&] { CHECK(xl_view_clipboard(view.get(), XL_CLIPBOARD_COPY, &handled) == XL_WRONG_THREAD && handled == 91); });
    wrong.join();
    std::cout << "plain clipboard, event lifetime, guarded editing and host isolation passed\n";
}
