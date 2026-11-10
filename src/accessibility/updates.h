#pragma once
#include "document/document.h"
#include <deque>
#include <memory>
#include <unordered_map>

namespace xl::detail {
struct AXEvent {
    xl_ax_event event{};
    std::string text;
};
struct AXUpdate {
    xl_ax_update_info info{};
    std::vector<AXEntry> nodes;
    std::vector<AXEvent> events;
    size_t bytes = 0;
};
class AXUpdates final {
  public:
    void enable(bool value);
    bool enabled() const { return enabled_; }
    bool needs_publish(uint64_t generation) const { return enabled_ && generation_ != generation; }
    void publish(const DocumentFrame* frame, const std::optional<xl_text_selection>& reading, uint64_t generation) noexcept;
    xl_status pending() const;
    std::shared_ptr<const AXUpdate> take();
  private:
    bool enabled_ = false;
    uint64_t sequence_ = 0, generation_ = UINT64_MAX;
    xl_status error_ = XL_OK;
    size_t queued_bytes_ = 0;
    std::optional<xl_text_selection> selection_;
    std::shared_ptr<const AXUpdate> baseline_;
    std::deque<std::shared_ptr<const AXUpdate>> pending_;
    std::unordered_map<xl_node_id, AXLiveRegion> live_;
    void build(const DocumentFrame* frame, const std::optional<xl_text_selection>& reading, uint64_t generation);
};
}

struct xl_ax_update { std::shared_ptr<const xl::detail::AXUpdate> data; };
