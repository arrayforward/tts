#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace tts::databoard {

class MetricsSnapshot {
public:
    void inc_inflight(std::int64_t v = 1)  { inflight_.fetch_add(v, std::memory_order_relaxed); }
    void dec_inflight(std::int64_t v = 1)  { inflight_.fetch_sub(v, std::memory_order_relaxed); }
    void set_queued(std::uint64_t v)       { queued_.store(v, std::memory_order_relaxed); }
    void inc_upstream_connected()          { upstream_connected_.fetch_add(1, std::memory_order_relaxed); }
    void inc_upstream_disconnected()       { upstream_disconnected_.fetch_add(1, std::memory_order_relaxed); }
    void add_audio_bytes(std::uint64_t v)  { audio_bytes_.fetch_add(v, std::memory_order_relaxed); }
    void inc_failed_requests()             { failed_requests_.fetch_add(1, std::memory_order_relaxed); }
    void set_pool_idle(std::uint64_t v)    { pool_idle_.store(v, std::memory_order_relaxed); }
    void set_pool_active(std::uint64_t v)  { pool_active_.store(v, std::memory_order_relaxed); }
    void set_backpressure_level(std::uint64_t v) { backpressure_.store(v, std::memory_order_relaxed); }
    void inc_heartbeat()                   { heartbeat_count_.fetch_add(1, std::memory_order_relaxed); }
    void record_heartbeat_latency_us(std::uint64_t us) {
        std::lock_guard lk(hb_mtx_);
        hb_samples_.push_back(us);
        if (hb_samples_.size() > 256) hb_samples_.erase(hb_samples_.begin());
    }
    void set_bus_pending(std::uint64_t v)   { bus_pending_.store(v, std::memory_order_relaxed); }

    [[nodiscard]] std::uint64_t inflight() const noexcept          { return inflight_.load(); }
    [[nodiscard]] std::uint64_t queued() const noexcept            { return queued_.load(); }
    [[nodiscard]] std::uint64_t upstream_connected() const noexcept { return upstream_connected_.load(); }
    [[nodiscard]] std::uint64_t upstream_disconnected() const noexcept { return upstream_disconnected_.load(); }
    [[nodiscard]] std::uint64_t audio_bytes() const noexcept       { return audio_bytes_.load(); }
    [[nodiscard]] std::uint64_t failed_requests() const noexcept   { return failed_requests_.load(); }
    [[nodiscard]] std::uint64_t pool_idle() const noexcept         { return pool_idle_.load(); }
    [[nodiscard]] std::uint64_t pool_active() const noexcept       { return pool_active_.load(); }
    [[nodiscard]] std::uint64_t backpressure_level() const noexcept { return backpressure_.load(); }
    [[nodiscard]] std::uint64_t heartbeat_count() const noexcept   { return heartbeat_count_.load(); }
    [[nodiscard]] std::uint64_t bus_pending() const noexcept        { return bus_pending_.load(); }

    [[nodiscard]] std::uint64_t heartbeat_p99_us() const {
        std::lock_guard lk(hb_mtx_);
        if (hb_samples_.empty()) return 0;
        std::vector<std::uint64_t> v(hb_samples_.begin(), hb_samples_.end());
        std::sort(v.begin(), v.end());
        std::size_t idx = static_cast<std::size_t>(v.size() * 0.99);
        if (idx >= v.size()) idx = v.size() - 1;
        return v[idx];
    }

private:
    std::atomic<std::uint64_t> inflight_{0};
    std::atomic<std::uint64_t> queued_{0};
    std::atomic<std::uint64_t> upstream_connected_{0};
    std::atomic<std::uint64_t> upstream_disconnected_{0};
    std::atomic<std::uint64_t> audio_bytes_{0};
    std::atomic<std::uint64_t> failed_requests_{0};
    std::atomic<std::uint64_t> pool_idle_{0};
    std::atomic<std::uint64_t> pool_active_{0};
    std::atomic<std::uint64_t> backpressure_{0};
    std::atomic<std::uint64_t> heartbeat_count_{0};
    std::atomic<std::uint64_t> bus_pending_{0};

    mutable std::mutex hb_mtx_;
    std::vector<std::uint64_t> hb_samples_;
};

}  // namespace tts::databoard