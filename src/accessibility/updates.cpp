#include "updates.h"
#include <algorithm>
#include <unordered_set>

namespace xl::detail {
namespace {
constexpr size_t byte_limit = 64 * 1024 * 1024, batch_limit = 8;
bool same(const xl_node_bounds& a, const xl_node_bounds& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
bool position(const xl_text_position& a, const xl_text_position& b) {
    return a.node == b.node && a.offset_utf16 == b.offset_utf16 && a.affinity == b.affinity;
}
bool selection(const std::optional<xl_text_selection>& a, const std::optional<xl_text_selection>& b) {
    return a.has_value() == b.has_value() && (!a || (position(a->anchor, b->anchor) && position(a->focus, b->focus)));
}
xl_node_id focus(const std::vector<AXEntry>& nodes) {
    for (const auto& entry : nodes) if (entry.node.states & XL_AX_FOCUSED) return entry.node.node;
    return 0;
}
AXEvent event(uint32_t type, xl_node_id node = 0, xl_node_id related = 0, uint32_t properties = 0) {
    return {{sizeof(xl_ax_event), type, properties, 0, node, related, {}}, {}};
}
void append(std::string& destination, std::string_view text) {
    if (text.empty()) return;
    if (!destination.empty() && destination.back() != ' ' && text.front() != ' ') destination.push_back(' ');
    destination += text;
}
std::string region_text(const AXLiveRegion& region, xl_node_id atomic = 0) {
    std::string result;
    for (const auto& part : region.parts) if (!atomic || part.atomic == atomic) append(result, part.text);
    return result;
}
size_t retained_bytes(const AXUpdate& update) {
    size_t bytes = sizeof(AXUpdate);
    const auto count = [&](size_t amount) {
        if (amount > byte_limit - bytes) throw DocumentFailure{XL_UNSUPPORTED};
        bytes += amount;
    };
    if (update.nodes.capacity() > byte_limit / sizeof(AXEntry) || update.events.capacity() > byte_limit / sizeof(AXEvent))
        throw DocumentFailure{XL_UNSUPPORTED};
    count(update.nodes.capacity() * sizeof(AXEntry)); count(update.events.capacity() * sizeof(AXEvent));
    // Conservative accounting includes string capacity and terminators, even
    // when a library stores a short string within the already counted object.
    for (const auto& entry : update.nodes) { count(entry.name.capacity()); count(1); count(entry.value.capacity()); count(1); }
    for (const auto& entry : update.events) { count(entry.text.capacity()); count(1); }
    return bytes;
}
void live_events(const std::vector<AXLiveRegion>& current,
                 const std::unordered_map<xl_node_id, AXLiveRegion>& old,
                 std::unordered_map<xl_node_id, AXLiveRegion>& next, std::vector<AXEvent>& events) {
    for (const auto& region : current) {
        const auto previous = old.find(region.node);
        if (region.busy) {
            // Keep the last non-busy contents, so clearing busy exposes the
            // entire accumulated change even if a host consumes every frame.
            if (previous != old.end()) next.emplace(region.node, previous->second);
            else next.emplace(region.node, AXLiveRegion{region.node, region.politeness, false, region.alert, {}});
            continue;
        }
        next.emplace(region.node, region);
        if (previous == old.end()) {
            if (region.alert) {
                auto fresh = event(XL_AX_EVENT_LIVE_REGION, region.node, region.node);
                fresh.event.flags = region.politeness; fresh.text = region_text(region);
                if (!fresh.text.empty()) events.push_back(std::move(fresh));
            }
            continue;
        }
        const auto& before = previous->second;
        std::unordered_map<xl_node_id, const AXLivePart*> old_parts, new_parts;
        for (const auto& part : before.parts) old_parts.emplace(part.node, &part);
        for (const auto& part : region.parts) new_parts.emplace(part.node, &part);
        struct Change { const AXLivePart* part; bool removed; };
        std::vector<Change> changes;
        for (const auto& part : region.parts) {
            const auto found = old_parts.find(part.node);
            const uint32_t kind = found == old_parts.end() ? 1u : found->second->text != part.text ? 2u : 0u;
            if (kind & part.relevant) changes.push_back({&part, false});
        }
        for (const auto& part : before.parts)
            if ((part.relevant & 4) && !new_parts.contains(part.node)) changes.push_back({&part, true});
        std::unordered_set<xl_node_id> announced;
        for (const auto& change : changes) {
            const auto target = change.part->atomic ? change.part->atomic : change.part->node;
            if (!announced.insert(target).second) continue;
            auto update = event(XL_AX_EVENT_LIVE_REGION, region.node, target);
            update.event.flags = region.politeness;
            update.text = change.part->atomic ? region_text(region, target) : change.part->text;
            if (update.text.empty() && change.removed) update.text = change.part->atomic ? region_text(before, target) : change.part->text;
            if (!update.text.empty()) events.push_back(std::move(update));
        }
    }
}
}
void AXUpdates::enable(bool value) {
    if (enabled_ == value) return;
    enabled_ = value; generation_ = UINT64_MAX; error_ = XL_OK; queued_bytes_ = 0;
    selection_.reset(); baseline_.reset(); pending_.clear(); live_.clear();
}
void AXUpdates::publish(const DocumentFrame* frame, const std::optional<xl_text_selection>& reading, uint64_t generation) noexcept {
    if (!enabled_) return;
    try { build(frame, reading, generation); }
    catch (const DocumentFailure& error) { error_ = error.status; }
    catch (const std::bad_alloc&) { error_ = XL_OUT_OF_MEMORY; }
    catch (...) { error_ = XL_INTERNAL_ERROR; }
    if (error_ != XL_OK) {
        pending_.clear(); baseline_.reset(); live_.clear(); selection_.reset(); queued_bytes_ = 0;
        generation_ = generation;
    }
}
void AXUpdates::build(const DocumentFrame* frame, const std::optional<xl_text_selection>& reading, uint64_t generation) {
    std::optional<xl_text_selection> selected = reading;
    if (frame && frame->edit) {
        const auto& edit = *frame->edit;
        selected = xl_text_selection{sizeof(xl_text_selection), 0,
            {sizeof(xl_text_position), edit.anchor_utf16, edit.node, XL_TEXT_DOWNSTREAM, 0},
            {sizeof(xl_text_position), edit.focus_utf16, edit.node, XL_TEXT_DOWNSTREAM, 0}};
    }
    if (generation_ == generation && selection(selection_, selected)) return;
    auto update = std::make_shared<AXUpdate>();
    update->info = {sizeof(xl_ax_update_info), 0, sequence_ + 1, frame ? frame->identity : 0, frame ? frame->revision : 0, 0, 0};
    if (frame) update->nodes = frame->accessibility;
    std::unordered_map<xl_node_id, const AXEntry*> old_nodes, new_nodes;
    if (baseline_) for (const auto& node : baseline_->nodes) old_nodes.emplace(node.node.node, &node);
    for (const auto& node : update->nodes) new_nodes.emplace(node.node.node, &node);
    if (selected && (!new_nodes.contains(selected->anchor.node) || !new_nodes.contains(selected->focus.node))) selected.reset();
    const bool reset = !baseline_ || baseline_->info.document != update->info.document;
    if (reset) {
        update->info.flags = XL_AX_UPDATE_RESET;
        update->events.push_back(event(XL_AX_EVENT_RESET, update->info.document));
    } else {
        for (auto it = baseline_->nodes.rbegin(); it != baseline_->nodes.rend(); ++it)
            if (!new_nodes.contains(it->node.node)) update->events.push_back(event(XL_AX_EVENT_NODE_REMOVED, it->node.node, it->node.parent));
        for (const auto& next : update->nodes) {
            const auto found = old_nodes.find(next.node.node);
            if (found == old_nodes.end()) { update->events.push_back(event(XL_AX_EVENT_NODE_ADDED, next.node.node, next.node.parent)); continue; }
            const auto& before = *found->second;
            uint32_t changes = 0;
            if (before.node.role != next.node.role) changes |= XL_AX_CHANGE_ROLE;
            if (before.name != next.name) changes |= XL_AX_CHANGE_NAME;
            if (before.value != next.value) changes |= XL_AX_CHANGE_VALUE;
            if (before.node.states != next.node.states) changes |= XL_AX_CHANGE_STATES;
            if (before.node.actions != next.node.actions) changes |= XL_AX_CHANGE_ACTIONS;
            if (!same(before.node.bounds, next.node.bounds)) changes |= XL_AX_CHANGE_BOUNDS;
            if (before.node.parent != next.node.parent) changes |= XL_AX_CHANGE_PARENT;
            if (changes) update->events.push_back(event(XL_AX_EVENT_NODE_CHANGED, next.node.node, next.node.parent, changes));
        }
        std::unordered_map<xl_node_id, std::vector<xl_node_id>> old_children, new_children;
        for (const auto& node : baseline_->nodes) if (node.node.parent) old_children[node.node.parent].push_back(node.node.node);
        for (const auto& node : update->nodes) if (node.node.parent) new_children[node.node.parent].push_back(node.node.node);
        for (const auto& node : update->nodes)
            if (old_nodes.contains(node.node.node) && old_children[node.node.node] != new_children[node.node.node])
                update->events.push_back(event(XL_AX_EVENT_CHILDREN_CHANGED, node.node.node));
    }
    const auto old_focus = baseline_ ? focus(baseline_->nodes) : 0, new_focus = focus(update->nodes);
    if (old_focus != new_focus || (reset && new_focus)) update->events.push_back(event(XL_AX_EVENT_FOCUS_CHANGED, new_focus, old_focus));
    if (!selection(selection_, selected) || (reset && selected)) {
        auto changed = event(XL_AX_EVENT_SELECTION_CHANGED, selected ? selected->focus.node : 0);
        if (selected) changed.event.selection = *selected;
        update->events.push_back(std::move(changed));
    }
    std::unordered_map<xl_node_id, AXLiveRegion> next_live;
    const std::unordered_map<xl_node_id, AXLiveRegion> empty_live;
    if (frame) live_events(frame->live_regions, reset ? empty_live : live_, next_live, update->events);
    update->bytes = retained_bytes(*update);
    if (!update->events.empty()) {
        if (pending_.size() >= batch_limit || update->bytes > byte_limit - queued_bytes_) {
            // Full current tree makes recovery deterministic; discarded transient
            // announcements are explicitly reported by OVERFLOW, not replayed stale.
            update->info.flags |= XL_AX_UPDATE_RESET | XL_AX_UPDATE_OVERFLOW;
            update->events.clear(); update->events.push_back(event(XL_AX_EVENT_RESET, update->info.document));
            if (new_focus) update->events.push_back(event(XL_AX_EVENT_FOCUS_CHANGED, new_focus));
            if (selected) { auto e = event(XL_AX_EVENT_SELECTION_CHANGED, selected->focus.node); e.event.selection = *selected; update->events.push_back(std::move(e)); }
            pending_.clear(); queued_bytes_ = 0;
            update->bytes = retained_bytes(*update);
        }
        update->info.node_count = update->nodes.size(); update->info.event_count = update->events.size();
        pending_.push_back(update); queued_bytes_ += update->bytes; ++sequence_;
    }
    baseline_ = std::move(update); live_ = std::move(next_live); selection_ = selected; generation_ = generation; error_ = XL_OK;
}
xl_status AXUpdates::pending() const { return !enabled_ ? XL_NOT_FOUND : error_ != XL_OK ? error_ : pending_.empty() ? XL_NOT_FOUND : XL_OK; }
std::shared_ptr<const AXUpdate> AXUpdates::take() {
    auto result = pending_.front(); pending_.pop_front(); queued_bytes_ -= result->bytes; return result;
}
}

namespace {
template<class Function> xl_status access(const xl_ax_update* update, Function&& function) {
    if (!update || !update->data) return XL_INVALID_ARGUMENT;
    try { return function(*update->data); }
    catch (const std::bad_alloc&) { return XL_OUT_OF_MEMORY; }
    catch (...) { return XL_INTERNAL_ERROR; }
}
}
extern "C" {
xl_status xl_ax_update_clone(const xl_ax_update* update, xl_ax_update** copy) {
    if (!copy) return XL_INVALID_ARGUMENT;
    return access(update, [&](const auto&) { auto* next = new xl_ax_update{update->data}; *copy = next; return XL_OK; });
}
void xl_ax_update_release(xl_ax_update* update) { delete update; }
xl_status xl_ax_update_get_info(const xl_ax_update* update, xl_ax_update_info* info, size_t size) {
    if (!info) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*info)) return XL_ABI_MISMATCH;
    return access(update, [&](const auto& data) { *info = data.info; return XL_OK; });
}
xl_status xl_ax_update_get_node(const xl_ax_update* update, size_t index, xl_ax_node* node, size_t size) {
    if (!node) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*node)) return XL_ABI_MISMATCH;
    return access(update, [&](const auto& data) { if (index >= data.nodes.size()) return XL_NOT_FOUND; *node = data.nodes[index].node; return XL_OK; });
}
xl_status xl_ax_update_name(const xl_ax_update* update, size_t index, char* output, size_t capacity, size_t* required) {
    if (!required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return access(update, [&](const auto& data) { if (index >= data.nodes.size()) return XL_NOT_FOUND; return xl::detail::copy_string(data.nodes[index].name, output, capacity, required); });
}
xl_status xl_ax_update_value(const xl_ax_update* update, size_t index, char* output, size_t capacity, size_t* required) {
    if (!required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return access(update, [&](const auto& data) { if (index >= data.nodes.size()) return XL_NOT_FOUND; return xl::detail::copy_string(data.nodes[index].value, output, capacity, required); });
}
xl_status xl_ax_update_get_event(const xl_ax_update* update, size_t index, xl_ax_event* result, size_t size) {
    if (!result) return XL_INVALID_ARGUMENT;
    if (size < sizeof(*result)) return XL_ABI_MISMATCH;
    return access(update, [&](const auto& data) { if (index >= data.events.size()) return XL_NOT_FOUND; *result = data.events[index].event; return XL_OK; });
}
xl_status xl_ax_update_event_text(const xl_ax_update* update, size_t index, char* output, size_t capacity, size_t* required) {
    if (!required || (!output && capacity)) return XL_INVALID_ARGUMENT;
    return access(update, [&](const auto& data) { if (index >= data.events.size()) return XL_NOT_FOUND; return xl::detail::copy_string(data.events[index].text, output, capacity, required); });
}
}
