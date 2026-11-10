#pragma once
#include "updates.h"
#include <condition_variable>
#include <mutex>

namespace xl::detail {
enum class NativeAXAction { focus, activate, value, expand, collapse };
struct NativeAXRequest {
    xl_node_id document = 0, node = 0;
    NativeAXAction action = NativeAXAction::focus;
    std::string text;
    std::mutex mutex;
    std::condition_variable changed;
    bool cancelled = false, started = false, done = false;
    xl_status status = XL_BUSY;
};
class NativeAX {
public:
    virtual ~NativeAX() = default;
    virtual xl_status close_status() const = 0;
    virtual xl_status focus_window() = 0;
    virtual void refresh_focus() noexcept = 0;
    static std::unique_ptr<NativeAX> create(uint32_t kind, uintptr_t window, bool owned);
    virtual bool pending() const = 0;
    virtual std::vector<std::shared_ptr<NativeAXRequest>> take_requests() = 0;
    virtual bool publish(std::shared_ptr<const AXUpdate> update, int width, int height) noexcept = 0;
    virtual void provider(uint32_t& kind, uintptr_t& value) = 0;
};
uint64_t native_ax_objects() noexcept;
inline void complete_native_ax(const std::shared_ptr<NativeAXRequest>& request, xl_status status) {
    std::lock_guard lock(request->mutex);
    request->status = status; request->done = true; request->changed.notify_all();
}
}
