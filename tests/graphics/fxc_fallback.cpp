#include <XLWebEngineUI/xl.hpp>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vector>
#include <cstdio>
int main(int argc,char** argv) {
    xl::Runtime runtime;
    xl_view_options options;
    xl_view_options_init(&options,sizeof(options));
    options.kind=XL_VIEW_OFFSCREEN; options.width=32; options.height=32;
    xl::View view(runtime,&options);
    const xl_paint_rect rect{5,6,20,12,0xff00ff00,0}; view.rects({&rect,1});
    const bool local=argc>1&&std::string_view(argv[1])=="--local";
    const bool modern=argc>1&&!local;
    const auto requested=modern?XL_GRAPHICS_D3D12:XL_GRAPHICS_ANGLE_D3D11;
    view.graphics_backend(requested);
    if(modern && view.graphics_info().shader_compiler!=XL_SHADER_COMPILER_DXC) {
        std::puts("Static DXC rendering requires a Shader Model 6 capable D3D12 device");
        return 77;
    }
    const bool created_d3d=view.graphics_info().active==requested;
    view.render();
    std::vector<unsigned char> pixels(32*32*4);
    if(xl_view_copy_rgba(view.get(),pixels.data(),pixels.size(),32*4)!=XL_OK)return 1;
    const auto info=view.graphics_info();
    if(modern) {
        if(info.active!=XL_GRAPHICS_D3D12 || info.failed_mask || info.shader_compiler!=XL_SHADER_COMPILER_DXC)return 2;
    } else if(local) {
        if(info.active!=requested||info.failed_mask||info.shader_compiler!=XL_SHADER_COMPILER_FXC)return 2;
        wchar_t loaded[32768]{},executable[32768]{};
        if(!GetModuleFileNameW(GetModuleHandleW(L"d3dcompiler_47.dll"),loaded,32768)||!GetModuleFileNameW(nullptr,executable,32768))return 5;
        auto* slash=wcsrchr(executable,L'\\');if(!slash)return 5;
        if(wcscpy_s(slash+1,32768-size_t(slash+1-executable),L"d3dcompiler_47.dll")||_wcsicmp(executable,loaded))return 5;
    } else if(info.active==requested || !(info.failed_mask&(1u<<requested)))return 2;
    for(size_t i=0;i<pixels.size();i+=4) {
        const auto x=(i/4)%32,y=(i/4)/32;
        const auto white=x<5 || x>=25 || y<6 || y>=18;
        if(pixels[i]!=(white?255:0) || pixels[i+1]!=255 || pixels[i+2]!=(white?255:0) || pixels[i+3]!=255)return 3;
    }
    if(modern) {
        // This process rendered a shader while local FXC always fails. Static
        // DXC must satisfy the modern path without loading either compiler DLL.
        const auto module=GetModuleHandleW(L"d3dcompiler_47.dll");
        const auto calls=module?reinterpret_cast<LONG(*)()>(GetProcAddress(module,"xl_test_fxc_failed_compiles")):nullptr;
        if((calls && calls()) || GetModuleHandleW(L"dxcompiler.dll") || GetModuleHandleW(L"dxil.dll"))return 4;
    }
    std::printf("Compiler path %s: active=%u failed=%u recoveries=%llu exercised=%u\n",local?"application-local FXC":modern?"static DXC":"FXC fallback",
        info.active,info.failed_mask,static_cast<unsigned long long>(info.recoveries),created_d3d);
}
