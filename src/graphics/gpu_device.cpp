#include "gpu_device.h"
#include "dxc_compiler.h"
#include "include/core/SkImageInfo.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrContextOptions.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "src/gpu/GpuTypesPriv.h"
#ifdef XL_STATIC_GRAPHICS
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"
#include "src/gpu/vk/vulkanmemoryallocator/VulkanMemoryAllocatorPriv.h"
#define KHRONOS_STATIC 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <algorithm>
#include <vector>
#include <optional>
#include <mutex>
#include <unordered_map>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "include/gpu/ganesh/d3d/GrD3DBackendContext.h"
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
#else
#include <dlfcn.h>
#endif
extern "C" __eglMustCastToProperFunctionPointerType EGLAPIENTRY xl_angle_eglGetProcAddress(const char*);
extern "C" PFN_vkVoidFunction VKAPI_CALL xl_swiftshader_vkGetInstanceProcAddr(VkInstance, const char*);
#endif

namespace xl::detail {
GpuDevice::GpuDevice() = default;
GpuDevice::~GpuDevice() = default;
void GpuDevice::Errors::compileError(const char*,const char*) {
    failed.store(true,std::memory_order_relaxed);
}
GrContextOptions GpuDevice::options() {
    GrContextOptions value;value.fShaderErrorHandler=&errors_;return value;
}
sk_sp<SkSurface> GpuDevice::surface(int width, int height) {
    if (!current() || !context_ || context_->abandoned()) return nullptr;
    return SkSurfaces::RenderTarget(context_.get(), skgpu::Budgeted::kYes,
        SkImageInfo::MakeN32Premul(width, height), 0, kTopLeft_GrSurfaceOrigin, nullptr);
}
bool GpuDevice::finish() {
    return current() && context_ && !context_->abandoned() && context_->flushAndSubmit(GrSyncCpu::kYes).fSuccess &&
        !errors_.failed.load(std::memory_order_relaxed) && !context_->isDeviceLost();
}
void GpuDevice::abandon() {if(context_)context_->abandonContext();}
bool GpuDevice::read_pixels(SkSurface* surface, const SkImageInfo& info, void* data,
                            size_t row_bytes, int x, int y) {
    return surface->readPixels(info, data, row_bytes, x, y);
}
#ifdef XL_STATIC_GRAPHICS
namespace {
#ifdef _WIN32
HMODULE system_module(const wchar_t* name) {
    wchar_t path[32768]{};const auto length=GetSystemDirectoryW(path,32768);
    const auto suffix=wcslen(name);
    if(!length || length+suffix+2>=32768)return nullptr;
    path[length]=L'\\';std::copy(name,name+suffix+1,path+length+1);
    return LoadLibraryW(path);
}
class D3DDevice final : public GpuDevice {
    HMODULE d3d_=nullptr,dxgi_=nullptr;
    GrD3DBackendContext backend_;
public:
    ~D3DDevice() override {
        context_.reset();backend_.fMemoryAllocator.reset();backend_.fQueue.reset();backend_.fDevice.reset();backend_.fAdapter.reset();
        if(d3d_)FreeLibrary(d3d_);if(dxgi_)FreeLibrary(dxgi_);
    }
    bool current() override{return bool(backend_.fDevice);}
    uint32_t shader_compiler() const override {
        return backend_.fShaderCompiler?XL_SHADER_COMPILER_DXC:XL_SHADER_COMPILER_FXC;
    }
    bool initialize() {
        d3d_=system_module(L"d3d12.dll");dxgi_=system_module(L"dxgi.dll");
        if(!d3d_||!dxgi_)return false;
        const auto factory=reinterpret_cast<HRESULT(WINAPI*)(REFIID,void**)>(GetProcAddress(dxgi_,"CreateDXGIFactory1"));
        const auto create=reinterpret_cast<PFN_D3D12_CREATE_DEVICE>(GetProcAddress(d3d_,"D3D12CreateDevice"));
        if(!factory||!create)return false;
        gr_cp<IDXGIFactory1> enumeration;
        if(FAILED(factory(__uuidof(IDXGIFactory1),reinterpret_cast<void**>(&enumeration))))return false;
        for(UINT index=0;;++index) {
            gr_cp<IDXGIAdapter1> adapter;
            if(enumeration->EnumAdapters1(index,&adapter)==DXGI_ERROR_NOT_FOUND)break;
            if(!adapter)continue;
            DXGI_ADAPTER_DESC1 description{};if(FAILED(adapter->GetDesc1(&description))||(description.Flags&DXGI_ADAPTER_FLAG_SOFTWARE))continue;
            gr_cp<ID3D12Device> device;
            if(FAILED(create(adapter.get(),D3D_FEATURE_LEVEL_11_0,__uuidof(ID3D12Device),reinterpret_cast<void**>(&device))))continue;
            D3D12_COMMAND_QUEUE_DESC desc{};desc.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
            gr_cp<ID3D12CommandQueue> queue;
            if(FAILED(device->CreateCommandQueue(&desc,__uuidof(ID3D12CommandQueue),reinterpret_cast<void**>(&queue))))continue;
            backend_.fAdapter=std::move(adapter);backend_.fDevice=std::move(device);backend_.fQueue=std::move(queue);
            D3D12_FEATURE_DATA_SHADER_MODEL shader_model{D3D_SHADER_MODEL_6_0};
            backend_.fShaderCompiler=SUCCEEDED(backend_.fDevice->CheckFeatureSupport(
                D3D12_FEATURE_SHADER_MODEL,&shader_model,sizeof(shader_model))) &&
                shader_model.HighestShaderModel>=D3D_SHADER_MODEL_6_0?compile_dxc:nullptr;
            context_=GrDirectContexts::MakeD3D(backend_,options());
            if(context_)return true;
        }
        return false;
    }
};
#endif
struct AngleDisplay {
    EGLDisplay display;
    PFNEGLTERMINATEPROC terminate;
    std::recursive_mutex usage;
};
std::mutex display_mutex;
std::unordered_map<EGLDisplay,std::shared_ptr<AngleDisplay>> displays;
class AngleDevice final : public GpuDevice {
    PFNEGLGETPLATFORMDISPLAYEXTPROC platform_ = nullptr;
    PFNEGLINITIALIZEPROC initialize_ = nullptr;
    PFNEGLCHOOSECONFIGPROC choose_ = nullptr;
    PFNEGLCREATEPBUFFERSURFACEPROC pbuffer_ = nullptr;
    PFNEGLCREATECONTEXTPROC create_ = nullptr;
    PFNEGLMAKECURRENTPROC make_current_ = nullptr;
    PFNEGLDESTROYCONTEXTPROC destroy_ = nullptr;
    PFNEGLDESTROYSURFACEPROC destroy_surface_ = nullptr;
    PFNEGLTERMINATEPROC terminate_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface_ = EGL_NO_SURFACE;
    std::shared_ptr<AngleDisplay> display_owner_;
    template<class T> static T proc(const char* name) { return reinterpret_cast<T>(xl_angle_eglGetProcAddress(name)); }
    struct Restore {
        PFNEGLMAKECURRENTPROC make;
        EGLDisplay display;
        EGLSurface draw, read;
        EGLContext context;
#ifdef _WIN32
        HDC native_dc = wglGetCurrentDC();
        HGLRC native_context = wglGetCurrentContext();
#endif
        Restore() : make(proc<PFNEGLMAKECURRENTPROC>("eglMakeCurrent")),
            display(proc<PFNEGLGETCURRENTDISPLAYPROC>("eglGetCurrentDisplay")()),
            draw(proc<PFNEGLGETCURRENTSURFACEPROC>("eglGetCurrentSurface")(EGL_DRAW)),
            read(proc<PFNEGLGETCURRENTSURFACEPROC>("eglGetCurrentSurface")(EGL_READ)),
            context(proc<PFNEGLGETCURRENTCONTEXTPROC>("eglGetCurrentContext")()) {}
        ~Restore() {
            if (display != EGL_NO_DISPLAY) make(display, draw, read, context);
#ifdef _WIN32
            else wglMakeCurrent(native_dc, native_context);
#endif
        }
    };
    std::optional<Restore> previous_;
    std::unique_lock<std::recursive_mutex> usage_;
    uint32_t compiler_=XL_SHADER_COMPILER_NONE;
public:
    uint32_t shader_compiler() const override { return compiler_; }
    ~AngleDevice() override {
        release();
        Restore restore;
        if (display_ != EGL_NO_DISPLAY) {
            {
            std::unique_lock<std::recursive_mutex> usage;
            if(display_owner_)usage=std::unique_lock(display_owner_->usage);
            if (make_current_) make_current_(display_, surface_, surface_, context);
            context_.reset();
            if (make_current_) make_current_(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (context != EGL_NO_CONTEXT && destroy_) destroy_(display_, context);
            if (surface_ != EGL_NO_SURFACE && destroy_surface_) destroy_surface_(display_, surface_);
            }
            if(display_owner_) {
                std::lock_guard lock(display_mutex);
                const auto found=displays.find(display_);
                if(found!=displays.end() && display_owner_.use_count()==2) {
                    terminate_(display_);displays.erase(found);
                }
                display_owner_.reset();
            }
        }
    }
    bool initialize(uint32_t backend) {
        // Display initialization itself can make an internal WGL context current.
        Restore restore;
        compiler_=backend==XL_GRAPHICS_ANGLE_D3D11?XL_SHADER_COMPILER_FXC:XL_SHADER_COMPILER_NONE;
        platform_ = proc<PFNEGLGETPLATFORMDISPLAYEXTPROC>("eglGetPlatformDisplayEXT");
        initialize_ = proc<PFNEGLINITIALIZEPROC>("eglInitialize"); choose_ = proc<PFNEGLCHOOSECONFIGPROC>("eglChooseConfig");
        pbuffer_ = proc<PFNEGLCREATEPBUFFERSURFACEPROC>("eglCreatePbufferSurface"); create_ = proc<PFNEGLCREATECONTEXTPROC>("eglCreateContext");
        make_current_ = proc<PFNEGLMAKECURRENTPROC>("eglMakeCurrent"); destroy_ = proc<PFNEGLDESTROYCONTEXTPROC>("eglDestroyContext");
        destroy_surface_ = proc<PFNEGLDESTROYSURFACEPROC>("eglDestroySurface"); terminate_ = proc<PFNEGLTERMINATEPROC>("eglTerminate");
        if (!platform_ || !initialize_ || !choose_ || !pbuffer_ || !create_ || !make_current_ || !destroy_ || !destroy_surface_ || !terminate_) return false;
        const EGLint attrs[]{EGL_PLATFORM_ANGLE_TYPE_ANGLE,
            backend == XL_GRAPHICS_ANGLE_D3D11 ? EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE : EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE, EGL_NONE};
        display_ = platform_(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attrs);
        EGLint major, minor;
        if (display_ == EGL_NO_DISPLAY) return false;
        {
            std::lock_guard lock(display_mutex);
            const auto found=displays.find(display_);
            if(found!=displays.end())display_owner_=found->second;
            if(!display_owner_) {
                if(!initialize_(display_, &major, &minor))return false;
                display_owner_=std::make_shared<AngleDisplay>(display_,terminate_);
                displays[display_]=display_owner_;
            }
        }
        std::lock_guard usage(display_owner_->usage);
        const EGLint wanted[]{EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_NONE};
        EGLConfig config; EGLint count;
        if (!choose_(display_,wanted,&config,1,&count) || !count) return false;
        const EGLint size[]{EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
        surface_ = pbuffer_(display_,config,size);
        const EGLint version[]{EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
        context = create_(display_,config,EGL_NO_CONTEXT,version);
        if (surface_ == EGL_NO_SURFACE || context == EGL_NO_CONTEXT || !current()) return false;
        auto gl = GrGLMakeAssembledGLESInterface(nullptr, [](void*, const char* name) {
            return reinterpret_cast<GrGLFuncPtr>(xl_angle_eglGetProcAddress(name));
        });
        context_ = GrDirectContexts::MakeGL(std::move(gl),options());
        release(); return bool(context_);
    }
    bool current() override {
        if(display_==EGL_NO_DISPLAY || context==EGL_NO_CONTEXT)return false;
        if(!previous_) {
            // EGL contexts share a renderer/immediate context through the display.
            // Hold the display for the whole Skia operation, not individual GL calls.
            usage_=std::unique_lock(display_owner_->usage);
            previous_.emplace();
        }
        if(make_current_(display_,surface_,surface_,context))return true;
        release(); return false;
    }
    void release() override {
        if(!previous_)return;
        if(previous_->display==EGL_NO_DISPLAY)make_current_(display_,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        previous_.reset();
        usage_.unlock();
    }
};

class VulkanDevice final : public GpuDevice {
    void* module_ = nullptr;
    PFN_vkGetInstanceProcAddr instance_proc_ = nullptr;
    PFN_vkGetDeviceProcAddr device_proc_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroy_instance_ = nullptr;
    PFN_vkDestroyDevice destroy_device_ = nullptr;
    PFN_vkDeviceWaitIdle wait_ = nullptr;
public:
    ~VulkanDevice() override {
        context_.reset();
        if (device_ != VK_NULL_HANDLE) { if (wait_) wait_(device_); if (destroy_device_) destroy_device_(device_,nullptr); }
        if (instance_ != VK_NULL_HANDLE && destroy_instance_) destroy_instance_(instance_,nullptr);
        if (module_) {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(module_));
#else
            dlclose(module_);
#endif
        }
    }
    bool current() override { return device_ != VK_NULL_HANDLE; }
    bool initialize(uint32_t backend) {
        if (backend == XL_GRAPHICS_SWIFTSHADER) instance_proc_ = xl_swiftshader_vkGetInstanceProcAddr;
        else {
#ifdef _WIN32
            wchar_t path[32768]{};const auto length=GetSystemDirectoryW(path,32768);
            constexpr wchar_t suffix[]=L"\\vulkan-1.dll";
            if (!length || length+std::size(suffix)>=32768) return false;
            std::copy(std::begin(suffix),std::end(suffix),path+length);
            module_=LoadLibraryW(path);
            if(module_)instance_proc_=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(static_cast<HMODULE>(module_),"vkGetInstanceProcAddr"));
#else
            module_=dlopen("libvulkan.so.1",RTLD_NOW|RTLD_LOCAL);
            if(module_)instance_proc_=reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(module_,"vkGetInstanceProcAddr"));
#endif
        }
        if(!instance_proc_)return false;
        const auto create=reinterpret_cast<PFN_vkCreateInstance>(instance_proc_(VK_NULL_HANDLE,"vkCreateInstance"));
        if(!create)return false;
        VkApplicationInfo app{};app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;info.pApplicationInfo=&app;
        if(create(&info,nullptr,&instance_)!=VK_SUCCESS)return false;
        destroy_instance_=reinterpret_cast<PFN_vkDestroyInstance>(instance_proc_(instance_,"vkDestroyInstance"));
        const auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(instance_proc_(instance_,"vkEnumeratePhysicalDevices"));
        const auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(instance_proc_(instance_,"vkGetPhysicalDeviceProperties"));
        const auto queues=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(instance_proc_(instance_,"vkGetPhysicalDeviceQueueFamilyProperties"));
        const auto features=reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(instance_proc_(instance_,"vkGetPhysicalDeviceFeatures"));
        const auto create_device=reinterpret_cast<PFN_vkCreateDevice>(instance_proc_(instance_,"vkCreateDevice"));
        device_proc_=reinterpret_cast<PFN_vkGetDeviceProcAddr>(instance_proc_(instance_,"vkGetDeviceProcAddr"));
        if(!destroy_instance_||!enumerate||!properties||!queues||!features||!create_device||!device_proc_)return false;
        uint32_t count=0;if(enumerate(instance_,&count,nullptr)!=VK_SUCCESS||!count||count>64)return false;
        std::vector<VkPhysicalDevice> devices(count);if(enumerate(instance_,&count,devices.data())!=VK_SUCCESS)return false;
        for(auto physical:devices) {
            VkPhysicalDeviceProperties props{};properties(physical,&props);if(props.apiVersion<VK_API_VERSION_1_1)continue;
            uint32_t count=0;queues(physical,&count,nullptr);if(!count||count>256)continue;
            std::vector<VkQueueFamilyProperties> families(count);queues(physical,&count,families.data());
            for(uint32_t index=0;index<count;++index)if(families[index].queueCount && (families[index].queueFlags&VK_QUEUE_GRAPHICS_BIT)) {
                float priority=1;
                VkDeviceQueueCreateInfo queue{};queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;queue.queueFamilyIndex=index;queue.queueCount=1;queue.pQueuePriorities=&priority;
                VkPhysicalDeviceFeatures enabled{};features(physical,&enabled);
                VkDeviceCreateInfo device_info{};device_info.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;device_info.queueCreateInfoCount=1;device_info.pQueueCreateInfos=&queue;device_info.pEnabledFeatures=&enabled;
                if(create_device(physical,&device_info,nullptr,&device_)!=VK_SUCCESS)continue;
                destroy_device_=reinterpret_cast<PFN_vkDestroyDevice>(device_proc_(device_,"vkDestroyDevice"));
                wait_=reinterpret_cast<PFN_vkDeviceWaitIdle>(device_proc_(device_,"vkDeviceWaitIdle"));
                const auto get_queue=reinterpret_cast<PFN_vkGetDeviceQueue>(device_proc_(device_,"vkGetDeviceQueue"));
                if(!destroy_device_||!wait_||!get_queue)return false;
                skgpu::VulkanBackendContext backend_context;backend_context.fInstance=instance_;backend_context.fPhysicalDevice=physical;backend_context.fDevice=device_;backend_context.fGraphicsQueueIndex=index;
                get_queue(device_,index,0,&backend_context.fQueue);backend_context.fMaxAPIVersion=VK_API_VERSION_1_1;backend_context.fDeviceFeatures=&enabled;
                backend_context.fGetProc=[this](const char* name,VkInstance instance,VkDevice device){return device!=VK_NULL_HANDLE?device_proc_(device,name):instance_proc_(instance,name);};
                backend_context.fMemoryAllocator=skgpu::VulkanMemoryAllocators::Make(backend_context,skgpu::ThreadSafe::kYes);
                if(backend_context.fMemoryAllocator)context_=GrDirectContexts::MakeVulkan(backend_context,options());
                if(context_)return true;
                // VMA can call the device during destruction even if Skia rejected
                // this device. Release it before destroying the Vulkan handle.
                backend_context.fMemoryAllocator.reset();
                wait_(device_);destroy_device_(device_,nullptr);device_=VK_NULL_HANDLE;
            }
        }
        return false;
    }
};
}
#endif
std::unique_ptr<GpuDevice> make_gpu_device(uint32_t backend) {
#ifdef XL_DAWN_GRAPHICS
    if(backend==XL_GRAPHICS_DAWN || backend==XL_GRAPHICS_DAWN_SWIFTSHADER)
        return make_dawn_device(backend==XL_GRAPHICS_DAWN_SWIFTSHADER);
#endif
#ifdef XL_STATIC_GRAPHICS
    if(backend==XL_GRAPHICS_OPENGL)return make_native_gl();
#ifdef _WIN32
    if(backend==XL_GRAPHICS_D3D12) {
        auto result=std::make_unique<D3DDevice>();if(result->initialize())return result;
    }
#endif
    if(backend==XL_GRAPHICS_ANGLE_D3D11 || backend==XL_GRAPHICS_ANGLE_OPENGL) {
        auto result=std::make_unique<AngleDevice>();if(result->initialize(backend))return result;
    }
    if(backend==XL_GRAPHICS_VULKAN || backend==XL_GRAPHICS_SWIFTSHADER) {
        auto result=std::make_unique<VulkanDevice>();if(result->initialize(backend))return result;
    }
#else
    (void)backend;
#endif
    return nullptr;
}
}
