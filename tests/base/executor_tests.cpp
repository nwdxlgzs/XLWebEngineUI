#include "base/executor.h"
#include <atomic>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace xl::detail;
#define CHECK(condition) do { if (!(condition)) { std::cerr << "line " << __LINE__ << ": " #condition "\n"; return 1; } } while (false)

int queue_bound_and_shutdown() {
    Executor executor(1, 2);
    CHECK(executor.stats().workers == 0);
    CHECK(executor.try_submit({}) == SubmitResult::invalid);
    std::promise<void> entered, release;
    auto continue_work = release.get_future().share();
    std::atomic<int> executions{0};
    CHECK(executor.try_submit([&] { entered.set_value(); continue_work.wait(); ++executions; }) == SubmitResult::accepted);
    entered.get_future().wait();
    CHECK(executor.try_submit([&] { ++executions; }) == SubmitResult::accepted);
    CHECK(executor.try_submit([&] { ++executions; }) == SubmitResult::accepted);
    CHECK(executor.try_submit([] {}) == SubmitResult::full);
    CHECK(executor.stats().pending == 2);
    release.set_value();
    CHECK(executor.shutdown());
    CHECK(executions == 3);
    CHECK(executor.try_submit([] {}) == SubmitResult::closed);
    CHECK(executor.stats().completed == 3);
    CHECK(executor.stats().active == 0 && executor.stats().workers == 0);
    return 0;
}

int cancellation_and_reentry() {
    Executor executor(1, 4);
    std::promise<void> entered, release, captures_released;
    auto proceed = release.get_future().share();
    CHECK(executor.try_submit([&] { entered.set_value(); proceed.wait(); }) == SubmitResult::accepted);
    entered.get_future().wait();
    std::atomic<int> executed{0};
    auto capture = std::shared_ptr<int>(new int(1), [&](int* value) {
        // Deadlocks if capture destruction happens while the executor lock is held.
        const auto snapshot = executor.stats();
        if (snapshot.accepting) std::terminate();
        delete value;
        captures_released.set_value();
    });
    CHECK(executor.try_submit([&, capture] { ++executed; }) == SubmitResult::accepted);
    capture.reset();
    CHECK(executor.try_submit([&] { ++executed; }) == SubmitResult::accepted);
    std::thread unblock([&] { captures_released.get_future().wait(); release.set_value(); });
    CHECK(executor.shutdown(ShutdownMode::cancel_pending));
    unblock.join();
    CHECK(executed == 0);
    CHECK(executor.stats().completed == 1 && executor.stats().canceled == 2);
    return 0;
}

int exceptions_and_owner_thread() {
    Executor executor(2, 8);
    std::promise<bool> foreign_close;
    CHECK(executor.try_submit([] { throw std::runtime_error("expected task failure"); }) == SubmitResult::accepted);
    CHECK(executor.try_submit([&] { foreign_close.set_value(executor.shutdown()); }) == SubmitResult::accepted);
    CHECK(!foreign_close.get_future().get());
    CHECK(executor.shutdown());
    CHECK(executor.stats().completed == 2 && executor.stats().failed == 1);
    return 0;
}

int concurrent_producers() {
    Executor executor(4, 16);
    std::atomic<int> executed{0}, active{0}, peak{0};
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&] {
            for (int index = 0; index < 150; ++index) {
                while (executor.try_submit([&] {
                    const auto current = ++active;
                    auto previous = peak.load();
                    while (previous < current && !peak.compare_exchange_weak(previous, current)) {}
                    std::this_thread::yield();
                    ++executed;
                    --active;
                }) == SubmitResult::full) std::this_thread::yield();
            }
        });
    }
    for (auto& producer : producers) producer.join();
    CHECK(executor.shutdown());
    CHECK(executed == 600);
    CHECK(peak > 0 && peak <= 4);
    CHECK(executor.stats().completed == 600 && executor.stats().accepted == 600);
    return 0;
}

int main() {
    if (queue_bound_and_shutdown() || cancellation_and_reentry() || exceptions_and_owner_thread() || concurrent_producers()) return 1;
    std::cout << "bounded executor: lazy start, backpressure, drain, cancellation, reentry, exceptions and concurrency passed\n";
    return 0;
}
