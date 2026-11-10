#ifndef XL_BASE_MEMORY_POOL_H
#define XL_BASE_MEMORY_POOL_H
#include <cstddef>
#include <cstdint>
#include <memory>

namespace xl::detail {

// Callbacks must not throw. The user context must outlive all outstanding blocks.
struct Allocator {
    void* user = nullptr;
    void* (*allocate)(void* user, std::size_t size, std::size_t alignment) = nullptr;
    void (*deallocate)(void* user, void* data, std::size_t size, std::size_t alignment) = nullptr;
};

struct MemoryBlock {
    std::byte* data = nullptr;
    std::size_t size = 0;
    std::size_t capacity = 0;
    std::size_t alignment = 0;
};

struct PoolStats {
    std::size_t live_bytes = 0;
    std::size_t cached_bytes = 0;
    std::size_t cache_budget = 0;
    std::uint64_t system_allocations = 0;
    std::uint64_t system_frees = 0;
    std::uint64_t reuses = 0;
};

// Retains bounded free blocks; outstanding leases can outlive this facade.
// Bytes count allocation capacities, not logical resource sizes.
class MemoryPool final {
public:
    explicit MemoryPool(std::size_t cache_budget, Allocator allocator = {});
    std::shared_ptr<MemoryBlock> allocate(std::size_t size, std::size_t alignment = 16);
    std::size_t trim();
    PoolStats stats() const;

private:
    struct State;
    std::shared_ptr<State> state_;
};

}
#endif
