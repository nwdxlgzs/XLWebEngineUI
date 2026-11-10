#pragma once
#if defined(__linux__)
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <stdexcept>
#include <system_error>
#include <type_traits>
#include <utility>

namespace xl::detail {
// Shader validators/JIT compilers need more than musl's small default stack.
// One synchronous slot keeps View operations ordered without a growing queue.
class RenderExecutor {
    pthread_t thread_{};
    std::mutex mutex_;
    std::condition_variable wake_;
    std::function<void()> pending_;
    bool stopping_ = false;
    void run() {
        for (;;) {
            std::function<void()> task;
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || bool(pending_); });
                if (!pending_) return;
                // A moved-from std::function need not be empty in libc++.
                // Swap with the empty local so this slot executes exactly once.
                task.swap(pending_);
            }
            task();
        }
    }
public:
    RenderExecutor() {
        pthread_attr_t attributes;
        int error = pthread_attr_init(&attributes);
        if (error) throw std::system_error(error, std::generic_category());
        error = pthread_attr_setstacksize(&attributes, 4 * 1024 * 1024);
        if (!error) error = pthread_create(&thread_, &attributes, [](void* value) -> void* {
            static_cast<RenderExecutor*>(value)->run(); return nullptr;
        }, this);
        pthread_attr_destroy(&attributes);
        if (error) throw std::system_error(error, std::generic_category());
    }
    ~RenderExecutor() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_one();
        if (pthread_join(thread_, nullptr)) std::terminate();
    }
    bool current() const noexcept { return pthread_equal(thread_, pthread_self()); }
    template<class Function> auto invoke(Function&& function) -> std::invoke_result_t<Function> {
        if (current()) return std::forward<Function>(function)();
        using Result = std::invoke_result_t<Function>;
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<Function>(function));
        auto result = task->get_future();
        {
            std::lock_guard lock(mutex_);
            if (stopping_ || pending_) throw std::logic_error("graphics executor is busy or closed");
            pending_ = [task] { (*task)(); };
        }
        wake_.notify_one();
        return result.get();
    }
};
}
#endif
