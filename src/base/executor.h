#ifndef XL_BASE_EXECUTOR_H
#define XL_BASE_EXECUTOR_H

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace xl::detail {

enum class SubmitResult { accepted, full, closed, invalid };
enum class ShutdownMode { drain, cancel_pending };

struct ExecutorStats {
    std::size_t workers = 0;
    std::size_t pending = 0;
    std::size_t active = 0;
    std::uint64_t accepted = 0;
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t canceled = 0;
    bool accepting = true;
};

// Construct/close/destroy on the owner thread. Submit and stats are thread-safe.
// Task bodies and capture destructors run outside internal locks. Dependencies
// captured by an executing task must remain valid until shutdown has returned.
class Executor final {
public:
    Executor(std::size_t workers, std::size_t queue_capacity);
    ~Executor() noexcept;
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    SubmitResult try_submit(std::function<void()> task);
    bool shutdown(ShutdownMode mode = ShutdownMode::drain);
    ExecutorStats stats() const;

private:
    void worker_loop() noexcept;
    void start(std::unique_lock<std::mutex>& lock);

    const std::thread::id owner_;
    const std::size_t worker_limit_;
    const std::size_t queue_capacity_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable startup_finished_;
    std::deque<std::function<void()>> queue_;
    std::vector<std::thread> threads_;
    ExecutorStats stats_;
    bool cleaning_startup_ = false;
};

}
#endif
