#include "executor.h"
#include <exception>
#include <stdexcept>
#include <utility>

namespace xl::detail {

Executor::Executor(std::size_t workers, std::size_t queue_capacity)
    : owner_(std::this_thread::get_id()), worker_limit_(workers), queue_capacity_(queue_capacity) {
    if (workers == 0 || workers > 64 || queue_capacity == 0)
        throw std::invalid_argument("invalid executor capacity");
    threads_.reserve(workers);
}

Executor::~Executor() noexcept {
    if (!shutdown()) std::terminate();
}

void Executor::start(std::unique_lock<std::mutex>& lock) {
    if (!threads_.empty()) return;
    try {
        for (std::size_t index = 0; index < worker_limit_; ++index)
            threads_.emplace_back([this] { worker_loop(); });
        stats_.workers = threads_.size();
    } catch (...) {
        // Partially created workers cannot be joined while holding their mutex.
        stats_.accepting = false;
        std::vector<std::thread> partial;
        partial.swap(threads_);
        stats_.workers = 0;
        cleaning_startup_ = true;
        lock.unlock();
        wake_.notify_all();
        for (auto& thread : partial) if (thread.joinable()) thread.join();
        lock.lock();
        cleaning_startup_ = false;
        startup_finished_.notify_all();
        throw;
    }
}

SubmitResult Executor::try_submit(std::function<void()> task) {
    if (!task) return SubmitResult::invalid;
    {
        std::unique_lock lock(mutex_);
        if (!stats_.accepting) return SubmitResult::closed;
        if (queue_.size() >= queue_capacity_) return SubmitResult::full;
        start(lock);
        queue_.push_back(std::move(task));
        ++stats_.accepted;
        stats_.pending = queue_.size();
    }
    wake_.notify_one();
    return SubmitResult::accepted;
}

bool Executor::shutdown(ShutdownMode mode) {
    if (std::this_thread::get_id() != owner_) return false;
    std::deque<std::function<void()>> canceled;
    {
        std::unique_lock lock(mutex_);
        startup_finished_.wait(lock, [this] { return !cleaning_startup_; });
        stats_.accepting = false;
        if (mode == ShutdownMode::cancel_pending) {
            canceled.swap(queue_);
            stats_.canceled += canceled.size();
            stats_.pending = 0;
        }
    }
    // A capture destructor may call back into stats or another host service.
    canceled.clear();
    wake_.notify_all();
    for (auto& thread : threads_) if (thread.joinable()) thread.join();
    {
        std::lock_guard lock(mutex_);
        stats_.workers = 0;
    }
    return true;
}

ExecutorStats Executor::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

void Executor::worker_loop() noexcept {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] { return !queue_.empty() || !stats_.accepting; });
            if (queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop_front();
            stats_.pending = queue_.size();
            ++stats_.active;
        }
        bool failed = false;
        try { task(); } catch (...) { failed = true; }
        task = {};
        {
            std::lock_guard lock(mutex_);
            --stats_.active;
            ++stats_.completed;
            if (failed) ++stats_.failed;
        }
    }
}

}
