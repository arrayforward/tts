#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>

#include "clock.h"
#include "copy_channel.h"
#include "priority_timer.h"
#include "task.h"
#include "thread_pool.h"

namespace tts::framework {

class Reactor {
public:
    Reactor(std::shared_ptr<IClock> clock,
            std::size_t cpu_workers,
            std::size_t io_workers)
        : clock_(std::move(clock)),
          cpu_pool_(ThreadPool::Kind::Cpu, cpu_workers),
          io_pool_(ThreadPool::Kind::Io, io_workers),
          skip_threshold_(std::chrono::seconds(1)) {}

    ~Reactor() { stop(); }

    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    using TickFn = std::function<void()>;

    void run(TickFn on_tick) {
        if (running_.exchange(true)) return;
        tick_fn_ = std::move(on_tick);
        loop_thread_ = std::jthread([this](std::stop_token st) { main_loop(st); });
    }

    void stop() {
        if (!running_.exchange(false)) return;
        if (loop_thread_.joinable()) loop_thread_.request_stop();
        cv_.notify_all();
        if (loop_thread_.joinable()) loop_thread_.join();
        cpu_pool_.shutdown();
        io_pool_.shutdown();
    }

    [[nodiscard]] bool is_running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    ThreadPool& cpu_pool() noexcept { return cpu_pool_; }
    ThreadPool& io_pool() noexcept { return io_pool_; }
    PriorityTimer& timer() noexcept { return timer_; }
    std::shared_ptr<IClock> clock() const noexcept { return clock_; }

    void schedule_cpu(Task t) {
        auto id = t.id();
        auto name = std::string(t.name());
        auto kind = t.kind();
        cpu_pool_.submit([fn = std::move(t), id, name, kind]() mutable {
            fn.run_with_slow_warning();
        });
    }

    void schedule_io(Task t) {
        auto id = t.id();
        auto name = std::string(t.name());
        auto kind = t.kind();
        io_pool_.submit([fn = std::move(t), id, name, kind]() mutable {
            fn.run_with_slow_warning();
        });
    }

    void schedule_timer(std::chrono::milliseconds delay, bool skippable, Task t) {
        timer_.schedule_in(delay, skippable, [fn = std::move(t), this]() mutable {
            fn.run_with_slow_warning();
        });
    }

    void notify_loop() { cv_.notify_one(); }

private:
    void main_loop(std::stop_token st) {
        while (!st.stop_requested() && running_.load(std::memory_order_acquire)) {
            auto now = clock_->now();
            auto due = timer_.pop_expired(now, skip_threshold_);
            for (auto& e : due) {
                if (e.fn) e.fn();
            }
            if (tick_fn_) tick_fn_();
            std::unique_lock lk(timer_mtx_);
            cv_.wait_for(lk, std::chrono::milliseconds(1), [&] {
                return st.stop_requested() || !running_.load();
            });
        }
    }

    std::shared_ptr<IClock> clock_;
    ThreadPool cpu_pool_;
    ThreadPool io_pool_;
    PriorityTimer timer_;
    std::chrono::seconds skip_threshold_;

    std::atomic<bool> running_{false};
    TickFn tick_fn_;
    std::jthread loop_thread_;

    std::mutex timer_mtx_;
    std::condition_variable cv_;
};

}  // namespace tts::framework