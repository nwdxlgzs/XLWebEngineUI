#include "native.h"
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <commctrl.h>
#include <UIAutomation.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <unordered_map>
#include <utility>

namespace xl::detail {
namespace {
std::atomic<uint64_t> object_count{0};
HRESULT status(xl_status value) {
    switch (value) {
    case XL_OK: return S_OK;
    case XL_BUSY: return RPC_E_CALL_REJECTED;
    case XL_WRONG_THREAD: return RPC_E_WRONG_THREAD;
    case XL_NOT_FOUND: return UIA_E_ELEMENTNOTAVAILABLE;
    case XL_OUT_OF_MEMORY: return E_OUTOFMEMORY;
    case XL_INVALID_ARGUMENT: return E_INVALIDARG;
    case XL_UNSUPPORTED: return UIA_E_NOTSUPPORTED;
    default: return E_FAIL;
    }
}
template<class Function> HRESULT com(Function&& function) noexcept {
    try { return function(); }
    catch (const DocumentFailure& error) { return status(error.status); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
struct Api {
    HMODULE module = nullptr;
    decltype(&UiaReturnRawElementProvider) return_provider = nullptr;
    decltype(&UiaHostProviderFromHwnd) host = nullptr;
    decltype(&UiaClientsAreListening) listening = nullptr;
    decltype(&UiaRaiseAutomationEvent) automation = nullptr;
    decltype(&UiaRaiseAutomationPropertyChangedEvent) property = nullptr;
    decltype(&UiaRaiseStructureChangedEvent) structure = nullptr;
    decltype(&UiaDisconnectProvider) disconnect = nullptr;
    decltype(&UiaRaiseNotificationEvent) notification = nullptr;
    Api() {
        wchar_t directory[MAX_PATH]; const auto size = GetSystemDirectoryW(directory, MAX_PATH);
        if (!size || size >= MAX_PATH) throw DocumentFailure{XL_UNSUPPORTED};
        const auto path = std::wstring(directory, size) + L"\\UIAutomationCore.dll";
        module = LoadLibraryW(path.c_str());
        if (!module) throw DocumentFailure{XL_UNSUPPORTED};
#define XL_UIA_LOAD(field, symbol) field = reinterpret_cast<decltype(field)>(GetProcAddress(module, #symbol))
        XL_UIA_LOAD(return_provider, UiaReturnRawElementProvider); XL_UIA_LOAD(host, UiaHostProviderFromHwnd);
        XL_UIA_LOAD(listening, UiaClientsAreListening); XL_UIA_LOAD(automation, UiaRaiseAutomationEvent);
        XL_UIA_LOAD(property, UiaRaiseAutomationPropertyChangedEvent); XL_UIA_LOAD(structure, UiaRaiseStructureChangedEvent);
        XL_UIA_LOAD(disconnect, UiaDisconnectProvider); XL_UIA_LOAD(notification, UiaRaiseNotificationEvent);
#undef XL_UIA_LOAD
        if (!return_provider || !host || !listening || !automation || !property || !structure) {
            FreeLibrary(module); module = nullptr; throw DocumentFailure{XL_UNSUPPORTED};
        }
    }
    ~Api() { if (module) FreeLibrary(module); }
};
struct Variant {
    VARIANT value{};
    ~Variant() { VariantClear(&value); }
};
BSTR wide(std::string_view text) {
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), nullptr, 0);
    if (!count && !text.empty()) throw DocumentFailure{XL_INVALID_ARGUMENT};
    auto* result = SysAllocStringLen(nullptr, UINT(count));
    if (!result && count) throw std::bad_alloc{};
    if (count) MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), int(text.size()), result, count);
    return result;
}
std::string utf8(const wchar_t* text) {
    if (!text) throw DocumentFailure{XL_INVALID_ARGUMENT};
    size_t length = 0; while (length <= 4 * 1024 * 1024 && text[length]) ++length;
    if (length > 4 * 1024 * 1024) throw DocumentFailure{XL_INVALID_ARGUMENT};
    const auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, int(length), nullptr, 0, nullptr, nullptr);
    if ((!count && length) || count > 4 * 1024 * 1024) throw DocumentFailure{XL_INVALID_ARGUMENT};
    std::string result(size_t(count), '\0');
    if (count) WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, int(length), result.data(), count, nullptr, nullptr);
    return result;
}
CONTROLTYPEID control_type(uint32_t role) {
    switch (role) {
    case XL_AX_DOCUMENT: return UIA_DocumentControlTypeId;
    case XL_AX_BUTTON: return UIA_ButtonControlTypeId;
    case XL_AX_LINK: return UIA_HyperlinkControlTypeId;
    case XL_AX_CHECKBOX: return UIA_CheckBoxControlTypeId;
    case XL_AX_RADIO: return UIA_RadioButtonControlTypeId;
    case XL_AX_TEXT_FIELD: return UIA_EditControlTypeId;
    case XL_AX_IMAGE: return UIA_ImageControlTypeId;
    case XL_AX_TEXT: case XL_AX_HEADING: case XL_AX_PARAGRAPH: case XL_AX_STATUS: case XL_AX_ALERT:
    case XL_AX_LOG: case XL_AX_TIMER: case XL_AX_MARQUEE: return UIA_TextControlTypeId;
    default: return UIA_GroupControlTypeId;
    }
}
struct Tree {
    std::shared_ptr<const AXUpdate> update;
    std::unordered_map<xl_node_id, size_t> indices;
    std::unordered_map<xl_node_id, std::vector<xl_node_id>> children;
    int width = 1, height = 1;
    AXEntry empty{{sizeof(xl_ax_node), XL_AX_DOCUMENT, 0, 0, {}, 0, 0}, {}, {}};
    Tree(std::shared_ptr<const AXUpdate> value, int w, int h) : update(std::move(value)), width(w), height(h) {
        empty.node.bounds = {0, 0, float(w), float(h)};
        if (!update) return;
        const auto root = update->info.document;
        for (size_t i = 0; i < update->nodes.size(); ++i) indices.emplace(update->nodes[i].node.node, i);
        for (const auto& entry : update->nodes) if (entry.node.parent)
            children[entry.node.parent == root ? 0 : entry.node.parent].push_back(entry.node.node);
    }
    const AXEntry& entry(xl_node_id node) const {
        if (!node) return !update || update->nodes.empty() ? empty : update->nodes.front();
        const auto found = indices.find(node);
        if (found == indices.end()) throw DocumentFailure{XL_NOT_FOUND};
        return update->nodes[found->second];
    }
    xl_node_id parent(xl_node_id node) const {
        const auto id = entry(node).node.parent;
        return update && id == update->info.document ? 0 : id;
    }
};
struct Geometry { double x, y, sx, sy, width, height; };
Geometry geometry(HWND window, const Tree& tree) {
    RECT client{}; POINT origin{}, end{};
    if (!IsWindow(window) || !GetClientRect(window, &client)) throw DocumentFailure{XL_NOT_FOUND};
    end = {client.right, client.bottom};
    if (!ClientToScreen(window, &origin) || !ClientToScreen(window, &end)) throw DocumentFailure{XL_NOT_FOUND};
    // Available on Vista/Win7. Per-monitor aware callers already use physical
    // coordinates; the optional newer conversion handles unaware host threads.
    using Convert = BOOL(WINAPI*)(HWND, LPPOINT);
    const auto user = GetModuleHandleW(L"user32.dll");
    const auto convert = reinterpret_cast<Convert>(GetProcAddress(user, "LogicalToPhysicalPointForPerMonitorDPI"));
    if (convert) { convert(window, &origin); convert(window, &end); }
    else { LogicalToPhysicalPoint(window, &origin); LogicalToPhysicalPoint(window, &end); }
    const double width = std::max(0L, end.x - origin.x), height = std::max(0L, end.y - origin.y);
    return {double(origin.x), double(origin.y), width / std::max(1, tree.width), height / std::max(1, tree.height), width, height};
}
UiaRect bounds(HWND window, const Tree& tree, xl_node_id node) {
    const auto transform = geometry(window, tree);
    if (!node) return {transform.x, transform.y, transform.width, transform.height};
    const auto& box = tree.entry(node).node.bounds;
    return {transform.x + box.x * transform.sx, transform.y + box.y * transform.sy, box.width * transform.sx, box.height * transform.sy};
}
bool native_focus(HWND window, DWORD thread) {
    GUITHREADINFO info{}; info.cbSize = sizeof(info);
    return GetForegroundWindow() == GetAncestor(window, GA_ROOT) && GetGUIThreadInfo(thread, &info) &&
        (info.hwndFocus == window || (info.hwndFocus && IsChild(window, info.hwndFocus)));
}
class Provider;
struct State : std::enable_shared_from_this<State> {
    std::shared_ptr<Api> api;
    HWND window;
    DWORD owner = GetCurrentThreadId();
    mutable std::mutex mutex;
    bool alive = true;
    std::shared_ptr<const Tree> tree;
    std::unordered_map<xl_node_id, Provider*> providers;
    std::deque<std::shared_ptr<NativeAXRequest>> requests;
    size_t request_bytes = 0;
    State(std::shared_ptr<Api> functions, HWND hwnd) : api(std::move(functions)), window(hwnd), tree(std::make_shared<Tree>(nullptr, 1, 1)) {}
    std::shared_ptr<const Tree> snapshot() const {
        std::lock_guard lock(mutex); if (!alive) throw DocumentFailure{XL_NOT_FOUND}; return tree;
    }
    Provider* provider(xl_node_id node);
    HRESULT request(xl_node_id node, NativeAXAction action, std::string text = {});
    void close() noexcept {
        std::shared_ptr<const Tree> previous;
        { std::lock_guard lock(mutex); alive = false; previous = std::move(tree); request_bytes = 0; }
        for (;;) {
            std::shared_ptr<NativeAXRequest> request;
            { std::lock_guard lock(mutex); if (requests.empty()) break; request = std::move(requests.front()); requests.pop_front(); }
            complete_native_ax(request, XL_NOT_FOUND);
        }
    }
};
class Provider final : public IRawElementProviderSimple, public IRawElementProviderFragment,
                       public IRawElementProviderFragmentRoot, public IInvokeProvider, public IToggleProvider, public IValueProvider,
                       public IExpandCollapseProvider {
public:
    std::shared_ptr<State> state;
    const xl_node_id node;
    std::atomic<ULONG> references{1};
    Provider(std::shared_ptr<State> value, xl_node_id id) : state(std::move(value)), node(id) { ++object_count; }
    ~Provider() {
        { std::lock_guard lock(state->mutex); const auto found = state->providers.find(node);
          if (found != state->providers.end() && found->second == this) state->providers.erase(found); }
        --object_count;
    }
    bool try_addref() {
        auto count = references.load();
        while (count && !references.compare_exchange_weak(count, count + 1)) {}
        return count != 0;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) *output = static_cast<IRawElementProviderSimple*>(this);
        else if (id == __uuidof(IRawElementProviderFragment)) *output = static_cast<IRawElementProviderFragment*>(this);
        else if (id == __uuidof(IRawElementProviderFragmentRoot) && !node) *output = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (id == __uuidof(IInvokeProvider)) *output = static_cast<IInvokeProvider*>(this);
        else if (id == __uuidof(IToggleProvider)) *output = static_cast<IToggleProvider*>(this);
        else if (id == __uuidof(IValueProvider)) *output = static_cast<IValueProvider*>(this);
        else if (id == __uuidof(IExpandCollapseProvider)) *output = static_cast<IExpandCollapseProvider*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --references; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* output) override {
        if (!output) return E_POINTER; *output = ProviderOptions_ServerSideProvider; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        return com([&]() -> HRESULT {
            const auto snapshot = state->snapshot(); const auto role = snapshot->entry(node).node.role;
            if (pattern == UIA_InvokePatternId && (role == XL_AX_BUTTON || role == XL_AX_LINK || role == XL_AX_RADIO)) *output = static_cast<IInvokeProvider*>(this);
            else if (pattern == UIA_TogglePatternId && role == XL_AX_CHECKBOX) *output = static_cast<IToggleProvider*>(this);
            else if (pattern == UIA_ValuePatternId && role == XL_AX_TEXT_FIELD) *output = static_cast<IValueProvider*>(this);
            else if (pattern == UIA_ExpandCollapsePatternId && (snapshot->entry(node).node.actions & (XL_AX_ACTION_EXPAND | XL_AX_ACTION_COLLAPSE)))
                *output = static_cast<IExpandCollapseProvider*>(this);
            if (*output) AddRef(); return S_OK;
        });
    }
    HRESULT property(PROPERTYID property, VARIANT* output, const Tree& tree) {
        const auto& entry = tree.entry(node); const auto& ax = entry.node;
        const auto boolean = [&](bool value) { output->vt = VT_BOOL; output->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE; };
        const auto integer = [&](LONG value) { output->vt = VT_I4; output->lVal = value; };
        const auto string = [&](std::string_view value) { output->bstrVal = wide(value); output->vt = VT_BSTR; };
        switch (property) {
        case UIA_ControlTypePropertyId: integer(control_type(ax.role)); break;
        case UIA_NamePropertyId: if (node || !entry.name.empty()) string(entry.name); break;
        case UIA_AutomationIdPropertyId: string(node ? "xl-node-" + std::to_string(node) : "xl-document"); break;
        case UIA_FrameworkIdPropertyId: string("XLWebEngineUI"); break;
        case UIA_ProviderDescriptionPropertyId: string("XLWebEngineUI native UI Automation provider"); break;
        case UIA_IsEnabledPropertyId: boolean(!(ax.states & XL_AX_DISABLED)); break;
        case UIA_IsKeyboardFocusablePropertyId: boolean(!node || (ax.actions & XL_AX_ACTION_FOCUS)); break;
        case UIA_HasKeyboardFocusPropertyId: boolean(((!node && (!tree.update || !std::any_of(tree.update->nodes.begin(), tree.update->nodes.end(), [](const AXEntry& value) { return value.node.states & XL_AX_FOCUSED; }))) || (ax.states & XL_AX_FOCUSED)) && native_focus(state->window, state->owner)); break;
        case UIA_IsPasswordPropertyId: boolean(ax.states & XL_AX_PROTECTED); break;
        case UIA_IsOffscreenPropertyId: boolean((ax.states & XL_AX_OFFSCREEN) || !IsWindowVisible(state->window)); break;
        case UIA_IsRequiredForFormPropertyId: boolean(ax.states & XL_AX_REQUIRED); break;
        case UIA_IsDataValidForFormPropertyId: boolean(!(ax.states & XL_AX_INVALID)); break;
        case UIA_IsControlElementPropertyId: case UIA_IsContentElementPropertyId:
            boolean(ax.role != XL_AX_GENERIC || !entry.name.empty() || (ax.states & (XL_AX_LIVE_POLITE_REGION | XL_AX_LIVE_ASSERTIVE_REGION))); break;
        case UIA_IsInvokePatternAvailablePropertyId: boolean(ax.role == XL_AX_BUTTON || ax.role == XL_AX_LINK || ax.role == XL_AX_RADIO); break;
        case UIA_SelectionItemIsSelectedPropertyId: if (ax.role == XL_AX_RADIO) boolean(ax.states & XL_AX_CHECKED); break;
        case UIA_IsTogglePatternAvailablePropertyId: boolean(ax.role == XL_AX_CHECKBOX); break;
        case UIA_IsValuePatternAvailablePropertyId: boolean(ax.role == XL_AX_TEXT_FIELD); break;
        case UIA_IsExpandCollapsePatternAvailablePropertyId: boolean(ax.actions & (XL_AX_ACTION_EXPAND | XL_AX_ACTION_COLLAPSE)); break;
        case UIA_ExpandCollapseExpandCollapseStatePropertyId:
            if (ax.states & (XL_AX_EXPANDED | XL_AX_COLLAPSED)) integer(ax.states & XL_AX_EXPANDED ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed);
            break;
        case UIA_ValueValuePropertyId: if (ax.role == XL_AX_TEXT_FIELD && !(ax.states & XL_AX_PROTECTED)) string(entry.value); break;
        case UIA_ValueIsReadOnlyPropertyId: if (ax.role == XL_AX_TEXT_FIELD) boolean(ax.states & (XL_AX_READ_ONLY | XL_AX_DISABLED)); break;
        case UIA_ToggleToggleStatePropertyId: if (ax.role == XL_AX_CHECKBOX) integer(ax.states & XL_AX_MIXED ? ToggleState_Indeterminate : ax.states & XL_AX_CHECKED ? ToggleState_On : ToggleState_Off); break;
        case UIA_LiveSettingPropertyId: integer(ax.states & XL_AX_LIVE_ASSERTIVE_REGION ? LiveSetting::Assertive : ax.states & XL_AX_LIVE_POLITE_REGION ? LiveSetting::Polite : LiveSetting::Off); break;
        case UIA_AriaPropertiesPropertyId: string(std::string("busy=") + (ax.states & XL_AX_BUSY ? "true" : "false")); break;
        case UIA_BoundingRectanglePropertyId: {
            const auto box = bounds(state->window, tree, node);
            auto* array = SafeArrayCreateVector(VT_R8, 0, 4); if (!array) return E_OUTOFMEMORY;
            const double values[]{box.left, box.top, box.width, box.height};
            for (LONG i = 0; i < 4; ++i) SafeArrayPutElement(array, &i, const_cast<double*>(&values[i]));
            output->vt = VT_ARRAY | VT_R8; output->parray = array; break;
        }
        default: break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* output) override {
        if (!output) return E_POINTER; VariantInit(output);
        return com([&] { const auto snapshot = state->snapshot(); return property(id, output, *snapshot); });
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        return com([&]() -> HRESULT { const auto snapshot = state->snapshot(); (void)snapshot->entry(node); return node ? S_OK : state->api->host(state->window, output); });
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        return com([&]() -> HRESULT {
            const auto snapshot = state->snapshot(); (void)snapshot->entry(node);
            xl_node_id target = 0; bool found = false;
            if (direction == NavigateDirection_Parent) { if (node) { target = snapshot->parent(node); found = true; } }
            else if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
                const auto children = snapshot->children.find(node);
                if (children != snapshot->children.end() && !children->second.empty()) {
                    target = direction == NavigateDirection_FirstChild ? children->second.front() : children->second.back(); found = true;
                }
            } else if (direction == NavigateDirection_NextSibling || direction == NavigateDirection_PreviousSibling) {
                if (node) {
                    const auto& siblings = snapshot->children.at(snapshot->parent(node));
                    const auto iterator = std::find(siblings.begin(), siblings.end(), node);
                    if (iterator != siblings.end() && direction == NavigateDirection_NextSibling && iterator + 1 != siblings.end()) { target = *(iterator + 1); found = true; }
                    if (iterator != siblings.end() && direction == NavigateDirection_PreviousSibling && iterator != siblings.begin()) { target = *(iterator - 1); found = true; }
                }
            } else return E_INVALIDARG;
            if (found) *output = static_cast<IRawElementProviderFragment*>(state->provider(target));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        return com([&]() -> HRESULT {
            const auto snapshot = state->snapshot(); (void)snapshot->entry(node); if (!node) return S_OK;
            auto* array = SafeArrayCreateVector(VT_I4, 0, 3); if (!array) return E_OUTOFMEMORY;
            const LONG values[]{UiaAppendRuntimeId, LONG(uint32_t(node)), LONG(uint32_t(node >> 32))};
            for (LONG i = 0; i < 3; ++i) SafeArrayPutElement(array, &i, const_cast<LONG*>(&values[i]));
            *output = array; return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* output) override {
        if (!output) return E_POINTER; *output = {};
        return com([&]() -> HRESULT { const auto snapshot = state->snapshot(); *output = bounds(state->window, *snapshot, node); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** output) override {
        if (!output) return E_POINTER; *output = nullptr; return com([&]() -> HRESULT { const auto snapshot = state->snapshot(); (void)snapshot->entry(node); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return com([&]() -> HRESULT { return state->request(node, NativeAXAction::focus); }); }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        return com([&]() -> HRESULT { const auto snapshot = state->snapshot(); (void)snapshot->entry(node); *output = static_cast<IRawElementProviderFragmentRoot*>(state->provider(0)); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        return com([&]() -> HRESULT {
            const auto snapshot = state->snapshot(); const auto root = bounds(state->window, *snapshot, 0);
            const auto inside = [&](const UiaRect& box) { return box.width > 0 && box.height > 0 && x >= box.left && x < box.left + box.width && y >= box.top && y < box.top + box.height; };
            if (!inside(root)) return S_OK;
            xl_node_id target = 0;
            if (snapshot->update) for (auto it = snapshot->update->nodes.rbegin(); it != snapshot->update->nodes.rend(); ++it) {
                if (it->node.node == snapshot->update->info.document || (it->node.states & XL_AX_OFFSCREEN)) continue;
                if (inside(bounds(state->window, *snapshot, it->node.node))) { target = it->node.node; break; }
            }
            *output = static_cast<IRawElementProviderFragment*>(state->provider(target)); return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** output) override {
        if (!output) return E_POINTER; *output = nullptr;
        return com([&]() -> HRESULT {
            const auto snapshot = state->snapshot(); if (!native_focus(state->window, state->owner) || !snapshot->update) return S_OK;
            for (const auto& entry : snapshot->update->nodes) if (entry.node.states & XL_AX_FOCUSED) {
                *output = static_cast<IRawElementProviderFragment*>(state->provider(entry.node.node)); break;
            }
            if (!*output) *output = static_cast<IRawElementProviderFragment*>(state->provider(0));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Invoke() override { return com([&] { return state->request(node, NativeAXAction::activate); }); }
    HRESULT STDMETHODCALLTYPE Toggle() override { return com([&] { return state->request(node, NativeAXAction::activate); }); }
    HRESULT STDMETHODCALLTYPE Expand() override { return com([&] { return state->request(node, NativeAXAction::expand); }); }
    HRESULT STDMETHODCALLTYPE Collapse() override { return com([&] { return state->request(node, NativeAXAction::collapse); }); }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* output) override {
        if (!output) return E_POINTER; *output = ExpandCollapseState_LeafNode;
        return com([&]() -> HRESULT {
            const auto snapshot = state->snapshot(); const auto& entry = snapshot->entry(node).node;
            if (!(entry.actions & (XL_AX_ACTION_EXPAND | XL_AX_ACTION_COLLAPSE))) return UIA_E_NOTSUPPORTED;
            *output = entry.states & XL_AX_EXPANDED ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed; return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* output) override {
        if (!output) return E_POINTER; *output = ToggleState_Off;
        return com([&]() -> HRESULT {
            const auto snapshot = state->snapshot(); const auto& entry = snapshot->entry(node).node;
            if (entry.role != XL_AX_CHECKBOX) return UIA_E_NOTSUPPORTED;
            *output = entry.states & XL_AX_MIXED ? ToggleState_Indeterminate : entry.states & XL_AX_CHECKED ? ToggleState_On : ToggleState_Off; return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override { return com([&] { return state->request(node, NativeAXAction::value, utf8(value)); }); }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* output) override {
        if (!output) return E_POINTER; *output = nullptr;
        return com([&]() -> HRESULT {
            const auto snapshot = state->snapshot(); const auto& entry = snapshot->entry(node);
            if (entry.node.role != XL_AX_TEXT_FIELD) return UIA_E_NOTSUPPORTED;
            if (entry.node.states & XL_AX_PROTECTED) return E_ACCESSDENIED;
            *output = wide(entry.value); return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* output) override {
        if (!output) return E_POINTER; *output = TRUE;
        return com([&]() -> HRESULT {
            const auto snapshot = state->snapshot(); const auto& entry = snapshot->entry(node).node;
            if (entry.role != XL_AX_TEXT_FIELD) return UIA_E_NOTSUPPORTED;
            *output = (entry.states & (XL_AX_READ_ONLY | XL_AX_DISABLED)) != 0; return S_OK;
        });
    }
};
Provider* State::provider(xl_node_id node) {
    std::lock_guard lock(mutex); if (!alive) throw DocumentFailure{XL_NOT_FOUND};
    (void)tree->entry(node);
    const auto [position, inserted] = providers.try_emplace(node, nullptr);
    if (position->second && position->second->try_addref()) return position->second;
    try { position->second = new Provider(shared_from_this(), node); }
    catch (...) { if (inserted) providers.erase(position); throw; }
    return position->second;
}
HRESULT State::request(xl_node_id node, NativeAXAction action, std::string text) {
    if (GetCurrentThreadId() == owner) return RPC_E_CALL_REJECTED;
    auto request = std::make_shared<NativeAXRequest>(); request->node = node; request->action = action; request->text = std::move(text);
    {
        std::lock_guard lock(mutex); if (!alive) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto& entry = tree->entry(node).node;
        if (entry.states & XL_AX_DISABLED) return UIA_E_ELEMENTNOTENABLED;
        if (!tree->update || !tree->update->info.document) return UIA_E_ELEMENTNOTAVAILABLE;
        if ((action == NativeAXAction::focus && node && !(entry.actions & XL_AX_ACTION_FOCUS)) ||
            (action == NativeAXAction::activate && !(entry.actions & XL_AX_ACTION_ACTIVATE)) ||
            (action == NativeAXAction::expand && !(entry.actions & XL_AX_ACTION_EXPAND)) ||
            (action == NativeAXAction::collapse && !(entry.actions & XL_AX_ACTION_COLLAPSE)) ||
            (action == NativeAXAction::value && (entry.role != XL_AX_TEXT_FIELD || (entry.states & XL_AX_READ_ONLY)))) return UIA_E_NOTSUPPORTED;
        if (requests.size() >= 64 || request->text.size() > 4 * 1024 * 1024 - request_bytes) return RPC_E_SERVERCALL_RETRYLATER;
        request->document = tree->update->info.document;
        requests.push_back(request); request_bytes += request->text.size();
    }
    // Wake an idle native pump, while the host retains control of rendering.
    PostMessageW(window, WM_NULL, 0, 0);
    std::unique_lock lock(request->mutex);
    if (!request->changed.wait_for(lock, std::chrono::seconds(2), [&] { return request->done; })) {
        if (!request->started) request->cancelled = true;
        return UIA_E_TIMEOUT;
    }
    return status(request->status);
}
class Bridge final : public NativeAX {
    std::shared_ptr<State> state;
    Provider* root = nullptr;
    bool owned, hooked = false, focused = false;
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
        auto* self = reinterpret_cast<Bridge*>(data);
        if (message == WM_GETOBJECT && LONG(l) == UiaRootObjectId)
            return self->state->api->return_provider(window, w, l, self->root);
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, procedure, id); self->hooked = false; self->state->close();
            self->state->api->return_provider(window, 0, 0, nullptr);
        }
        return DefSubclassProc(window, message, w, l);
    }
    void events(const std::shared_ptr<const Tree>& before, const std::shared_ptr<const Tree>& after) {
        if (!state->api->listening() || !after->update) return;
        bool structure = false;
        for (const auto& update : after->update->events) {
            const auto& event = update.event;
            if (event.type == XL_AX_EVENT_RESET || event.type == XL_AX_EVENT_NODE_ADDED || event.type == XL_AX_EVENT_NODE_REMOVED || event.type == XL_AX_EVENT_CHILDREN_CHANGED) { structure = true; continue; }
            if (!event.node || !after->indices.contains(event.node)) continue;
            auto* provider = state->provider(event.node == after->update->info.document ? 0 : event.node);
            const auto release = std::unique_ptr<Provider, void(*)(Provider*)>(provider, [](Provider* value) { value->Release(); });
            if (event.type == XL_AX_EVENT_NODE_CHANGED && before->indices.contains(event.node)) {
                constexpr std::pair<uint32_t, PROPERTYID> properties[]{
                    {XL_AX_CHANGE_ROLE, UIA_ControlTypePropertyId}, {XL_AX_CHANGE_NAME, UIA_NamePropertyId},
                    {XL_AX_CHANGE_VALUE, UIA_ValueValuePropertyId}, {XL_AX_CHANGE_BOUNDS, UIA_BoundingRectanglePropertyId},
                    {XL_AX_CHANGE_STATES, UIA_IsEnabledPropertyId}, {XL_AX_CHANGE_STATES, UIA_IsOffscreenPropertyId},
                    {XL_AX_CHANGE_STATES, UIA_HasKeyboardFocusPropertyId}, {XL_AX_CHANGE_STATES, UIA_ToggleToggleStatePropertyId},
                    {XL_AX_CHANGE_STATES, UIA_ValueIsReadOnlyPropertyId}, {XL_AX_CHANGE_STATES, UIA_IsRequiredForFormPropertyId},
                    {XL_AX_CHANGE_STATES, UIA_LiveSettingPropertyId}, {XL_AX_CHANGE_STATES, UIA_AriaPropertiesPropertyId},
                    {XL_AX_CHANGE_STATES, UIA_IsDataValidForFormPropertyId},
                    {XL_AX_CHANGE_STATES, UIA_ExpandCollapseExpandCollapseStatePropertyId},
                    {XL_AX_CHANGE_ACTIONS, UIA_IsExpandCollapsePatternAvailablePropertyId}};
                for (const auto& [mask, property] : properties) if (mask & event.properties) {
                    Variant old_value, new_value;
                    if (SUCCEEDED(provider->property(property, &old_value.value, *before)) && SUCCEEDED(provider->property(property, &new_value.value, *after)))
                        state->api->property(provider, property, old_value.value, new_value.value);
                }
            } else if (event.type == XL_AX_EVENT_FOCUS_CHANGED && focused && native_focus(state->window, state->owner)) state->api->automation(provider, UIA_AutomationFocusChangedEventId);
            else if (event.type == XL_AX_EVENT_LIVE_REGION) {
                if (state->api->notification) {
                    Variant text, activity; text.value.vt = activity.value.vt = VT_BSTR;
                    text.value.bstrVal = wide(update.text); activity.value.bstrVal = wide("xl-live-" + std::to_string(event.node));
                    state->api->notification(provider, NotificationKind_Other,
                        event.flags == XL_AX_LIVE_ASSERTIVE ? NotificationProcessing_ImportantMostRecent : NotificationProcessing_All,
                        text.value.bstrVal, activity.value.bstrVal);
                } else state->api->automation(provider, UIA_LiveRegionChangedEventId);
            }
        }
        if (structure) state->api->structure(root, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    }
public:
    Bridge(HWND window, bool automatic) : state(std::make_shared<State>(std::make_shared<Api>(), window)), owned(automatic) {
        root = state->provider(0);
        if (owned && !SetWindowSubclass(window, procedure, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this))) {
            root->Release(); root = nullptr; throw DocumentFailure{XL_INTERNAL_ERROR};
        }
        hooked = owned;
    }
    ~Bridge() override {
        const bool cleanup = owned && hooked;
        if (cleanup && IsWindow(state->window)) RemoveWindowSubclass(state->window, procedure, reinterpret_cast<UINT_PTR>(this));
        state->close();
        // Extract one cache entry at a time. No allocation or internal mutex is
        // retained across UIA/COM disconnection, including in low-memory close.
        for (;;) {
            Provider* provider = nullptr;
            { std::lock_guard lock(state->mutex);
              if (state->providers.empty()) break;
              const auto found = state->providers.begin();
              if (found->second && found->second->try_addref()) provider = found->second;
              state->providers.erase(found); }
            if (provider) {
                if (state->api->disconnect) state->api->disconnect(provider);
                else CoDisconnectObject(static_cast<IRawElementProviderSimple*>(provider), 0);
                provider->Release();
            }
        }
        if (root) {
            if (cleanup) state->api->return_provider(state->window, 0, 0, nullptr);
            root->Release();
        }
    }
    xl_status close_status() const override { return InSendMessageEx(nullptr) & ISMEX_SEND ? XL_BUSY : XL_OK; }
    xl_status focus_window() override {
        if (!IsWindow(state->window)) return XL_NOT_FOUND;
        if (!IsWindowVisible(state->window) || !IsWindowEnabled(state->window)) return XL_UNSUPPORTED;
        SetForegroundWindow(GetAncestor(state->window, GA_ROOT));
        SetFocus(state->window);
        return native_focus(state->window, state->owner) ? XL_OK : XL_UNSUPPORTED;
    }
    void refresh_focus() noexcept override {
        try {
            const bool next = native_focus(state->window, state->owner);
            const bool gained = next && !focused; focused = next;
            if (!gained || !state->api->listening()) return;
            const auto snapshot = state->snapshot(); xl_node_id target = 0;
            if (snapshot->update) for (const auto& entry : snapshot->update->nodes)
                if (entry.node.states & XL_AX_FOCUSED) { target = entry.node.node; break; }
            auto* provider = state->provider(target);
            state->api->automation(provider, UIA_AutomationFocusChangedEventId); provider->Release();
        } catch (...) {}
    }
    bool pending() const override {
        bool alive, requests;
        { std::lock_guard lock(state->mutex); alive = state->alive; requests = !state->requests.empty(); }
        return alive && (requests || focused != native_focus(state->window, state->owner));
    }
    std::vector<std::shared_ptr<NativeAXRequest>> take_requests() override {
        std::lock_guard lock(state->mutex);
        std::vector<std::shared_ptr<NativeAXRequest>> result(state->requests.begin(), state->requests.end());
        state->requests.clear(); state->request_bytes = 0; return result;
    }
    bool publish(std::shared_ptr<const AXUpdate> update, int width, int height) noexcept override {
        try {
            const auto next = std::make_shared<Tree>(std::move(update), width, height);
            std::shared_ptr<const Tree> previous;
            { std::lock_guard lock(state->mutex); if (!state->alive) return true; previous = std::exchange(state->tree, next); }
            events(previous, next); return true;
        } catch (...) { return false; }
    }
    void provider(uint32_t& kind, uintptr_t& value) override {
        auto* provider = state->provider(0); kind = XL_NATIVE_AX_UIA; value = reinterpret_cast<uintptr_t>(static_cast<IRawElementProviderSimple*>(provider));
    }
};
}
std::unique_ptr<NativeAX> NativeAX::create(uint32_t kind, uintptr_t window, bool owned) {
    if (kind != XL_NATIVE_WIN32 || !window || !IsWindow(reinterpret_cast<HWND>(window))) throw DocumentFailure{XL_UNSUPPORTED};
    if (GetWindowThreadProcessId(reinterpret_cast<HWND>(window), nullptr) != GetCurrentThreadId()) throw DocumentFailure{XL_WRONG_THREAD};
    return std::make_unique<Bridge>(reinterpret_cast<HWND>(window), owned);
}
uint64_t native_ax_objects() noexcept { return object_count.load(); }
}
