#include "service.h"
#include <zstd.h>
#include <zstd_errors.h>
#include <map>
#include <mutex>
#include <new>
#include <stdexcept>
#include <utility>

namespace xl::detail {

struct ResourceRequest::State {
    std::mutex mutex;
    bool completed = false;
    std::promise<ResourceResult> promise;
    std::shared_future<ResourceResult> future = promise.get_future().share();

    bool complete(ResourceResult result) {
        std::lock_guard lock(mutex);
        if (completed) return false;
        promise.set_value(std::move(result));
        completed = true;
        return true;
    }
};

bool ResourceRequest::cancel() const {
    return state_ && state_->complete({ResourceStatus::canceled, {}});
}
bool ResourceRequest::ready() const {
    return state_ && state_->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}
ResourceResult ResourceRequest::get() const {
    return state_ ? state_->future.get() : ResourceResult{ResourceStatus::not_found, {}};
}

struct ResourceService::State {
    struct Entry {
        ResourceSpec spec;
        bool loading = false;
        std::weak_ptr<const MemoryBlock> live;
        std::shared_ptr<const MemoryBlock> cached;
        std::vector<std::shared_ptr<ResourceRequest::State>> waiters;
    };

    State(std::vector<ResourceSpec> catalog, std::size_t budget, Allocator allocator)
        : pool(budget / 4, allocator), cache_limit(budget - budget / 4) {
        for (auto& spec : catalog) {
            if (spec.compressed.empty() || spec.decoded_size == 0 || spec.alignment == 0
                    || (spec.alignment & (spec.alignment - 1)) != 0
                    || !entries.emplace(spec.id, Entry{spec, false, {}, {}, {}}).second)
                throw std::invalid_argument("invalid or duplicate embedded resource");
        }
    }

    void finish(std::uint32_t id, ResourceStatus status, std::shared_ptr<const MemoryBlock> block = {}) {
        std::vector<std::shared_ptr<ResourceRequest::State>> waiters;
        {
            std::lock_guard lock(mutex);
            auto& entry = entries.at(id);
            waiters.swap(entry.waiters);
            entry.loading = false;
            if (!counters.accepting) status = ResourceStatus::closed;
            if (status == ResourceStatus::ready) {
                entry.live = block;
                if (block->capacity <= cache_limit - counters.cache_bytes) {
                    entry.cached = block;
                    counters.cache_bytes += block->capacity;
                }
            } else if (status != ResourceStatus::closed && status != ResourceStatus::busy) {
                ++counters.failures;
            }
        }
        ResourceResult result{status, status == ResourceStatus::ready ? ResourceLease(std::move(block)) : ResourceLease{}};
        for (const auto& waiter : waiters) waiter->complete(result);
    }

    void load(std::uint32_t id) noexcept {
        try {
            ResourceSpec spec;
            {
                std::lock_guard lock(mutex);
                if (!counters.accepting) return;
                spec = entries.at(id).spec;
                ++counters.decompressions;
            }
            const auto size = ZSTD_getFrameContentSize(spec.compressed.data(), spec.compressed.size());
            if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN || size != spec.decoded_size) {
                finish(id, ResourceStatus::corrupt);
                return;
            }
            auto block = pool.allocate(spec.decoded_size, spec.alignment);
            const auto decoded = ZSTD_decompress(block->data, block->size, spec.compressed.data(), spec.compressed.size());
            if (ZSTD_isError(decoded) || decoded != spec.decoded_size) {
                finish(id, ZSTD_isError(decoded) && ZSTD_getErrorCode(decoded) == ZSTD_error_memory_allocation
                           ? ResourceStatus::out_of_memory : ResourceStatus::corrupt);
                return;
            }
            finish(id, ResourceStatus::ready, std::move(block));
        } catch (const std::bad_alloc&) {
            finish(id, ResourceStatus::out_of_memory);
        } catch (...) {
            finish(id, ResourceStatus::internal_error);
        }
    }

    mutable std::mutex mutex;
    MemoryPool pool;
    const std::size_t cache_limit;
    std::map<std::uint32_t, Entry> entries;
    ResourceStats counters;
};

ResourceService::ResourceService(std::vector<ResourceSpec> catalog, std::size_t workers, std::size_t queue_capacity,
                               std::size_t cache_budget, Allocator allocator)
    : state_(std::make_shared<State>(std::move(catalog), cache_budget, allocator)),
      executor_(workers, queue_capacity), owner_(std::this_thread::get_id()) {}

ResourceService::~ResourceService() noexcept {
    if (!shutdown()) std::terminate();
}

ResourceRequest ResourceService::request(std::uint32_t id) {
    auto waiter = std::make_shared<ResourceRequest::State>();
    ResourceRequest request(waiter);
    std::shared_ptr<const MemoryBlock> hit;
    auto immediate = ResourceStatus::ready;
    bool schedule = false;
    {
        std::lock_guard lock(state_->mutex);
        const auto found = state_->entries.find(id);
        if (!state_->counters.accepting) {
            immediate = ResourceStatus::closed;
        } else if (found == state_->entries.end()) {
            immediate = ResourceStatus::not_found;
        } else {
            auto& entry = found->second;
            hit = entry.live.lock();
            if (hit) {
                ++state_->counters.hits;
            } else {
                entry.waiters.push_back(waiter);
                if (entry.loading) ++state_->counters.coalesced;
                else schedule = entry.loading = true;
            }
        }
    }
    if (hit || immediate != ResourceStatus::ready) {
        waiter->complete({immediate, ResourceLease(std::move(hit))});
    } else if (schedule) {
        try {
            const auto outcome = executor_.try_submit([state = state_, id] { state->load(id); });
            if (outcome != SubmitResult::accepted)
                state_->finish(id, outcome == SubmitResult::full ? ResourceStatus::busy : ResourceStatus::closed);
        } catch (const std::bad_alloc&) {
            state_->finish(id, ResourceStatus::out_of_memory);
        } catch (...) {
            state_->finish(id, ResourceStatus::internal_error);
        }
    }
    return request;
}

ResourceStats ResourceService::stats() const {
    ResourceStats result;
    {
        std::lock_guard lock(state_->mutex);
        result = state_->counters;
    }
    result.pool = state_->pool.stats();
    result.executor = executor_.stats();
    return result;
}

std::size_t ResourceService::trim() {
    std::size_t bytes = 0;
    for (auto& [id, entry] : state_->entries) {
        (void)id;
        std::shared_ptr<const MemoryBlock> released;
        {
            std::lock_guard lock(state_->mutex);
            released = std::move(entry.cached);
            if (released) {
                bytes += released->capacity;
                state_->counters.cache_bytes -= released->capacity;
            }
        }
        // Free outside the catalog lock without allocating bookkeeping memory.
        // This also keeps shutdown viable after an allocation failure.
    }
    return bytes + state_->pool.trim();
}

bool ResourceService::shutdown() {
    if (std::this_thread::get_id() != owner_) return false;
    {
        std::lock_guard lock(state_->mutex);
        state_->counters.accepting = false;
    }
    // Detach each waiter list under the lock, then fulfill promises outside it.
    // No user callback or allocator executes while the catalog is locked.
    for (auto& [id, entry] : state_->entries) {
        (void)entry;
        state_->finish(id, ResourceStatus::closed);
    }
    executor_.shutdown(ShutdownMode::cancel_pending);
    trim();
    return true;
}

}
