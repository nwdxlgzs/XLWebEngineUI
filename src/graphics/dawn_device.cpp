#include "gpu_device.h"
#ifdef XL_DAWN_GRAPHICS
#include <dawn/native/DawnNative.h>
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkTiledImageUtils.h"
#include "include/gpu/graphite/Context.h"
#include "include/gpu/graphite/ContextOptions.h"
#include "include/gpu/graphite/Image.h"
#include "include/gpu/graphite/ImageProvider.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Recording.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/graphite/dawn/DawnBackendContext.h"
#include <cstring>
#include <algorithm>
#include <array>
#include <list>
#include <vector>

namespace xl::detail {
namespace {
// One provider per Recorder: texture uploads and their consumers are recorded
// in order on the same rendering thread, without cross-Recorder synchronization.
class DawnImages final : public skgpu::graphite::ImageProvider {
    struct Entry {
        std::array<uint32_t, SkTiledImageUtils::kNumImageKeyValues> key;
        bool mipmapped;
        size_t bytes;
        sk_sp<SkImage> image;
    };
    std::list<Entry> entries_;
    size_t bytes_ = 0;
    std::shared_ptr<std::atomic_bool> failed_;
public:
    explicit DawnImages(std::shared_ptr<std::atomic_bool> failed) : failed_(std::move(failed)) {}
    sk_sp<SkImage> findOrCreate(skgpu::graphite::Recorder* recorder, const SkImage* image,
                               SkImage::RequiredProperties properties) override {
        std::array<uint32_t, SkTiledImageUtils::kNumImageKeyValues> key;
        SkTiledImageUtils::GetImageKeyValues(image, key.data());
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->key == key && (it->mipmapped || !properties.fMipmapped)) {
                auto result = it->image;
                entries_.splice(entries_.begin(), entries_, it);
                return result;
            }
        }
        auto result = SkImages::TextureFromImage(recorder, image, properties);
        if (!result) {
            failed_->store(true, std::memory_order_relaxed);
            return nullptr;
        }
        // Bound retained references, not the total GPU allocation. Recordings
        // and active draws keep their own references after cache eviction.
        constexpr size_t budget = 64 * 1024 * 1024;
        const auto pixels = uint64_t(image->width()) * uint64_t(image->height());
        const auto estimate = std::max(uint64_t(result->textureSize()), pixels * 8);
        if (estimate <= budget) {
            while (!entries_.empty() && (entries_.size() >= 64 || bytes_ + estimate > budget)) {
                bytes_ -= entries_.back().bytes;
                entries_.pop_back();
            }
            const auto bytes = static_cast<size_t>(estimate);
            entries_.push_front({key, result->hasMipmaps(), bytes, result});
            bytes_ += bytes;
        }
        return result;
    }
};

