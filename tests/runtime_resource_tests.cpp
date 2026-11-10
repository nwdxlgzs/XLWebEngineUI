#include <XLWebEngineUI/xl.hpp>
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <iostream>
#include <new>
#include <thread>
#include <vector>

#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " #condition "\n"; std::terminate(); } } while (false)

static void prepare(xl_runtime* runtime) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    xl_status status;
    while ((status = xl_runtime_prepare_text(runtime)) == XL_BUSY) {
        CHECK(std::chrono::steady_clock::now() < end);
        std::this_thread::yield();
    }
    CHECK(status == XL_OK);
}
static void normalize(xl_runtime* runtime) {
    constexpr char input[] = "e\xcc\x81";
    char output[8] = {'x'};
    size_t count = 0;
    CHECK(xl_runtime_normalize_utf8(runtime, input, 3, output, 1, &count) == XL_BUFFER_TOO_SMALL);
    CHECK(count == 2 && output[0] == 'x');
    CHECK(xl_runtime_normalize_utf8(runtime, input, 3, output, sizeof(output), &count) == XL_OK);
    CHECK(count == 2 && std::memcmp(output, "\xc3\xa9", 2) == 0);
    CHECK(xl_runtime_normalize_utf8(runtime, "\xff", 1, output, sizeof(output), &count) == XL_INVALID_ARGUMENT);
    CHECK(xl_runtime_normalize_utf8(runtime, nullptr, 0, nullptr, 0, &count) == XL_OK && count == 0);
}
static void leases_and_laziness() {
    xl::ResourceLease survivor;
    {
        xl::Runtime runtime;
        xl_resource_stats stats;
        std::memset(&stats, 0x55, sizeof(stats));
        auto old = stats;
        CHECK(xl_runtime_resource_stats(runtime.get(), &stats, sizeof(stats) - 1) == XL_ABI_MISMATCH);
        CHECK(std::memcmp(&stats, &old, sizeof(stats)) == 0);
        CHECK(xl_runtime_resource_stats(runtime.get(), &stats, sizeof(stats)) == XL_OK);
        CHECK(stats.decompressions == 0 && stats.live_bytes == 0 && stats.worker_threads == 0);
        auto missing = runtime.resource(999);
        CHECK(missing.poll(survivor) == XL_NOT_FOUND);
        std::vector<xl::ResourceRequest> requests;
        for (int i = 0; i < 12; ++i) requests.push_back(runtime.resource(XL_RESOURCE_ICU_DATA));
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (requests[0].poll(survivor) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < end);
            std::this_thread::yield();
        }
        CHECK((survivor.size() > 1024 * 1024 && survivor.size() < 16 * 1024 * 1024));
        CHECK(xl_runtime_resource_stats(runtime.get(), &stats, sizeof(stats)) == XL_OK && stats.decompressions == 1);
        CHECK(xl_runtime_trim_resources(runtime.get()) == XL_OK);
        xl::ResourceLease another;
        CHECK(runtime.resource(1).poll(another) == XL_OK && another.data() == survivor.data());
        prepare(runtime.get());
        normalize(runtime.get());
        CHECK(xl_runtime_set_allocator(runtime.get(), nullptr, nullptr, nullptr) == XL_BUSY);
    }
    CHECK((survivor.size() > 1024 * 1024 && survivor.size() < 16 * 1024 * 1024) && survivor.data());
}

