#include <XLWebEngineUI/xl.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
int main(int argc, char** argv) {
    CHECK(argc == 2); CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
    _putenv_s("SDL_VIDEODRIVER", "windows");
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        const auto module = LoadLibraryA(argv[1]); CHECK(module);
#define LOAD(name) const auto name = reinterpret_cast<decltype(&::name)>(GetProcAddress(module, #name)); CHECK(name)
        LOAD(xl_runtime_create); LOAD(xl_runtime_destroy); LOAD(xl_runtime_pump_events);
        LOAD(xl_document_create); LOAD(xl_document_destroy); LOAD(xl_document_load_html);
        LOAD(xl_view_options_init); LOAD(xl_view_create); LOAD(xl_view_destroy); LOAD(xl_view_set_document);
        LOAD(xl_view_render); LOAD(xl_view_native_handle); LOAD(xl_view_set_native_accessibility);
        LOAD(xl_view_get_native_ax_provider); LOAD(xl_native_ax_get_object_count);
#undef LOAD
        uint64_t count = 123; CHECK(xl_native_ax_get_object_count(&count) == XL_OK && !count);
        xl_runtime* runtime = nullptr; xl_document* document = nullptr; xl_view* view = nullptr;
        CHECK(xl_runtime_create(nullptr, &runtime) == XL_OK && xl_document_create(runtime, &document) == XL_OK);
        const char html[] = "<style>body{margin:0}button{width:60px;height:24px}</style><button aria-label=Unload></button>";
        CHECK(xl_document_load_html(document, html, sizeof(html) - 1) == XL_OK);
        xl_view_options options; CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
        options.width = 160; options.height = 100; options.flags = XL_VIEW_HIDDEN;
        CHECK(xl_view_create(runtime, &options, &view) == XL_OK && xl_view_set_document(view, document) == XL_OK);
        CHECK(xl_view_set_native_accessibility(view, 1) == XL_OK);
        uint32_t flags; CHECK(xl_view_render(view, &flags) == XL_OK);
        uint32_t kind; uintptr_t provider_pointer, window_pointer;
        CHECK(xl_view_get_native_ax_provider(view, &kind, &provider_pointer) == XL_OK && kind == XL_NATIVE_AX_UIA);
        auto* retained = reinterpret_cast<IRawElementProviderSimple*>(provider_pointer);
        CHECK(xl_view_native_handle(view, &kind, &window_pointer) == XL_OK && kind == XL_NATIVE_WIN32);
        std::atomic<bool> done{false};
        std::thread client([&] {
            CHECK(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
            IUIAutomation* automation = nullptr;
            CHECK(SUCCEEDED(CoCreateInstance(__uuidof(CUIAutomation), nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation), reinterpret_cast<void**>(&automation))));
            IUIAutomationElement* root = nullptr;
            CHECK(SUCCEEDED(automation->ElementFromHandle(reinterpret_cast<HWND>(window_pointer), &root)) && root);
            CONTROLTYPEID control; CHECK(SUCCEEDED(root->get_CurrentControlType(&control)) && control == UIA_DocumentControlTypeId);
            IUIAutomationCondition* condition = nullptr; CHECK(SUCCEEDED(automation->CreateTrueCondition(&condition)));
            IUIAutomationElementArray* children = nullptr; CHECK(SUCCEEDED(root->FindAll(TreeScope_Descendants, condition, &children)));
            int length = 0; CHECK(SUCCEEDED(children->get_Length(&length)) && length >= 3);
            children->Release(); condition->Release(); root->Release(); automation->Release(); CoUninitialize(); done = true;
        });
        const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!done) {
            CHECK(std::chrono::steady_clock::now() < until);
            CHECK(xl_runtime_pump_events(runtime, XL_PUMP_NATIVE_EVENTS) == XL_OK && xl_view_render(view, &flags) == XL_OK);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        client.join();
        CHECK(xl_view_destroy(view) == XL_OK && xl_document_destroy(document) == XL_OK && xl_runtime_destroy(runtime) == XL_OK);
        CHECK(xl_native_ax_get_object_count(&count) == XL_OK && count >= 1);
        VARIANT value{}; CHECK(retained->GetPropertyValue(UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE);
        retained->Release(); CHECK(xl_native_ax_get_object_count(&count) == XL_OK && count == 0);
        CHECK(FreeLibrary(module) && GetModuleHandleA("XLWebEngineUI.dll") == nullptr);
    }
    CoUninitialize(); std::cout << "Native UIA DLL expose/disconnect/retained provider/real unload passed three cycles\n";
}
