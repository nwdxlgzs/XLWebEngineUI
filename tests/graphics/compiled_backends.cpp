#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/graphite/vk/VulkanGraphiteContext.h"
#ifdef _WIN32
#include "include/gpu/ganesh/d3d/GrD3DDirectContext.h"
#endif
#include <cstdio>

int main() {
    // Volatile addresses keep actual factory relocations in the linked image.
    // This verifies closure completeness, without pretending to create a GPU.
    sk_sp<GrDirectContext> (*volatile gl)(sk_sp<const GrGLInterface>) = &GrDirectContexts::MakeGL;
    sk_sp<GrDirectContext> (*volatile vk)(const skgpu::VulkanBackendContext&) = &GrDirectContexts::MakeVulkan;
    auto* volatile graphite = &skgpu::graphite::ContextFactory::MakeVulkan;
    if (!gl || !vk || !graphite) return 1;
#ifdef _WIN32
    sk_sp<GrDirectContext> (*volatile d3d)(const GrD3DBackendContext&) = &GrDirectContexts::MakeD3D;
    if (!d3d) return 2;
    std::puts("Ganesh D3D12 factory statically linked");
#endif
    std::puts("Ganesh OpenGL/Vulkan and Graphite Vulkan factories statically linked; device execution not tested");
}
