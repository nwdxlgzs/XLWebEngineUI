#include "memory_pool.h"
#include <map>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xl::detail {
namespace {
constexpr std::size_t smallest_bucket = 256;
constexpr std::size_t largest_bucket = 1024 * 1024;

void* default_allocate(void*, std::size_t size, std::size_t alignment) {
    return ::operator new(size, std::align_val_t(alignment));
}
void default_deallocate(void*, void* data, std::size_t, std::size_t alignment) {
    ::operator delete(data, std::align_val_t(alignment));
}

std::size_t capacity_for(std::size_t size) {
    if (size > largest_bucket) return size;
    auto capacity = smallest_bucket;
    while (capacity < size) capacity *= 2;
    return capacity;
}
}

struct MemoryPool::State {
    using Key = std::pair<std::size_t, std::size_t>;
    using FreeLists = std::map<Key, std::vector<void*>>;
    explicit State(std::size_t budget, Allocator callbacks) : allocator(callbacks) {
        counters.cache_budget = budget;
    }
    ~State() noexcept {
        for (auto& entry : free_lists)
            for (auto* data : entry.second)
                allocator.deallocate(allocator.user, data, entry.first.first, entry.first.second);
    }
    void release(MemoryBlock* block) noexcept {
        bool retained = false;
        {
            std::lock_guard lock(mutex);
            counters.live_bytes -= block->capacity;
            if (block->capacity <= largest_bucket && block->capacity <= counters.cache_budget - counters.cached_bytes) {
                try {
                    free_lists[{block->capacity, block->alignment}].push_back(block->data);
                    counters.cached_bytes += block->capacity;
                    retained = true;
                } catch (...) {
                    // Deallocation cannot fail because cache bookkeeping ran out of memory.
                }
            }
            if (!retained) ++counters.system_frees;
        }
        if (!retained) allocator.deallocate(allocator.user, block->data, block->capacity, block->alignment);
        delete block;
    }
    Allocator allocator;
    mutable std::mutex mutex;
    FreeLists free_lists;
    PoolStats counters;
};

MemoryPool::MemoryPool(std::size_t cache_budget, Allocator allocator) {
    if (bool(allocator.allocate) != bool(allocator.deallocate))
        throw std::invalid_argument("allocator callbacks must be provided as a pair");
    if (!allocator.allocate) allocator = {nullptr, default_allocate, default_deallocate};
    state_ = std::make_shared<State>(cache_budget, allocator);
}

std::shared_ptr<MemoryBlock> MemoryPool::allocate(std::size_t size, std::size_t alignment) {
    if (size == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0)
        throw std::invalid_argument("nonzero size and power-of-two alignment required");
    if (alignment < alignof(std::max_align_t)) alignment = alignof(std::max_align_t);
    auto block = std::make_unique<MemoryBlock>();
    block->size = size;
    block->capacity = capacity_for(size);
    block->alignment = alignment;
    auto state = state_;
    {
        std::lock_guard lock(state->mutex);
        const auto bucket = state->free_lists.find({block->capacity, alignment});
        if (bucket != state->free_lists.end() && !bucket->second.empty()) {
            block->data = static_cast<std::byte*>(bucket->second.back());
            bucket->second.pop_back();
            state->counters.cached_bytes -= block->capacity;
            state->counters.live_bytes += block->capacity;
            ++state->counters.reuses;
        }
    }
    if (!block->data) {
        // Host callbacks are never invoked with the pool mutex held.
        block->data = static_cast<std::byte*>(state->allocator.allocate(state->allocator.user, block->capacity, alignment));
        if (!block->data) throw std::bad_alloc();
        if (reinterpret_cast<std::uintptr_t>(block->data) % alignment != 0) {
            state->allocator.deallocate(state->allocator.user, block->data, block->capacity, alignment);
            throw std::invalid_argument("allocator returned a misaligned buffer");
        }
        std::lock_guard lock(state->mutex);
        state->counters.live_bytes += block->capacity;
        ++state->counters.system_allocations;
    }
    return std::shared_ptr<MemoryBlock>(block.release(), [state](MemoryBlock* item) noexcept { state->release(item); });
}

std::size_t MemoryPool::trim() {
    State::FreeLists freed;
    std::size_t bytes;
    {
        std::lock_guard lock(state_->mutex);
        freed.swap(state_->free_lists);
        bytes = state_->counters.cached_bytes;
        state_->counters.cached_bytes = 0;
        for (const auto& entry : freed) state_->counters.system_frees += entry.second.size();
    }
    for (auto& entry : freed)
        for (auto* data : entry.second)
            state_->allocator.deallocate(state_->allocator.user, data, entry.first.first, entry.first.second);
    return bytes;
}

PoolStats MemoryPool::stats() const {
    std::lock_guard lock(state_->mutex);
    return state_->counters;
}

}
