#include "base/memory_pool.h"
#include <atomic>
#include <cstring>
#include <iostream>
#include <memory>
#include <new>
#include <thread>
#include <vector>

using namespace xl::detail;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "line " << __LINE__ << ": " #condition "\n"; return 1; } } while (false)

int reuse_alignment_and_leases() {
    MemoryPool pool(4096);
    auto first = pool.allocate(300, 64);
    CHECK(reinterpret_cast<std::uintptr_t>(first->data) % 64 == 0);
    CHECK(first->capacity == 512 && first->size == 300);
    auto* original = first->data;
    auto pinned = first;
    first.reset();
    auto other = pool.allocate(300, 64);
    CHECK(other->data != original);
    pinned.reset();
    auto reused = pool.allocate(350, 64);
    CHECK(reused->data == original);
    CHECK(pool.stats().reuses == 1);
    CHECK(pool.trim() == 0); // outstanding leases cannot be reclaimed
    reused.reset();
    other.reset();
    CHECK(pool.stats().live_bytes == 0 && pool.stats().cached_bytes == 1024);
    CHECK(pool.trim() == 1024);
    CHECK(pool.stats().cached_bytes == 0);
    return 0;
}

int concurrency_and_budget() {
    MemoryPool pool(4096);
    std::vector<std::thread> workers;
    std::atomic<bool> valid{true};
    for (int thread = 0; thread < 8; ++thread) {
        workers.emplace_back([&] {
            for (int iteration = 0; iteration < 200; ++iteration) {
                auto block = pool.allocate(500, 256);
                std::memset(block->data, 0x5a, block->size);
                std::this_thread::yield();
                if (block->data[250] != std::byte{0x5a}) valid = false;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    CHECK(valid && pool.stats().live_bytes == 0);
    CHECK(pool.stats().cached_bytes <= 4096 && pool.stats().reuses > 0);
    auto large = pool.allocate(2 * 1024 * 1024, 64);
    const auto cached = pool.stats().cached_bytes;
    large.reset();
    CHECK(pool.stats().cached_bytes == cached);
    return 0;
}

struct Recorder {
    MemoryPool* pool = nullptr;
    int allocations = 0;
    int frees = 0;
    bool fail = false;
};
void* allocate_callback(void* user, std::size_t size, std::size_t alignment) {
    auto& recorder = *static_cast<Recorder*>(user);
    if (recorder.pool) (void)recorder.pool->stats();
    if (recorder.fail) return nullptr;
    ++recorder.allocations;
    return ::operator new(size, std::align_val_t(alignment));
}
void free_callback(void* user, void* data, std::size_t, std::size_t alignment) {
    auto& recorder = *static_cast<Recorder*>(user);
    if (recorder.pool) (void)recorder.pool->stats();
    ++recorder.frees;
    ::operator delete(data, std::align_val_t(alignment));
}

int host_callbacks_failure_and_lifetime() {
    Recorder recorder;
    std::shared_ptr<MemoryBlock> lease;
    {
        MemoryPool pool(4096, {&recorder, allocate_callback, free_callback});
        recorder.pool = &pool;
        recorder.fail = true;
        bool threw = false;
        try { (void)pool.allocate(100); } catch (const std::bad_alloc&) { threw = true; }
        CHECK(threw && pool.stats().live_bytes == 0 && pool.stats().system_allocations == 0);
        recorder.fail = false;
        auto block = pool.allocate(100);
        block.reset();
        CHECK(pool.trim() == 256 && recorder.frees == 1);
        lease = pool.allocate(100);
        lease->data[0] = std::byte{0x33};
        recorder.pool = nullptr;
    }
    CHECK(lease->data[0] == std::byte{0x33} && recorder.frees == 1);
    lease.reset();
    CHECK(recorder.allocations == recorder.frees);
    return 0;
}

int invalid_requests_do_not_allocate() {
    MemoryPool pool(0);
    for (const auto& request : std::vector<std::pair<std::size_t, std::size_t>>{{0, 16}, {20, 0}, {20, 3}}) {
        bool rejected = false;
        try { (void)pool.allocate(request.first, request.second); }
        catch (const std::invalid_argument&) { rejected = true; }
        CHECK(rejected);
    }
    CHECK(pool.stats().system_allocations == 0 && pool.stats().live_bytes == 0);
    auto block = pool.allocate(100, 64);
    block.reset();
    CHECK(pool.stats().cached_bytes == 0 && pool.stats().system_frees == 1);
    return 0;
}

int main() {
    if (reuse_alignment_and_leases() || concurrency_and_budget() || host_callbacks_failure_and_lifetime() || invalid_requests_do_not_allocate()) return 1;
    std::cout << "memory pool: alignment, pinned leases, reuse, budget, concurrency, host reentry, allocation failure and lifetime passed\n";
    return 0;
}
