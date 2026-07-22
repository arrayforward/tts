#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <utility>
#include <vector>

namespace tts::framework {

using Clock = std::chrono::steady_clock;

struct TimerEntry {
    Clock::time_point deadline;
    bool skippable{false};
    std::function<void()> fn;
    std::uint64_t seq{0};

    bool operator>(const TimerEntry& o) const {
        if (deadline != o.deadline) return deadline > o.deadline;
        return seq > o.seq;
    }
};

class PriorityTimer {
public:
    PriorityTimer() = default;

    template <typename F>
    void schedule_at(Clock::time_point tp, bool skippable, F&& fn) {
        std::lock_guard lk(mtx_);
        heap_.push(TimerEntry{tp, skippable, std::forward<F>(fn), next_seq_++});
        cv_.notify_one();
    }

    template <typename Rep, typename Period, typename F>
    void schedule_in(std::chrono::duration<Rep, Period> d, bool skippable, F&& fn) {
        schedule_at(Clock::now() + d, skippable, std::forward<F>(fn));
    }

    [[nodiscard]] std::vector<TimerEntry> pop_expired(Clock::time_point now,
                                                      std::chrono::seconds skip_threshold) {
        std::vector<TimerEntry> out;
        std::lock_guard lk(mtx_);
        while (!heap_.empty() && heap_.top().deadline <= now) {
            auto e = heap_.top();
            heap_.pop();
            auto lag = std::chrono::duration_cast<std::chrono::seconds>(now - e.deadline);
            if (e.skippable && lag > skip_threshold) {
                continue;
            }
            out.push_back(std::move(e));
        }
        return out;
    }

    [[nodiscard]] bool empty() const {
        std::lock_guard lk(mtx_);
        return heap_.empty();
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mtx_);
        return heap_.size();
    }

    void notify() {
        cv_.notify_one();
    }

    void wait_until_change(std::unique_lock<std::mutex>& lk) {
        cv_.wait(lk);
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::priority_queue<TimerEntry, std::vector<TimerEntry>, std::greater<TimerEntry>> heap_;
    std::uint64_t next_seq_{0};
};

}  // namespace tts::framework