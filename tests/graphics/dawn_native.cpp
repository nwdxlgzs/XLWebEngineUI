#include <dawn/native/DawnNative.h>
#include <dawn/native/OpenGLBackend.h>
#include <webgpu/webgpu_cpp.h>
#define KHRONOS_STATIC 1
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/eglext_angle.h>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
extern "C" __eglMustCastToProperFunctionPointerType EGLAPIENTRY xl_angle_eglGetProcAddress(const char*);

namespace {
std::atomic_bool error{false};
int render(const char* backend) {
    dawn::native::Instance instance;
    wgpu::RequestAdapterOptions options;
    options.backendType = std::strcmp(backend, "d3d12") == 0 ? wgpu::BackendType::D3D12 :
                          std::strcmp(backend, "d3d11") == 0 ? wgpu::BackendType::D3D11 :
                          std::strcmp(backend, "angle-gl") == 0 ? wgpu::BackendType::OpenGLES : wgpu::BackendType::Vulkan;
    options.forceFallbackAdapter = std::strcmp(backend, "swiftshader") == 0;
    const char* dxc = "use_dxc";
    wgpu::DawnTogglesDescriptor toggles;
    if (options.backendType == wgpu::BackendType::D3D12) {
        toggles.enabledToggleCount = 1;
        toggles.enabledToggles = &dxc;
        options.nextInChain = &toggles;
    }
    dawn::native::opengl::RequestAdapterOptionsGetGLProc gl;
    if (options.backendType == wgpu::BackendType::OpenGLES) {
        const auto get_display = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(xl_angle_eglGetProcAddress("eglGetPlatformDisplayEXT"));
        if (!get_display) return 7;
        const EGLint attributes[]{EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_OPENGL_ANGLE, EGL_NONE};
        gl.getProc = xl_angle_eglGetProcAddress;
        gl.display = get_display(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attributes);
        if (!gl.display) return 7;
        options.nextInChain = &gl;
        options.featureLevel = wgpu::FeatureLevel::Compatibility;
    }
    auto adapters = instance.EnumerateAdapters(&options);
    if (std::strcmp(backend, "vulkan") == 0) {
        std::erase_if(adapters, [](const dawn::native::Adapter& candidate) {
            wgpu::AdapterInfo info;
            wgpu::Adapter(candidate.Get()).GetInfo(&info);
            return info.adapterType == wgpu::AdapterType::CPU;
        });
    }
    if (adapters.empty()) { std::printf("No Dawn %s adapter\n", backend); return 2; }
    wgpu::AdapterInfo adapter_info;
    wgpu::Adapter(adapters.front().Get()).GetInfo(&adapter_info);
    std::printf("Dawn adapter type=%u name=%.*s\n", unsigned(adapter_info.adapterType), int(adapter_info.device.length), adapter_info.device.data);
    wgpu::DeviceDescriptor device_desc;
    device_desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents,
        [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView) {
            if (reason != wgpu::DeviceLostReason::Destroyed) error = true;
        });
    device_desc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView message) {
        error = true;
        std::fprintf(stderr, "Dawn error: %.*s\n", int(message.length), message.data);
    });
    auto device = wgpu::Device::Acquire(adapters.front().CreateDevice(&device_desc));
    if (!device) return 3;
    wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = R"(