struct Gate {
    std::promise<void> entered, release;
    std::shared_future<void> proceed = release.get_future().share();
    static void* allocate(void* context, size_t size, size_t alignment) {
        auto& self = *static_cast<Gate*>(context);
        self.entered.set_value();
        self.proceed.wait();
        return ::operator new(size, std::align_val_t(alignment));
    }
    static void free(void*, void* data, size_t, size_t alignment) { ::operator delete(data, std::align_val_t(alignment)); }
};
static void cancel_and_close() {
    Gate gate;
    xl_runtime* runtime = nullptr;
    CHECK(xl_runtime_create(nullptr, &runtime) == XL_OK);
    CHECK(xl_runtime_set_allocator(runtime, &gate, Gate::allocate, Gate::free) == XL_OK);
    xl_resource_request *active = nullptr, *cancel = nullptr;
    CHECK(xl_runtime_request_resource(runtime, 1, &active) == XL_OK);
    gate.entered.get_future().wait();
    CHECK(xl_runtime_request_resource(runtime, 1, &cancel) == XL_OK);
    CHECK(xl_resource_request_cancel(cancel) == XL_OK);
    xl_resource_lease* lease = nullptr;
    CHECK(xl_resource_request_poll(cancel, &lease) == XL_CANCELED && lease == nullptr);
    std::thread unblock([&] {
        xl_status status;
        while ((status = xl_resource_request_poll(active, &lease)) == XL_BUSY) std::this_thread::yield();
        CHECK(status == XL_CLOSED && !lease);
        gate.release.set_value();
    });
    CHECK(xl_runtime_destroy(runtime) == XL_OK);
    unblock.join();
    xl_resource_request_release(active);
    xl_resource_request_release(cancel);
}
static void shared_icu_lifetime() {
    xl_runtime *first = nullptr, *second = nullptr;
    CHECK(xl_runtime_create(nullptr, &first) == XL_OK && xl_runtime_create(nullptr, &second) == XL_OK);
    struct HostAllocator {
        std::atomic<int> allocations{0}, frees{0};
        static void* allocate(void* context, size_t size, size_t alignment) {
            ++static_cast<HostAllocator*>(context)->allocations;
            return ::operator new(size, std::align_val_t(alignment));
        }
        static void free(void* context, void* data, size_t, size_t alignment) {
            ++static_cast<HostAllocator*>(context)->frees;
            ::operator delete(data, std::align_val_t(alignment));
        }
    };
    {
        HostAllocator allocator;
        CHECK(xl_runtime_set_allocator(first, &allocator, HostAllocator::allocate, HostAllocator::free) == XL_OK);
        xl_resource_request* request = nullptr;
        CHECK(xl_runtime_request_resource(first, XL_RESOURCE_ICU_DATA, &request) == XL_OK);
        xl_resource_lease* lease = nullptr;
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        xl_status status;
        while ((status = xl_resource_request_poll(request, &lease)) == XL_BUSY) {
            CHECK(std::chrono::steady_clock::now() < end);
            std::this_thread::yield();
        }
        CHECK(status == XL_OK && lease);
        xl_resource_lease_release(lease);
        xl_resource_request_release(request);
        prepare(first);
        prepare(second);
        xl_resource_stats stats;
        CHECK(xl_runtime_resource_stats(first, &stats, sizeof(stats)) == XL_OK);
        CHECK(stats.retained_bytes <= 64ull * 1024 * 1024);
        CHECK(xl_runtime_resource_stats(second, &stats, sizeof(stats)) == XL_OK);
        CHECK(stats.decompressions == 0 && stats.worker_threads == 0 && (stats.shared_text_data_bytes > 1024 * 1024 && stats.shared_text_data_bytes < 16 * 1024 * 1024));
        normalize(first);
        CHECK(xl_runtime_destroy(first) == XL_OK);
        // The second Runtime must not retain an unobservable host allocation.
        CHECK(allocator.allocations == 1 && allocator.frees == 1);
    }
    normalize(second);
    CHECK(xl_runtime_destroy(second) == XL_OK);
    // Re-register after the last private ICU user has cleaned up.
    xl::Runtime again;
    prepare(again.get());
    normalize(again.get());
}
int main() {
    leases_and_laziness();
    cancel_and_close();
    shared_icu_lifetime();
    std::vector<std::thread> hosts;
    for (int i = 0; i < 4; ++i) hosts.emplace_back([] { for (int n = 0; n < 3; ++n) { xl::Runtime runtime; prepare(runtime.get()); normalize(runtime.get()); } });
    for (auto& host : hosts) host.join();
    std::cout << "public asynchronous resources, leases and concurrent private ICU lifetime passed\n";
}
