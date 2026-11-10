#include "resources/service.h"
#include <zstd.h>
#include <atomic>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <new>
#include <thread>

using namespace xl::detail;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "line " << __LINE__ << ": " #condition "\n"; std::terminate(); } } while (false)

static void await_idle(ResourceService& service) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (service.stats().executor.active || service.stats().executor.pending) {
        CHECK(std::chrono::steady_clock::now() < deadline);
        std::this_thread::yield();
    }
}

static std::vector<std::byte> compress(const std::vector<std::byte>& plain) {
    std::vector<std::byte> result(ZSTD_compressBound(plain.size()));
    auto* context = ZSTD_createCCtx();
    CHECK(context);
    CHECK(!ZSTD_isError(ZSTD_CCtx_setParameter(context, ZSTD_c_checksumFlag, 1)));
    const auto size = ZSTD_compress2(context, result.data(), result.size(), plain.data(), plain.size());
    ZSTD_freeCCtx(context);
    CHECK(!ZSTD_isError(size));
    result.resize(size);
    return result;
}

struct Gate {
    std::promise<void> entered, release;
    std::shared_future<void> ready = release.get_future().share();
    std::atomic<int> allocations{0};
    ResourceService* service = nullptr;
    static void* allocate(void* user, size_t size, size_t alignment) {
        auto& self = *static_cast<Gate*>(user);
        if (self.service) (void)self.service->stats();
        if (++self.allocations == 1) {
            self.entered.set_value();
            self.ready.wait();
        }
        return ::operator new(size, std::align_val_t(alignment));
    }
    static void free(void* user, void* data, size_t, size_t alignment) {
        auto& self = *static_cast<Gate*>(user);
        if (self.service) (void)self.service->stats();
        ::operator delete(data, std::align_val_t(alignment));
    }
    Allocator allocator() { return {this, allocate, free}; }
};

static void lazy_and_coalesced() {
    std::vector<std::byte> plain(1000, std::byte{'x'});
    auto encoded = compress(plain);
    Gate gate;
    ResourceService service({{1, encoded, plain.size(), 64}}, 2, 8, 8192, gate.allocator());
    gate.service = &service;
    CHECK(service.stats().decompressions == 0 && gate.allocations == 0 && service.stats().executor.workers == 0);
    auto prewarm = service.prewarm(1);
    gate.entered.get_future().wait();
    std::vector<ResourceRequest> requests(20);
    std::vector<std::thread> clients;
    for (auto& request : requests) clients.emplace_back([&service, &request] { request = service.request(1); });
    for (auto& client : clients) client.join();
    CHECK(service.stats().decompressions == 1 && service.stats().coalesced == 20);
    CHECK(requests[0].cancel() && !requests[0].cancel());
    CHECK(requests[0].ready() && requests[0].get().status == ResourceStatus::canceled);
    gate.release.set_value();
    auto loaded = prewarm.get();
    CHECK(loaded.status == ResourceStatus::ready && loaded.lease.bytes().size() == plain.size());
    CHECK(std::memcmp(loaded.lease.bytes().data(), plain.data(), plain.size()) == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(loaded.lease.bytes().data()) % 64 == 0);
    for (size_t i = 1; i < requests.size(); ++i) {
        auto result = requests[i].get();
        CHECK(result.status == ResourceStatus::ready && result.lease.bytes().data() == loaded.lease.bytes().data());
    }
    service.trim();
    auto still_live = service.request(1).get();
    CHECK(still_live.lease.bytes().data() == loaded.lease.bytes().data());
    CHECK(service.stats().decompressions == 1);
    CHECK(service.shutdown());
    CHECK(service.request(1).get().status == ResourceStatus::closed);
    CHECK(loaded.lease.bytes()[0] == std::byte{'x'});
    gate.service = nullptr;
}

static void failures_and_reuse() {
    std::vector<std::byte> plain(500, std::byte{'a'});
    auto encoded = compress(plain);
    auto damaged = encoded;
    damaged.back() ^= std::byte{1};
    ResourceService service({{1, encoded, plain.size()}, {2, damaged, plain.size()}, {3, encoded, plain.size() + 1}}, 1, 4, 4096);
    CHECK(service.request(9).get().status == ResourceStatus::not_found);
    CHECK(service.request(2).get().status == ResourceStatus::corrupt);
    CHECK(service.request(3).get().status == ResourceStatus::corrupt);
    CHECK(service.request(1).get().status == ResourceStatus::ready);
    CHECK(service.stats().failures == 2 && service.stats().pool.reuses > 0);
    await_idle(service);
    service.trim();
    const auto before = service.stats().decompressions;
    CHECK(service.request(1).get().status == ResourceStatus::ready);
    CHECK(service.stats().decompressions == before + 1);
    Allocator oom{nullptr, [](void*, size_t, size_t) -> void* { return nullptr; }, [](void*, void*, size_t, size_t) {}};
    ResourceService refused({{1, encoded, plain.size()}}, 1, 2, 4096, oom);
    CHECK(refused.request(1).get().status == ResourceStatus::out_of_memory);
}

static void bounded_queue_and_close() {
    std::vector<std::byte> plain(1024, std::byte{'b'});
    auto encoded = compress(plain);
    Gate gate;
    ResourceService service({{1, encoded, plain.size()}, {2, encoded, plain.size()}, {3, encoded, plain.size()}}, 1, 1, 4096, gate.allocator());
    gate.service = &service;
    auto active = service.request(1);
    gate.entered.get_future().wait();
    auto queued = service.request(2);
    CHECK(service.request(3).get().status == ResourceStatus::busy);
    std::promise<bool> wrong_thread;
    std::thread probe([&] { wrong_thread.set_value(service.shutdown()); });
    probe.join();
    CHECK(!wrong_thread.get_future().get());
    std::thread unblock([&] {
        CHECK(active.get().status == ResourceStatus::closed);
        CHECK(queued.get().status == ResourceStatus::closed);
        gate.release.set_value();
    });
    CHECK(service.shutdown());
    unblock.join();
    CHECK(service.stats().decompressions == 1 && !service.stats().accepting);
    gate.service = nullptr;
}

static void lease_outlives_service() {
    std::vector<std::byte> plain(100, std::byte{'c'});
    auto encoded = compress(plain);
    ResourceLease lease;
    {
        ResourceService service({{1, encoded, plain.size()}}, 1, 4, 1024);
        lease = service.request(1).get().lease;
    }
    CHECK(lease.bytes().size() == plain.size() && lease.bytes()[99] == std::byte{'c'});
}

int main() {
    lazy_and_coalesced();
    failures_and_reuse();
    bounded_queue_and_close();
    lease_outlives_service();
    std::cout << "lazy memory-only resources: coalescing, cancellation, lifetime, bounds and failures passed\n";
}
