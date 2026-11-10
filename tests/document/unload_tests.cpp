#include <XLWebEngineUI/xl.h>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#define CHECK(value) do { if (!(value)) { std::cerr << __LINE__ << ": " #value "\n"; std::exit(1); } } while (false)
int main(int argc, char** argv) {
    CHECK(argc == 2 || (argc == 3 && std::strcmp(argv[2], "--dawn") == 0));
    std::ifstream input(XL_TEST_FONT, std::ios::binary);
    const std::vector<char> font{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    CHECK(!font.empty());
    for (int cycle = 0; cycle < 8; ++cycle) {
#ifdef _WIN32
        const auto module = LoadLibraryA(argv[1]);
        CHECK(module);
        auto find = [&](const char* name) { return GetProcAddress(module, name); };
#else
        void* module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!module) std::cerr << dlerror() << '\n';
        CHECK(module);
        auto find = [&](const char* name) { return dlsym(module, name); };
#endif
#define LOAD(name) const auto name = reinterpret_cast<decltype(&::name)>(find(#name)); CHECK(name)
        LOAD(xl_runtime_create); LOAD(xl_runtime_destroy); LOAD(xl_runtime_resource_stats);
        LOAD(xl_document_create); LOAD(xl_document_destroy); LOAD(xl_document_load_html);
        LOAD(xl_document_add_font); LOAD(xl_document_eval);
        LOAD(xl_view_options_init); LOAD(xl_view_create); LOAD(xl_view_destroy);
        LOAD(xl_view_set_document); LOAD(xl_view_render); LOAD(xl_view_copy_rgba);
        LOAD(xl_view_set_graphics_backend); LOAD(xl_view_get_graphics_info);
        LOAD(xl_runtime_request_resource); LOAD(xl_resource_request_poll);
        LOAD(xl_resource_request_release); LOAD(xl_resource_lease_release); LOAD(xl_resource_lease_data);
#undef LOAD
        xl_runtime* runtime = nullptr;
        CHECK(xl_runtime_create(nullptr, &runtime) == XL_OK);
        xl_document* document = nullptr;
        CHECK(xl_document_create(runtime, &document) == XL_OK);
        CHECK(xl_document_add_font(document, "unload test", font.data(), font.size()) == XL_OK);
        const char html[] = "<style>body {margin:0}p{margin:0;height:32px;background:linear-gradient(to right,red,blue)}svg{display:block}</style><p id='text'>office e\xcc\x81</p>"
            "<svg width='100' height='28'><rect width='100' height='28' fill='lime'/><text x='2' y='22' font-size='20' font-family='unload test' fill='red'>SVG</text></svg><canvas id='canvas' width='100' height='24' style='display:block'></canvas>";
        CHECK(xl_document_load_html(document, html, sizeof(html)-1) == XL_OK);
        const char script[] = "document.getElementById('text').setAttribute('aria-label','Unload test');var cx=document.getElementById('canvas').getContext('2d');cx.fillStyle='blue';cx.fillRect(0,0,100,24);cx.font='18px \"unload test\"';cx.fillStyle='red';cx.fillText('Canvas',0,20)";
        CHECK(xl_document_eval(document, script, sizeof(script)-1, 100) == XL_OK);
        xl_view_options options;
        CHECK(xl_view_options_init(&options, sizeof(options)) == XL_OK);
        options.kind = XL_VIEW_OFFSCREEN; options.width = 128; options.height = 64;
        xl_view* view = nullptr;
        CHECK(xl_view_create(runtime, &options, &view) == XL_OK);
        if (argc == 3) CHECK(xl_view_set_graphics_backend(view, XL_GRAPHICS_DAWN_SWIFTSHADER) == XL_OK);
        CHECK(xl_view_set_document(view, document) == XL_OK);
        uint32_t flags;
        xl_status status;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while ((status = xl_view_render(view, &flags)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        CHECK(status == XL_OK && flags == XL_FRAME_REDRAWN);
        if (argc == 3) {
            xl_graphics_info info{};
            CHECK(xl_view_get_graphics_info(view, &info, sizeof(info)) == XL_OK);
            CHECK(info.active == XL_GRAPHICS_DAWN_SWIFTSHADER);
        }
        std::vector<unsigned char> pixels(128 * 64 * 4);
        CHECK(xl_view_copy_rgba(view, pixels.data(), pixels.size(), 128 * 4) == XL_OK);
        size_t ink = 0;
        for (size_t index = 0; index < pixels.size(); index += 4) if (pixels[index] < 240) ++ink;
        CHECK(ink > 20);
        CHECK(pixels[(25u*128+4)*4] > pixels[(25u*128+4)*4+2]);
        CHECK(pixels[(25u*128+120)*4+2] > pixels[(25u*128+120)*4]);
        CHECK(pixels[(50u*128+90)*4]==0&&pixels[(50u*128+90)*4+1]==255);
        CHECK(pixels[(62u*128+90)*4]==0&&pixels[(62u*128+90)*4+2]==255);
        xl_resource_request* request = nullptr;
        CHECK(xl_runtime_request_resource(runtime, XL_RESOURCE_ICU_DATA, &request) == XL_OK);
        xl_resource_lease* lease = nullptr;
        while ((status = xl_resource_request_poll(request, &lease)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < deadline);
            std::this_thread::yield();
        }
        CHECK(status == XL_OK);
        xl_resource_request_release(request);
        CHECK(xl_runtime_destroy(runtime) == XL_BUSY);
        CHECK(xl_document_destroy(document) == XL_BUSY);
        CHECK(xl_view_destroy(view) == XL_OK);
        CHECK(xl_document_destroy(document) == XL_OK);
        CHECK(xl_runtime_destroy(runtime) == XL_OK);
        const void* bytes; size_t size;
        CHECK(xl_resource_lease_data(lease, &bytes, &size) == XL_OK && bytes && (size > 1024 * 1024 && size < 16 * 1024 * 1024));
        xl_resource_lease_release(lease);
#ifdef _WIN32
        CHECK(FreeLibrary(module));
        CHECK(GetModuleHandleA("XLWebEngineUI.dll") == nullptr);
#else
        CHECK(dlclose(module) == 0);
        if (cycle == 0) {
            std::ifstream maps("/proc/self/maps");
            std::string line;
            bool retained = false;
            while (std::getline(maps, line)) if (line.find(argv[1]) != std::string::npos) retained = true;
            std::cout << "After dlclose, mapping retained by libc loader: " << (retained ? "yes" : "no") << '\n';
        }
#endif
    }
    std::cout << "8 explicit dynamic load/use/close cycles passed; all handles released before close\n";
}
