#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <unknwn.h>
#define DXC_API_IMPORT
#define DxcCreateInstance xl_dxcompiler_DxcCreateInstance
#define DxcCreateInstance2 xl_dxcompiler_DxcCreateInstance2
#include <dxc/dxcapi.h>
#include <wrl/client.h>
#include <cstring>
#include <cstdio>
#include <iterator>
#include <thread>
#include <atomic>
using Microsoft::WRL::ComPtr;
int compile(bool valid){
 ComPtr<IDxcCompiler3> compiler;
 auto hr=DxcCreateInstance(CLSID_DxcCompiler,IID_PPV_ARGS(&compiler));
 if(FAILED(hr)){std::printf("create=%lx\n",hr);return 1;}
 const char* source=valid?"float4 main() : SV_Target { return float4(0, 1, 0, 1); }":
                         "float4 main() : SV_Target { return missing_value; }";
 DxcBuffer input{source,std::strlen(source),DXC_CP_UTF8};
 const wchar_t* args[]{L"-E",L"main",L"-T",L"ps_6_0",L"-O1"};
 ComPtr<IDxcResult> result;hr=compiler->Compile(&input,args,UINT32(std::size(args)),nullptr,IID_PPV_ARGS(&result));
 if(FAILED(hr)){std::printf("compile=%lx\n",hr);return 2;}
 ComPtr<IDxcBlobUtf8> errors;result->GetOutput(DXC_OUT_ERRORS,IID_PPV_ARGS(&errors),nullptr);
 if(errors&&errors->GetStringLength())std::printf("%s",errors->GetStringPointer());
 HRESULT status;result->GetStatus(&status);
 if(!valid)return FAILED(status)&&errors&&errors->GetStringLength()?0:5;
 if(FAILED(status))return 3;
 ComPtr<IDxcBlob> object;hr=result->GetOutput(DXC_OUT_OBJECT,IID_PPV_ARGS(&object),nullptr);
 if(FAILED(hr)||!object||object->GetBufferSize()<4||std::memcmp(object->GetBufferPointer(),"DXBC",4))return 4;
 std::printf("Static DXC HLSL ps_6_0 object=%zu bytes\n",object->GetBufferSize());
 return 0;
}
int main(){
 std::atomic_int failed{0};
 // Race the first static session initialization across independent compiler objects.
 auto worker=[&]{for(int i=0;i<2;++i)if(auto error=compile(i==0))failed.store(error);};
 std::thread a(worker),b(worker);a.join();b.join();
 if(auto error=failed.load())return error;
 if(GetModuleHandleW(L"dxcompiler.dll") || GetModuleHandleW(L"dxil.dll"))return 6;
 std::puts("Static DXC concurrent initialization, DXIL objects and invalid HLSL diagnostics passed; no DXC/DXIL DLL loaded");
}
