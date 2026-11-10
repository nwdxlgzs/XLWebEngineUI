#ifndef XL_RESOURCES_SERVICE_H
#define XL_RESOURCES_SERVICE_H
#include "base/executor.h"
#include "base/memory_pool.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <span>
#include <vector>

namespace xl::detail {

enum class ResourceStatus { ready, not_found, busy, canceled, closed, corrupt, out_of_memory, internal_error };

// The catalog and compressed bytes remain immutable until shutdown returns.
struct ResourceSpec {
    std::uint32_t id;
    std::span<const std::byte> compressed;
    std::size_t decoded_size;
    std::size_t alignment = 16;
};

class ResourceLease final {
public:
    ResourceLease() = default;
    explicit ResourceLease(std::shared_ptr<const MemoryBlock> block) : block_(std::move(block)) {}
    std::span<const std::byte> bytes() const noexcept {
        return block_ ? std::span<const std::byte>(block_->data, block_->size) : std::span<const std::byte>{};
    }
    explicit operator bool() const noexcept { return bool(block_); }
private:
    std::shared_ptr<const MemoryBlock> block_;
};

struct ResourceResult {
    ResourceStatus status = ResourceStatus::internal_error;
    ResourceLease lease;
};

class ResourceRequest final {
public:
    ResourceRequest() = default;
    bool valid() const noexcept { return bool(state_); }
    bool cancel() const;
    bool ready() const;
    ResourceResult get() const;
private:
    struct State;
    explicit ResourceRequest(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
    friend class ResourceService;
};

struct ResourceStats {
    std::uint64_t decompressions = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t hits = 0;
    std::uint64_t failures = 0;
    std::size_t cache_bytes = 0;
    bool accepting = true;
    PoolStats pool;
    ExecutorStats executor;
};

// Create/close/destroy on the owner thread. Requests and trim are thread-safe.
// No callback runs on workers; callers poll results or forward completion to UI.
class ResourceService final {
public:
    ResourceService(std::vector<ResourceSpec> catalog, std::size_t workers, std::size_t queue_capacity,
                    std::size_t cache_budget, Allocator allocator = {});
    ~ResourceService() noexcept;
    ResourceService(const ResourceService&) = delete;
    ResourceService& operator=(const ResourceService&) = delete;

    ResourceRequest request(std::uint32_t id);
    ResourceRequest prewarm(std::uint32_t id) { return request(id); }
    ResourceStats stats() const;
    std::size_t trim();
    bool shutdown();
private:
    struct State;
    std::shared_ptr<State> state_;
    Executor executor_;
    const std::thread::id owner_;
};

}
#endif
