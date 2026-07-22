#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

namespace tts::framework {

class ThreadPool {
public:
    using TaskFn = std::function<void()>;

    enum class Kind {
        Cpu,
        Io,
    };

    ThreadPool(Kind kind, std::size_t n_threads)
        : kind_(kind), stop_(false) {
        if (n_threads == 0) n_threads = 1;
        for (std::size_t i = 0; i < n_threads; ++i) {
            workers_.emplace_back([this](std::stop_token st) { worker_loop(st); });
        }
    }

    ~ThreadPool() { shutdown(); }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void submit(TaskFn fn) {
        {
            std::lock_guard lk(mtx_);
            queue_.push(std::move(fn));
        }
        cv_.notify_one();
    }

    [[nodiscard]] std::size_t pending() const {
        std::lock_guard lk(mtx_);
        return queue_.size();
    }

    [[nodiscard]] Kind kind() const noexcept { return kind_; }

    void shutdown() {
        if (stop_.exchange(true)) return;
        cv_.notify_all();
        for (auto& t : workers_) {
            if (t.joinable()) t.join();
        }
    }

private:
    void worker_loop(std::stop_token st) {
        while (!stop_.load(std::memory_order_acquire) && !st.stop_requested()) {
            TaskFn fn;
            {
                std::unique_lock lk(mtx_);
                cv_.wait(lk, [&] { return stop_.load() || !queue_.empty(); });
                if (stop_.load() && queue_.empty()) return;
                if (queue_.empty()) continue;
                fn = std::move(queue_.front());
                queue_.pop();
            }
            if (fn) fn();
        }
    }

    Kind kind_;
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<TaskFn> queue_;
    std::atomic<bool> stop_;
    std::vector<std::jthread> workers_;
};

}  // namespace tts::framework