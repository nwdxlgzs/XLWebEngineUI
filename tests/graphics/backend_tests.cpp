#include <XLWebEngineUI/xl.hpp>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <thread>
#include <barrier>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif
#define CHECK(value) do{if(!(value)){std::cerr<<__LINE__<<": " #value "\n";std::exit(1);}}while(false)
xl_graphics_info info(xl::View& view){xl_graphics_info value{};CHECK(xl_view_get_graphics_info(view.get(),&value,sizeof(value))==XL_OK);return value;}
int main(){
    std::cout<<std::unitbuf;
#ifdef _WIN32
    // A View embedded in an application must return its host's WGL context.
    auto window=CreateWindowExW(0,L"STATIC",L"XL host context",WS_POPUP,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    CHECK(window);auto dc=GetDC(window);CHECK(dc);
    PIXELFORMATDESCRIPTOR format{};format.nSize=sizeof(format);format.nVersion=1;
    format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER;format.iPixelType=PFD_TYPE_RGBA;format.cColorBits=32;
    auto selected=ChoosePixelFormat(dc,&format);CHECK(selected && SetPixelFormat(dc,selected,&format));
    auto host=wglCreateContext(dc);CHECK(host && wglMakeCurrent(dc,host));
#endif
    xl::Runtime runtime;
    xl_view_options options;CHECK(xl_view_options_init(&options,sizeof(options))==XL_OK);
    options.kind=XL_VIEW_OFFSCREEN;options.width=64;options.height=48;
    xl::View view(runtime,&options),other(runtime,&options);
    const xl_paint_rect rect{5,6,20,12,0xff00ff00,0};view.rects({&rect,1});other.rects({&rect,1});
    CHECK(info(view).active==XL_GRAPHICS_RASTER);
    const auto pixels=[&](xl::View& target){
        uint32_t flags;CHECK(xl_view_render(target.get(),&flags)==XL_OK);
        const auto stats=target.stats();std::vector<unsigned char> value(size_t(stats.pixel_width)*stats.pixel_height*4);
        CHECK(xl_view_copy_rgba(target.get(),value.data(),value.size(),size_t(stats.pixel_width)*4)==XL_OK);
        CHECK(target.render()==0);return value;
    };
    const auto reference=pixels(view);
    for(const uint32_t backend:{XL_GRAPHICS_OPENGL,XL_GRAPHICS_SWIFTSHADER,XL_GRAPHICS_ANGLE_D3D11,XL_GRAPHICS_VULKAN,XL_GRAPHICS_ANGLE_OPENGL,XL_GRAPHICS_D3D12,XL_GRAPHICS_AUTO}){
        CHECK(xl_view_set_graphics_backend(view.get(),backend)==XL_OK);
        const auto first=info(view);CHECK(first.requested==backend);
        CHECK(pixels(view)==reference);
        const auto actual=info(view);std::cout<<"requested="<<backend<<" active="<<actual.active
            <<" shader_compiler="<<actual.shader_compiler<<" attempts="<<actual.attempted_mask<<" failed="<<actual.failed_mask<<'\n';
#ifdef XL_TEST_STATIC_GRAPHICS
        if(backend==XL_GRAPHICS_SWIFTSHADER)CHECK(actual.active==backend);
#endif
#ifdef _WIN32
        CHECK(wglGetCurrentContext()==host && wglGetCurrentDC()==dc);
        if(backend==XL_GRAPHICS_SWIFTSHADER || backend==XL_GRAPHICS_ANGLE_D3D11 || backend==XL_GRAPHICS_OPENGL)CHECK(actual.active==backend);
        if(actual.active==XL_GRAPHICS_ANGLE_D3D11)CHECK(actual.shader_compiler==XL_SHADER_COMPILER_FXC);
#endif
        CHECK(xl_view_set_graphics_backend(other.get(),backend)==XL_OK);
        CHECK(pixels(other)==reference);
        if(backend==XL_GRAPHICS_AUTO && actual.active!=XL_GRAPHICS_RASTER) {
            view.abandon_graphics_backend();CHECK(pixels(view)==reference);
            const auto recovered=view.graphics_info();
            CHECK(recovered.active!=actual.active && recovered.recoveries==actual.recoveries+1);
            CHECK(recovered.failed_mask&(1u<<actual.active));
        }
        CHECK(xl_view_set_graphics_backend(view.get(),XL_GRAPHICS_RASTER)==XL_OK);
        // Releasing one View must not terminate the second View's shared EGL display.
        CHECK(pixels(other)==reference);
        CHECK(xl_view_resize(other.get(),70,50)==XL_OK);pixels(other);
        CHECK(xl_view_resize(other.get(),64,48)==XL_OK);CHECK(pixels(other)==reference);
        CHECK(xl_view_set_graphics_backend(other.get(),XL_GRAPHICS_RASTER)==XL_OK);
    }
#if defined(_WIN32) || defined(XL_TEST_STATIC_GRAPHICS)
    std::barrier rendezvous(2);
#ifdef _WIN32
    constexpr auto threaded_backend=XL_GRAPHICS_ANGLE_D3D11;
#else
    constexpr auto threaded_backend=XL_GRAPHICS_SWIFTSHADER;
#endif
    const auto concurrent=[&] {
        xl::Runtime threaded_runtime;xl::View threaded_view(threaded_runtime,&options);
        threaded_view.rects({&rect,1});
        threaded_view.graphics_backend(threaded_backend);
        const auto actual_pixels=pixels(threaded_view);
        if(actual_pixels!=reference) {
            size_t changed=0,first=0;
            for(size_t i=0;i<reference.size();++i)if(actual_pixels[i]!=reference[i]){if(!changed)first=i;++changed;}
            std::cerr<<"threaded GPU mismatch bytes="<<changed<<" first="<<first
                     <<" actual="<<unsigned(actual_pixels[first])<<" expected="<<unsigned(reference[first])
                     <<" active="<<threaded_view.graphics_info().active<<'\n';
            CHECK(actual_pixels==reference);
        }
        CHECK(threaded_view.graphics_info().active==threaded_backend);
        rendezvous.arrive_and_wait();
        CHECK(xl_view_resize(threaded_view.get(),70,50)==XL_OK);pixels(threaded_view);
    };
    std::thread first_thread(concurrent),second_thread(concurrent);
    first_thread.join();second_thread.join();
#endif
    CHECK(xl_view_set_graphics_backend(view.get(),XL_GRAPHICS_SWIFTSHADER)==XL_OK);CHECK(pixels(view)==reference);
    const auto before=info(view);CHECK(xl_view_abandon_graphics_backend(view.get())==XL_OK);CHECK(pixels(view)==reference);
    CHECK(info(view).active==XL_GRAPHICS_RASTER);
#if defined(_WIN32) || defined(XL_TEST_STATIC_GRAPHICS)
    CHECK(info(view).recoveries==before.recoveries+1 && (info(view).failed_mask&(1u<<XL_GRAPHICS_SWIFTSHADER)));
#else
    (void)before;
#endif
    CHECK(xl_view_set_graphics_backend(view.get(),99)==XL_INVALID_ARGUMENT);
    CHECK(xl_view_get_graphics_info(view.get(),nullptr,sizeof(xl_graphics_info))==XL_INVALID_ARGUMENT);
    xl_graphics_info result;CHECK(xl_view_get_graphics_info(view.get(),&result,1)==XL_ABI_MISMATCH);
#ifdef _WIN32
    CHECK(wglGetCurrentContext()==host);
    CHECK(wglMakeCurrent(nullptr,nullptr));CHECK(wglDeleteContext(host));
    ReleaseDC(window,dc);DestroyWindow(window);
#endif
}
