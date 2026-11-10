#include "dxc_compiler.h"
#if defined(_WIN32) && defined(XL_STATIC_GRAPHICS)
#define DXC_API_IMPORT
#define DxcCreateInstance xl_dxcompiler_DxcCreateInstance
#define DxcCreateInstance2 xl_dxcompiler_DxcCreateInstance2
#include <dxc/dxcapi.h>
#include "include/gpu/ganesh/d3d/GrD3DTypes.h"
#include <string>
#include <string_view>
#include <vector>
#include <new>

namespace xl::detail {
HRESULT WINAPI compile_dxc(const void* source, SIZE_T length, const char*,
    const D3D_SHADER_MACRO* defines, ID3DInclude* includes, const char* entry, const char* target,
    UINT flags, UINT effect_flags, ID3DBlob** code, ID3DBlob** errors) noexcept {
    if(code)*code=nullptr;
    if(errors)*errors=nullptr;
    if(!code || !source || !length || !entry || !target || defines || includes || effect_flags)
        return E_INVALIDARG;
    try {
        // Skia supplies generated HLSL without file includes or macro arguments.
        // Select SM6 only after the D3D device has advertised support for it.
        const std::string_view profile(target);
        const wchar_t* shader_model=profile=="vs_5_1"?L"vs_6_0":profile=="ps_5_1"?L"ps_6_0":
                                    profile=="cs_5_1"?L"cs_6_0":nullptr;
        if(!shader_model)return E_INVALIDARG;
        std::wstring wide_entry;
        for(const unsigned char* p=reinterpret_cast<const unsigned char*>(entry);*p;++p) {
            if(*p>127 || wide_entry.size()>=128)return E_INVALIDARG;
            wide_entry.push_back(*p);
        }
        if(wide_entry.empty())return E_INVALIDARG;
        std::vector<const wchar_t*> args{L"-E",wide_entry.c_str(),L"-T",shader_model,L"-HV",L"2016"};
        args.push_back(flags&D3DCOMPILE_SKIP_OPTIMIZATION?L"-Od":L"-O3");
        if(flags&D3DCOMPILE_DEBUG)args.push_back(L"-Zi");
        if(flags&D3DCOMPILE_IEEE_STRICTNESS)args.push_back(L"-Gis");
        if(flags&D3DCOMPILE_PACK_MATRIX_ROW_MAJOR)args.push_back(L"-Zpr");
        if(flags&D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR)args.push_back(L"-Zpc");
        gr_cp<IDxcCompiler3> compiler;
        auto status=DxcCreateInstance(CLSID_DxcCompiler,IID_PPV_ARGS(&compiler));
        if(FAILED(status))return status;
        const DxcBuffer input{source,length,DXC_CP_UTF8};
        gr_cp<IDxcResult> result;
        status=compiler->Compile(&input,args.data(),UINT32(args.size()),nullptr,IID_PPV_ARGS(&result));
        if(FAILED(status))return status;
        if(errors) {
            gr_cp<IDxcBlobUtf8> diagnostics;
            if(SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS,IID_PPV_ARGS(&diagnostics),nullptr)) && diagnostics)
                diagnostics->QueryInterface(IID_PPV_ARGS(errors));
        }
        HRESULT compilation;
        status=result->GetStatus(&compilation);
        if(FAILED(status))return status;
        if(FAILED(compilation))return compilation;
        gr_cp<IDxcBlob> object;
        status=result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&object),nullptr);
        if(FAILED(status))return status;
        // IDxcBlob and ID3DBlob have the same COM identity and buffer contract.
        return object?object->QueryInterface(IID_PPV_ARGS(code)):E_FAIL;
    } catch(const std::bad_alloc&) { return E_OUTOFMEMORY; }
      catch(...) { return E_FAIL; }
}
}
#endif
