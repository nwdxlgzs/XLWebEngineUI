#pragma once
#include <memory>
#include <XLWebEngineUI/xl.h>
#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ShaderErrorHandler.h"
#include <atomic>
class GrDirectContext;
struct GrContextOptions;
namespace xl::detail {
class GpuDevice {
public:
    GpuDevice();
    virtual ~GpuDevice();
    virtual bool current() = 0;
    virtual void release() {}
    virtual bool finish();
    virtual uint32_t shader_compiler() const { return XL_SHADER_COMPILER_NONE; }
    virtual void abandon();
    virtual sk_sp<SkSurface> surface(int width, int height);
    virtual bool read_pixels(SkSurface*, const SkImageInfo&, void*, size_t row_bytes, int x, int y);
protected:
    struct Errors final : skgpu::ShaderErrorHandler {
        std::atomic_bool failed{false};
        void compileError(const char*, const char*) override;
    } errors_;
    GrContextOptions options();
    sk_sp<GrDirectContext> context_;
};
std::unique_ptr<GpuDevice> make_gpu_device(uint32_t backend);
std::unique_ptr<GpuDevice> make_native_gl();
std::unique_ptr<GpuDevice> make_dawn_device(bool software);
}