class DawnDevice final : public GpuDevice {
    // One-shot callbacks retain this state. The repeatable error userdata stays
    // alive until device destruction and the final instance event drain.
    std::shared_ptr<std::atomic_bool> failed_ = std::make_shared<std::atomic_bool>(false);
    std::unique_ptr<dawn::native::Instance> instance_;
    wgpu::Device device_;
    std::unique_ptr<skgpu::graphite::Context> graphite_;
    std::unique_ptr<skgpu::graphite::Recorder> recorder_;
    uint32_t compiler_ = XL_SHADER_COMPILER_NONE;
public:
    ~DawnDevice() override {
        recorder_.reset();
        if (graphite_ && current()) graphite_->submit(skgpu::graphite::SubmitInfo{skgpu::graphite::SyncToCpu::kYes});
        graphite_.reset();
        if (device_) device_.Destroy();
        device_ = nullptr;
        if (instance_) dawn::native::InstanceProcessEvents(instance_->Get());
        instance_.reset();
    }
    bool initialize(wgpu::BackendType backend, bool software) {
        const auto timed_wait = wgpu::InstanceFeatureName::TimedWaitAny;
        wgpu::InstanceDescriptor instance_desc;
        instance_desc.requiredFeatureCount = 1;
        instance_desc.requiredFeatures = &timed_wait;
        instance_ = std::make_unique<dawn::native::Instance>(&instance_desc);
        wgpu::RequestAdapterOptions options;
        options.backendType = backend;
        options.forceFallbackAdapter = software;
        const char* dxc_toggle = "use_dxc";
        wgpu::DawnTogglesDescriptor toggles;
        if (backend == wgpu::BackendType::D3D12) {
            toggles.enabledToggleCount = 1;
            toggles.enabledToggles = &dxc_toggle;
            options.nextInChain = &toggles;
        }
        auto adapters = instance_->EnumerateAdapters(&options);
        for (auto& candidate : adapters) {
            wgpu::Adapter adapter(candidate.Get());
            wgpu::AdapterInfo adapter_info;
            if (adapter.GetInfo(&adapter_info) != wgpu::Status::Success ||
                (!software && adapter_info.adapterType == wgpu::AdapterType::CPU)) continue;
            std::vector<wgpu::FeatureName> features;
            // Request capabilities only when the real adapter offers them.
            for (auto feature : {wgpu::FeatureName::ShaderF16, wgpu::FeatureName::DualSourceBlending,
                                 wgpu::FeatureName::TextureCompressionBC, wgpu::FeatureName::TextureCompressionETC2,
                                 wgpu::FeatureName::TextureFormatsTier1, wgpu::FeatureName::Unorm16TextureFormats})
                if (adapter.HasFeature(feature)) features.push_back(feature);
            wgpu::Limits limits;
            if (adapter.GetLimits(&limits) != wgpu::Status::Success) continue;
            wgpu::DeviceDescriptor desc;
            desc.requiredFeatureCount = features.size();
            desc.requiredFeatures = features.data();
            desc.requiredLimits = &limits;
            const auto state = failed_;
            desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents,
                [state](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView) {
                    if (reason != wgpu::DeviceLostReason::Destroyed) state->store(true, std::memory_order_relaxed);
                });
            desc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType, wgpu::StringView,
                                               std::atomic_bool* state) {
                state->store(true, std::memory_order_relaxed);
            }, state.get());
            device_ = wgpu::Device::Acquire(candidate.CreateDevice(&desc));
            if (!device_) continue;
            skgpu::graphite::DawnBackendContext context;
            context.fInstance = wgpu::Instance(instance_->Get());
            context.fDevice = device_;
            context.fQueue = device_.GetQueue();
            skgpu::graphite::ContextOptions graphite_options;
            graphite_options.fShaderErrorHandler = &errors_;
            graphite_ = skgpu::graphite::ContextFactory::MakeDawn(context, graphite_options);
            if (graphite_) {
                skgpu::graphite::RecorderOptions recorder_options;
                recorder_options.fImageProvider = sk_make_sp<DawnImages>(failed_);
                recorder_options.fRequireOrderedRecordings = true;
                recorder_ = graphite_->makeRecorder(recorder_options);
            }
            if (recorder_) {
                if (backend == wgpu::BackendType::D3D12) {
                    const auto active = dawn::native::GetTogglesUsed(device_.Get());
                    compiler_ = std::any_of(active.begin(), active.end(), [](const char* value) {
                        return std::strcmp(value, "use_dxc") == 0;
                    }) ? XL_SHADER_COMPILER_DXC : XL_SHADER_COMPILER_FXC;
                }
                return true;
            }
            graphite_.reset();
            device_.Destroy();
            device_ = nullptr;
        }
        return false;
    }
    uint32_t shader_compiler() const override { return compiler_; }
    bool current() override {
        return device_ && !failed_->load(std::memory_order_relaxed) &&
               !errors_.failed.load(std::memory_order_relaxed) && (!graphite_ || !graphite_->isDeviceLost());
    }
    void abandon() override { failed_->store(true, std::memory_order_relaxed); }
    sk_sp<SkSurface> surface(int width, int height) override {
        if (!current() || !recorder_) return nullptr;
        return SkSurfaces::RenderTarget(recorder_.get(), SkImageInfo::MakeN32Premul(width, height));
    }
    bool finish() override {
        if (!current() || !recorder_ || !graphite_) return false;
        auto recording = recorder_->snap();
        if (recording) {
            skgpu::graphite::InsertRecordingInfo info;
            info.fRecording = recording.get();
            if (graphite_->insertRecording(info) != skgpu::graphite::InsertStatus::kSuccess) return false;
        }
        // Readback submits its own transfer and synchronizes only when requested.
        return graphite_->submit() && current();
    }
    bool read_pixels(SkSurface* surface, const SkImageInfo& info, void* data,
                     size_t row_bytes, int x, int y) override {
        if (!current() || !graphite_) return false;
        struct Read { std::unique_ptr<const SkImage::AsyncReadResult> pixels; };
        const auto result = std::make_shared<Read>();
        auto* callback_state = new std::shared_ptr<Read>(result);
        graphite_->asyncRescaleAndReadPixels(surface, info, SkIRect::MakeXYWH(x, y, info.width(), info.height()),
            SkImage::RescaleGamma::kSrc, SkImage::RescaleMode::kNearest,
            [](void* opaque, std::unique_ptr<const SkImage::AsyncReadResult> pixels) {
                const std::unique_ptr<std::shared_ptr<Read>> state(static_cast<std::shared_ptr<Read>*>(opaque));
                (*state)->pixels = std::move(pixels);
            }, callback_state);
        // Graphite guarantees its read callbacks complete on a synchronous submit.
        if (!graphite_->submit(skgpu::graphite::SubmitInfo{skgpu::graphite::SyncToCpu::kYes}) ||
            !current() || !result->pixels) return false;
        auto* destination = static_cast<unsigned char*>(data);
        const auto* source = static_cast<const unsigned char*>(result->pixels->data(0));
        for (int row = 0; row < info.height(); ++row)
            std::memcpy(destination + size_t(row) * row_bytes,
                        source + size_t(row) * result->pixels->rowBytes(0), info.minRowBytes());
        return true;
    }
};
}
std::unique_ptr<GpuDevice> make_dawn_device(bool software) {
    if (software) {
        auto device = std::make_unique<DawnDevice>();
        return device->initialize(wgpu::BackendType::Vulkan, true) ? std::move(device) : nullptr;
    }
#ifdef _WIN32
    {
        auto device = std::make_unique<DawnDevice>();
        if (device->initialize(wgpu::BackendType::D3D12, false)) return device;
    }
#endif
    auto device = std::make_unique<DawnDevice>();
    return device->initialize(wgpu::BackendType::Vulkan, false) ? std::move(device) : nullptr;
}
}
#endif
