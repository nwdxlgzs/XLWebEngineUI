#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3dcompiler.h>
namespace xl::detail {
HRESULT WINAPI compile_dxc(const void* source, SIZE_T length, const char* source_name,
    const D3D_SHADER_MACRO* defines, ID3DInclude* includes, const char* entry, const char* target,
    UINT flags, UINT effect_flags, ID3DBlob** code, ID3DBlob** errors) noexcept;
}
#endif
