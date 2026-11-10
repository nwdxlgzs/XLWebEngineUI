#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static LONG calls;
extern "C" __declspec(dllexport) LONG xl_test_fxc_failed_compiles() { return calls; }
extern "C" __declspec(dllexport) HRESULT WINAPI D3DCompile(
    const void*, SIZE_T, const char*, const void*, void*, const char*, const char*, UINT, UINT,
    void** code, void** errors) {
    InterlockedIncrement(&calls);
    if (code) *code = nullptr;
    if (errors) *errors = nullptr;
    return E_NOINTERFACE;
}