@vertex fn vs(@builtin(vertex_index) index: u32) -> @builtin(position) vec4f {
    let xy = array<vec2f, 3>(vec2f(-1, -1), vec2f(3, -1), vec2f(-1, 3));
    return vec4f(xy[index], 0, 1);
}
@fragment fn fs() -> @location(0) vec4f { return vec4f(0, 1, 0, 1); }
)";
    wgpu::ShaderModuleDescriptor shader_desc;
    shader_desc.nextInChain = &wgsl;
    auto module = device.CreateShaderModule(&shader_desc);
    wgpu::ColorTargetState target;
    target.format = wgpu::TextureFormat::RGBA8Unorm;
    wgpu::FragmentState fragment;
    fragment.module = module;
    fragment.entryPoint = "fs";
    fragment.targetCount = 1;
    fragment.targets = &target;
    wgpu::RenderPipelineDescriptor pipeline_desc;
    pipeline_desc.vertex.module = module;
    pipeline_desc.vertex.entryPoint = "vs";
    pipeline_desc.fragment = &fragment;
    pipeline_desc.primitive.topology = wgpu::PrimitiveTopology::TriangleList;
    auto pipeline = device.CreateRenderPipeline(&pipeline_desc);
    wgpu::TextureDescriptor texture_desc;
    texture_desc.size = {8, 8, 1};
    texture_desc.format = target.format;
    texture_desc.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
    auto texture = device.CreateTexture(&texture_desc);
    wgpu::RenderPassColorAttachment color;
    color.view = texture.CreateView();
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {1, 0, 0, 1};
    wgpu::RenderPassDescriptor pass_desc;
    pass_desc.colorAttachmentCount = 1;
    pass_desc.colorAttachments = &color;
    wgpu::BufferDescriptor buffer_desc;
    buffer_desc.size = 8 * 256;
    buffer_desc.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    auto readback = device.CreateBuffer(&buffer_desc);
    auto encoder = device.CreateCommandEncoder();
    auto pass = encoder.BeginRenderPass(&pass_desc);
    pass.SetPipeline(pipeline);
    pass.Draw(3);
    pass.End();
    wgpu::TexelCopyTextureInfo source;
    source.texture = texture;
    wgpu::TexelCopyBufferInfo destination;
    destination.buffer = readback;
    destination.layout.bytesPerRow = 256;
    const wgpu::Extent3D size{8, 8, 1};
    encoder.CopyTextureToBuffer(&source, &destination, &size);
    auto commands = encoder.Finish();
    device.GetQueue().Submit(1, &commands);
    struct Result { bool done = false, mapped = false; };
    const auto result = std::make_shared<Result>();
    readback.MapAsync(wgpu::MapMode::Read, 0, buffer_desc.size, wgpu::CallbackMode::AllowProcessEvents,
                     [result](wgpu::MapAsyncStatus status, wgpu::StringView) {
                         result->mapped = status == wgpu::MapAsyncStatus::Success;
                         result->done = true;
                     });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!result->done && std::chrono::steady_clock::now() < deadline) {
        dawn::native::InstanceProcessEvents(instance.Get());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!result->mapped || error.load()) return 4;
    const auto* bytes = static_cast<const unsigned char*>(readback.GetConstMappedRange());
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
        const auto* pixel = bytes + y * 256 + x * 4;
        if (pixel[0] != 0 || pixel[1] != 255 || pixel[2] != 0 || pixel[3] != 255) return 5;
    }
    readback.Unmap();
#ifdef _WIN32
    bool external = false;
    for (const wchar_t* name : {L"dxcompiler.dll", L"dxil.dll", L"vk_swiftshader.dll", L"libEGL.dll", L"libGLESv2.dll"})
        if (auto module = GetModuleHandleW(name)) {
            wchar_t path[32768]{};
            GetModuleFileNameW(module, path, 32768);
            if (std::strcmp(backend, "vulkan") == 0 &&
                (std::wcscmp(name, L"dxcompiler.dll") == 0 || std::wcscmp(name, L"dxil.dll") == 0)) {
                // Measured with a loader breakpoint: the installed vulkan_dzn
                // ICD loads its own compiler while the system loader enumerates
                // drivers. This is not a Dawn/SDK runtime library requirement.
                std::fwprintf(stderr, L"Observed system Vulkan driver compiler module: %ls\n", path);
            } else {
                std::fwprintf(stderr, L"Unexpected optional module: %ls\n", path);
                external = true;
            }
        }
    if (external) return 6;
#endif
    std::printf("Dawn %s: WGSL vertex/fragment pipeline, 64 exact green RGBA pixels; private static graphics\n", backend);
    return 0;
}
}
int main(int argc, char** argv) {
    std::atomic_uint destroyed{0};
    struct ThreadExit {
        std::atomic_uint* destroyed;
        ~ThreadExit() { ++*destroyed; }
    };
    const auto use_tls = [&] { thread_local ThreadExit value{&destroyed}; (void)value; };
    std::thread first(use_tls), second(use_tls);
    first.join(); second.join();
    if (destroyed != 2) return 8;
    return render(argc > 1 ? argv[1] : "swiftshader");
}
