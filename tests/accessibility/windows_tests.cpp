#include <XLWebEngineUI/xl.hpp>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <source_location>
#include <thread>
#include "../document/win_input.h"

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
void check(HRESULT value, const std::source_location& at = std::source_location::current()) {
    if (FAILED(value)) { std::cerr << "HRESULT at " << at.line() << ": 0x" << std::hex << uint32_t(value) << '\n'; std::exit(1); }
}
template<class T> struct Ptr {
    T* value = nullptr;
    Ptr() = default;
    explicit Ptr(T* object) : value(object) {}
    ~Ptr() { if (value) value->Release(); }
    Ptr(const Ptr&) = delete;
    Ptr& operator=(const Ptr&) = delete;
    Ptr(Ptr&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
    Ptr& operator=(Ptr&& other) noexcept { if (this != &other) { if (value) value->Release(); value = std::exchange(other.value, nullptr); } return *this; }
    T* operator->() const { return value; }
    T** out() { CHECK(!value); return &value; }
    explicit operator bool() const { return value != nullptr; }
};
template<class T, class U> Ptr<T> query(U* source) { Ptr<T> result; check(source->QueryInterface(__uuidof(T), reinterpret_cast<void**>(result.out()))); return result; }
struct String {
    BSTR value = nullptr;
    explicit String(const wchar_t* text = nullptr) : value(text ? SysAllocString(text) : nullptr) {}
    ~String() { SysFreeString(value); }
    std::wstring text() const { return value ? std::wstring(value, SysStringLen(value)) : L""; }
};
void render(xl::View& view) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    uint32_t flags; xl_status status;
    while ((status = xl_view_render(view.get(), &flags)) == XL_BUSY) { CHECK(std::chrono::steady_clock::now() < until); std::this_thread::yield(); }
    CHECK(status == XL_OK);
}
uint64_t objects() { uint64_t count; CHECK(xl_native_ax_get_object_count(&count) == XL_OK); return count; }
Ptr<IRawElementProviderSimple> server(xl::View& view) {
    uintptr_t raw = 0; uint32_t kind = 0;
    CHECK(xl_view_get_native_ax_provider(view.get(), &kind, &raw) == XL_OK && kind == XL_NATIVE_AX_UIA && raw);
    return Ptr<IRawElementProviderSimple>(reinterpret_cast<IRawElementProviderSimple*>(raw));
}
Ptr<IRawElementProviderSimple> find_server(IRawElementProviderSimple* root, const wchar_t* name) {
    VARIANT value{}; check(root->GetPropertyValue(UIA_NamePropertyId, &value));
    const bool matches = value.vt == VT_BSTR && value.bstrVal && std::wstring_view(value.bstrVal, SysStringLen(value.bstrVal)) == name;
    VariantClear(&value);
    if (matches) { root->AddRef(); return Ptr<IRawElementProviderSimple>(root); }
    auto fragment = query<IRawElementProviderFragment>(root);
    Ptr<IRawElementProviderFragment> child; check(fragment->Navigate(NavigateDirection_FirstChild, child.out()));
    while (child) {
        auto simple = query<IRawElementProviderSimple>(child.value);
        auto found = find_server(simple.value, name); if (found) return found;
        Ptr<IRawElementProviderFragment> next; check(child->Navigate(NavigateDirection_NextSibling, next.out())); child = std::move(next);
    }
    return {};
}
Ptr<IUIAutomationElement> find(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* name, CONTROLTYPEID type) {
    VARIANT named{}; named.vt = VT_BSTR; String text(name); named.bstrVal = text.value;
    VARIANT typed{}; typed.vt = VT_I4; typed.lVal = type;
    Ptr<IUIAutomationCondition> by_name, by_type, both;
    check(automation->CreatePropertyCondition(UIA_NamePropertyId, named, by_name.out()));
    check(automation->CreatePropertyCondition(UIA_ControlTypePropertyId, typed, by_type.out()));
    check(automation->CreateAndCondition(by_name.value, by_type.value, both.out()));
    Ptr<IUIAutomationElement> result; check(root->FindFirst(TreeScope_Descendants, both.value, result.out()));
    if (!result) { std::wcerr << L"UIA element not found: " << name << '\n'; std::exit(1); }
    return result;
}
template<class T> Ptr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    Ptr<IUnknown> object; check(element->GetCurrentPattern(id, object.out())); CHECK(object); return query<T>(object.value);
}
struct PropertyEvents final : IUIAutomationPropertyChangedEventHandler, IUIAutomationNotificationEventHandler,
                              IUIAutomationEventHandler, IUIAutomationStructureChangedEventHandler {
    std::atomic<ULONG> refs{1}; std::atomic<unsigned> renamed{0}, announced{0}, structured{0}, expanded{0}, collapsed{0};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IUIAutomationPropertyChangedEventHandler)) *output = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
        else if (id == __uuidof(IUIAutomationNotificationEventHandler)) *output = static_cast<IUIAutomationNotificationEventHandler*>(this);
        else if (id == __uuidof(IUIAutomationEventHandler)) *output = static_cast<IUIAutomationEventHandler*>(this);
        else if (id == __uuidof(IUIAutomationStructureChangedEventHandler)) *output = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --refs; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement*, PROPERTYID property, VARIANT value) override {
        if (property == UIA_NamePropertyId && value.vt == VT_BSTR && value.bstrVal && std::wstring_view(value.bstrVal) == L"Renamed") ++renamed;
        if (property == UIA_ExpandCollapseExpandCollapseStatePropertyId && value.vt == VT_I4) {
            if (value.lVal == ExpandCollapseState_Expanded) ++expanded;
            if (value.lVal == ExpandCollapseState_Collapsed) ++collapsed;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleNotificationEvent(IUIAutomationElement*, NotificationKind, NotificationProcessing, BSTR text, BSTR) override {
        if (text && std::wstring_view(text, SysStringLen(text)) == L"Ready") ++announced; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID event) override {
        if (event == UIA_LiveRegionChangedEventId) ++announced; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement*, StructureChangeType, SAFEARRAY*) override {
        ++structured; return S_OK;
    }
};
std::atomic<IRawElementProviderSimple*> host_provider{nullptr};
std::atomic<xl_view*> host_view{nullptr};
LRESULT CALLBACK host_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_APP + 65) return xl_view_set_native_accessibility(host_view.load(), 0);
    if (message == WM_GETOBJECT && LONG(l) == UiaRootObjectId)
        if (auto* provider = host_provider.load()) return UiaReturnRawElementProvider(window, w, l, provider);
    return DefWindowProcW(window, message, w, l);
}
int focus_test() {
    if (!test_input_desktop_available()) {
        std::cout << "native UIA focus unavailable: noninteractive input desktop\n";
        return std::getenv("XL_REQUIRE_NATIVE_INPUT") ? 1 : 77;
    }
    check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    const auto previous = GetForegroundWindow();
    {
        xl::Runtime runtime; xl::Document document(runtime);
        document.html("<button id=focus aria-label=Focused style='width:100px;height:40px'></button>");
        xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
        options.width = 240; options.height = 120;
        xl::View view(runtime, &options); view.document(&document); view.native_accessibility(true); render(view);
        uint32_t kind; uintptr_t raw; CHECK(xl_view_native_handle(view.get(), &kind, &raw) == XL_OK);
        const auto window = reinterpret_cast<HWND>(raw); CHECK(test_foreground(window));
        const auto other = CreateWindowExW(0, L"STATIC", L"UIA focus host", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            0, 0, 150, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr); CHECK(other && test_foreground(other));
        std::atomic<bool> done{false};
        std::thread client([&] {
            check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            {
                Ptr<IUIAutomation> automation;
                check(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), reinterpret_cast<void**>(automation.out())));
                Ptr<IUIAutomationElement> root; check(automation->ElementFromHandle(window, root.out()));
                auto button = find(automation.value, root.value, L"Focused", UIA_ButtonControlTypeId);
                check(button->SetFocus()); BOOL focused = FALSE; check(button->get_CurrentHasKeyboardFocus(&focused)); CHECK(focused);
                Ptr<IUIAutomationElement> actual; check(automation->GetFocusedElement(actual.out())); CHECK(actual);
                BOOL same = FALSE; check(automation->CompareElements(actual.value, button.value, &same)); CHECK(same);
            }
            CoUninitialize(); done = true;
        });
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!done) {
            CHECK(std::chrono::steady_clock::now() < until); CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK);
            render(view); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        client.join(); CHECK(GetForegroundWindow() == window && GetFocus() == window);
        CHECK(document.evaluate("document.activeElement.id") == XL_OK && document.result() == "focus");
        DestroyWindow(other);
    }
    if (previous && IsWindow(previous)) SetForegroundWindow(previous);
    CHECK(objects() == 0); CoUninitialize(); std::cout << "Native UIA foreground focus and DOM focus agree\n"; return 0;
}
int main(int argc, char**) {
    _putenv_s("SDL_VIDEODRIVER", "windows");
    if (argc > 1) return focus_test();
    check(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED));
    CHECK(objects() == 0);
    Ptr<IRawElementProviderSimple> retained;
    {
        xl::Runtime runtime; xl::Document document(runtime);
        std::ifstream input(XL_TEST_FONT, std::ios::binary);
        const std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        CHECK(!font.empty() && xl_document_add_font(document.get(), "test", font.data(), font.size()) == XL_OK);
        const std::string base = "<!doctype html><style>body{margin:0;font:18px/24px test}button,input{display:block;width:120px;height:28px;margin:0}</style>";
        document.html(base + "<button id=run>Run</button><input id=check type=checkbox aria-label=Accept>"
            "<input id=message aria-label=Message value=Old><input id=password aria-label=Password type=password value=secret>"
            "<input id=readonly aria-label=Readonly value=Fixed readonly><button id=disabled disabled>Disabled</button>"
            "<div aria-live=polite id=live>Waiting</div>"
            "<details id=fold><summary>Disclosure</summary><p>Extra</p></details>");
        CHECK(document.evaluate("globalThis.clicks=0;globalThis.inputs=[];document.getElementById('run').onclick=()=>clicks++;document.getElementById('message').oninput=e=>inputs.push(e.inputType);'ready'") == XL_OK);
        xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
        options.width = 360; options.height = 260; options.flags = XL_VIEW_HIDDEN;
        xl::View view(runtime, &options); view.document(&document); view.native_accessibility(true); render(view);
        CHECK(objects() == 1 && !view.stats().needs_frame);
        auto root_server = server(view); retained = find_server(root_server.value, L"Run"); CHECK(retained);
        auto fragment = query<IRawElementProviderFragment>(retained.value);
        SAFEARRAY* id1 = nullptr; check(fragment->GetRuntimeId(&id1)); CHECK(id1 && id1->rgsabound[0].cElements == 3); SafeArrayDestroy(id1);
        CHECK(fragment->SetFocus() == RPC_E_CALL_REJECTED); // Own-thread actions must never deadlock the UI.
        uint32_t native_kind; uintptr_t raw_window; CHECK(xl_view_native_handle(view.get(), &native_kind, &raw_window) == XL_OK);
        const auto window = reinterpret_cast<HWND>(raw_window);
        ShowWindow(window, SW_SHOWNOACTIVATE);
        view.keyboard_input(true); view.selection_input(true);
        const auto native_click = [&] {
            render(view); xl_node_bounds summary{};
            CHECK(xl_view_node_bounds(view.get(), document.query("summary"), &summary) == XL_OK);
            RECT client{}; CHECK(GetClientRect(window, &client));
            const auto stats = view.stats();
            const auto x = int((summary.x + 3) * (client.right - client.left) / stats.pixel_width);
            const auto y = int((summary.y + 3) * (client.bottom - client.top) / stats.pixel_height);
            // SDL button events use the last mouse-motion position, rather
            // than reading WM_*BUTTON lParam as a new pointer position.
            CHECK(PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y)));
            CHECK(PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y)));
            CHECK(PostMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y)));
        };
        for (const auto* expected : {"true", "false"}) {
            native_click(); const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            for (;;) {
                CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(view);
                CHECK(document.evaluate("document.getElementById('fold').open") == XL_OK);
                if (document.result() == expected) break;
                if (std::chrono::steady_clock::now() >= end) { std::cerr << "native pointer expected " << expected << " got " << document.result() << '\n'; std::exit(1); }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        std::atomic<int> phase{0}; std::atomic<bool> finished{false};
        std::thread client([&] {
            check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
            {
                Ptr<IUIAutomation> automation;
                if (FAILED(CoCreateInstance(__uuidof(CUIAutomation8), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), reinterpret_cast<void**>(automation.out()))))
                    check(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), reinterpret_cast<void**>(automation.out())));
                Ptr<IUIAutomationElement> root; check(automation->ElementFromHandle(window, root.out())); CHECK(root);
                auto button = find(automation.value, root.value, L"Run", UIA_ButtonControlTypeId);
                auto same_button = find(automation.value, root.value, L"Run", UIA_ButtonControlTypeId);
                BOOL same = FALSE; check(automation->CompareElements(button.value, same_button.value, &same)); CHECK(same);
                RECT box{}; check(button->get_CurrentBoundingRectangle(&box)); CHECK(box.right > box.left && box.bottom > box.top);
                auto* handler = new PropertyEvents; Ptr<IUIAutomationPropertyChangedEventHandler> events(handler);
                PROPERTYID properties[]{UIA_NamePropertyId, UIA_ExpandCollapseExpandCollapseStatePropertyId};
                check(automation->AddPropertyChangedEventHandlerNativeArray(root.value, TreeScope_Subtree, nullptr, handler, properties, 2));
                check(automation->AddStructureChangedEventHandler(root.value, TreeScope_Subtree, nullptr, handler));
                Ptr<IUIAutomation5> modern;
                if (SUCCEEDED(automation->QueryInterface(__uuidof(IUIAutomation5), reinterpret_cast<void**>(modern.out()))))
                    check(modern->AddNotificationEventHandler(root.value, TreeScope_Subtree, nullptr, handler));
                else check(automation->AddAutomationEventHandler(UIA_LiveRegionChangedEventId, root.value, TreeScope_Subtree, nullptr, handler));
                check(pattern<IUIAutomationInvokePattern>(button.value, UIA_InvokePatternId)->Invoke());
                auto checkbox = find(automation.value, root.value, L"Accept", UIA_CheckBoxControlTypeId);
                auto toggle = pattern<IUIAutomationTogglePattern>(checkbox.value, UIA_TogglePatternId); check(toggle->Toggle());
                ToggleState toggled; check(toggle->get_CurrentToggleState(&toggled)); CHECK(toggled == ToggleState_On);
                auto message = find(automation.value, root.value, L"Message", UIA_EditControlTypeId);
                auto value = pattern<IUIAutomationValuePattern>(message.value, UIA_ValuePatternId); String edited(L"Edited \u00e9"); check(value->SetValue(edited.value));
                String current; check(value->get_CurrentValue(&current.value)); CHECK(current.text() == L"Edited \u00e9");
                auto password = find(automation.value, root.value, L"Password", UIA_EditControlTypeId);
                auto password_value = pattern<IUIAutomationValuePattern>(password.value, UIA_ValuePatternId);
                BOOL protected_value = FALSE; check(password->get_CurrentIsPassword(&protected_value)); CHECK(protected_value);
                String secret; const auto secret_status = password_value->get_CurrentValue(&secret.value);
                CHECK(FAILED(secret_status) || secret.text().empty());
                auto readonly = find(automation.value, root.value, L"Readonly", UIA_EditControlTypeId);
                auto readonly_value = pattern<IUIAutomationValuePattern>(readonly.value, UIA_ValuePatternId);
                BOOL read_only; String bad(L"Bad"); check(readonly_value->get_CurrentIsReadOnly(&read_only)); CHECK(read_only && FAILED(readonly_value->SetValue(bad.value)));
                auto disabled = find(automation.value, root.value, L"Disabled", UIA_ButtonControlTypeId);
                CHECK(pattern<IUIAutomationInvokePattern>(disabled.value, UIA_InvokePatternId)->Invoke() == UIA_E_ELEMENTNOTENABLED);
                auto disclosure = find(automation.value, root.value, L"Disclosure", UIA_ButtonControlTypeId);
                auto expansion = pattern<IUIAutomationExpandCollapsePattern>(disclosure.value, UIA_ExpandCollapsePatternId);
                ExpandCollapseState expanded;
                check(expansion->get_CurrentExpandCollapseState(&expanded)); CHECK(expanded == ExpandCollapseState_Collapsed);
                check(expansion->Expand()); check(expansion->Expand());
                check(expansion->get_CurrentExpandCollapseState(&expanded)); CHECK(expanded == ExpandCollapseState_Expanded);
                check(expansion->Collapse()); check(expansion->Collapse());
                check(expansion->get_CurrentExpandCollapseState(&expanded)); CHECK(expanded == ExpandCollapseState_Collapsed);
                phase = 2;
                const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                while (phase < 3 || !handler->renamed.load() || !handler->announced.load() || !handler->structured.load() || !handler->expanded.load() || !handler->collapsed.load()) {
                    if (std::chrono::steady_clock::now() >= until) {
                        std::cerr << "events: renamed=" << handler->renamed << " announced=" << handler->announced << " structured=" << handler->structured << " expanded=" << handler->expanded << " collapsed=" << handler->collapsed << '\n'; std::exit(1);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                String renamed; check(button->get_CurrentName(&renamed.value)); CHECK(renamed.text() == L"Renamed");
                check(automation->RemovePropertyChangedEventHandler(root.value, handler));
                check(automation->RemoveStructureChangedEventHandler(root.value, handler));
                if (modern) check(modern->RemoveNotificationEventHandler(root.value, handler));
                else check(automation->RemoveAutomationEventHandler(UIA_LiveRegionChangedEventId, root.value, handler));
                phase = 4;
                while (phase < 5) { CHECK(std::chrono::steady_clock::now() < until); std::this_thread::yield(); }
                String stale; CHECK(FAILED(button->get_CurrentName(&stale.value)));
            }
            CoUninitialize(); finished = true;
        });
        bool renamed = false, navigated = false;
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!finished) {
            CHECK(std::chrono::steady_clock::now() < until);
            CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(view);
            if (phase == 2 && !renamed) {
                CHECK(document.evaluate("clicks+':'+document.getElementById('check').checked+':'+JSON.stringify(inputs)") == XL_OK);
                CHECK(document.result() == "1:true:[\"insertReplacementText\"]");
                CHECK(document.value(document.query("#message")) == "Edited \xc3\xa9");
                document.attribute(document.query("#run"), "aria-label", "Renamed");
                document.text(document.query("#live"), "Ready"); render(view); renamed = true; phase = 3;
            }
            if (phase == 4 && !navigated) { document.html(base + "<p>New document</p>"); render(view); navigated = true; phase = 5; }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        client.join(); CHECK(renamed && navigated);
        VARIANT stale{}; CHECK(retained->GetPropertyValue(UIA_NamePropertyId, &stale) == UIA_E_ELEMENTNOTAVAILABLE);
        fragment = {}; retained = {}; root_server = {};
        // Cancel an action that timed out while the host deliberately stopped
        // rendering; it must never execute later when rendering resumes.
        document.html(base + "<button id=timeout>Timed</button>");
        CHECK(document.evaluate("globalThis.late=0;document.getElementById('timeout').onclick=()=>late++") == XL_OK); render(view);
        root_server = server(view); auto timed = find_server(root_server.value, L"Timed"); CHECK(timed);
        auto invoke = query<IInvokeProvider>(timed.value); HRESULT timeout_status = S_OK;
        std::thread timeout([&] { timeout_status = invoke->Invoke(); });
        while (!view.stats().needs_frame) { CHECK(std::chrono::steady_clock::now() < until); std::this_thread::yield(); }
        timeout.join(); CHECK(timeout_status == UIA_E_TIMEOUT); render(view);
        CHECK(document.evaluate("late") == XL_OK && document.result() == "0");
        // A document replacement invalidates queued actions before execution.
        HRESULT navigation_status = S_OK; std::thread queued([&] { navigation_status = invoke->Invoke(); });
        while (!view.stats().needs_frame) { CHECK(std::chrono::steady_clock::now() < until); std::this_thread::yield(); }
        document.html(base + "<p>Replacement</p>"); render(view); queued.join(); CHECK(navigation_status == UIA_E_ELEMENTNOTAVAILABLE);
        timed = {}; invoke = {}; root_server = {};
        // Borrowed HWNDs keep their current host procedure. The host can opt
        // into WM_GETOBJECT forwarding using the explicitly owned COM pointer.
        WNDCLASSW type{}; type.lpfnWndProc = host_proc; type.hInstance = GetModuleHandleW(nullptr); type.lpszClassName = L"XLUIATestHost";
        CHECK(RegisterClassW(&type));
        const auto host = CreateWindowExW(0, type.lpszClassName, L"Host", WS_OVERLAPPEDWINDOW, 0, 0, 300, 200, nullptr, nullptr, type.hInstance, nullptr); CHECK(host);
        {
            xl_view_options borrowed_options = options; borrowed_options.kind = XL_VIEW_NATIVE; borrowed_options.flags = 0;
            borrowed_options.native_kind = XL_NATIVE_WIN32; borrowed_options.native_window = reinterpret_cast<uintptr_t>(host);
            xl::View borrowed(runtime, &borrowed_options); borrowed.document(&document);
            const auto procedure = GetWindowLongPtrW(host, GWLP_WNDPROC);
            borrowed.native_accessibility(true); render(borrowed); CHECK(GetWindowLongPtrW(host, GWLP_WNDPROC) == procedure);
            host_view = borrowed.get();
            std::atomic<bool> sent{false}; LRESULT close_status = XL_OK;
            std::thread sender([&] { close_status = SendMessageW(host, WM_APP + 65, 0, 0); sent = true; });
            while (!sent) {
                MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                CHECK(std::chrono::steady_clock::now() < until); std::this_thread::yield();
            }
            sender.join(); CHECK(close_status == XL_BUSY); host_view = nullptr;
            auto provider = server(borrowed); host_provider = provider.value;
            std::atomic<bool> attached{false};
            std::thread host_client([&] {
                check(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
                {
                    Ptr<IUIAutomation> automation;
                    check(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), reinterpret_cast<void**>(automation.out())));
                    Ptr<IUIAutomationElement> element; check(automation->ElementFromHandle(host, element.out()));
                    CONTROLTYPEID control; check(element->get_CurrentControlType(&control)); CHECK(control == UIA_DocumentControlTypeId);
                    auto replacement = find(automation.value, element.value, L"Replacement", UIA_TextControlTypeId); CHECK(replacement);
                }
                CoUninitialize(); attached = true;
            });
            while (!attached) { CHECK(std::chrono::steady_clock::now() < until); CHECK(xl_runtime_pump_events(runtime.get(), XL_PUMP_NATIVE_EVENTS) == XL_OK); render(borrowed); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
            host_client.join();
            host_provider = nullptr; borrowed.native_accessibility(false);
            UiaReturnRawElementProvider(host, 0, 0, nullptr);
            CHECK(GetWindowLongPtrW(host, GWLP_WNDPROC) == procedure && IsWindow(host));
            VARIANT dead{}; CHECK(provider->GetPropertyValue(UIA_NamePropertyId, &dead) == UIA_E_ELEMENTNOTAVAILABLE);
        }
        DestroyWindow(host); UnregisterClassW(type.lpszClassName, type.hInstance);
        retained = server(view); view.native_accessibility(false);
        VARIANT dead{}; CHECK(retained->GetPropertyValue(UIA_NamePropertyId, &dead) == UIA_E_ELEMENTNOTAVAILABLE && objects() >= 1);
        render(view); CHECK(!view.stats().needs_frame);
    }
    CHECK(objects() >= 1); retained = {};
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (objects() && std::chrono::steady_clock::now() < until) { MSG message; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    CHECK(objects() == 0); CoUninitialize();
    std::cout << "Windows UIA COM client, patterns, events, cancellation and provider lifetime passed\n";
}
