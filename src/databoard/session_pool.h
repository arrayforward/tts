#pragma once

#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "framework/clock.h"

#include "framework/messages.h"

namespace tts::databoard {

enum class SessionState : std::uint8_t {
    Cold,
    Connecting,
    Ready,
    Streaming,
    Draining,
    Closed,
    Errored,
};

[[nodiscard]] inline const char* to_string(SessionState s) {
    switch (s) {
        case SessionState::Cold:       return "Cold";
        case SessionState::Connecting: return "Connecting";
        case SessionState::Ready:      return "Ready";
        case SessionState::Streaming:  return "Streaming";
        case SessionState::Draining:   return "Draining";
        case SessionState::Closed:     return "Closed";
        case SessionState::Errored:    return "Errored";
    }
    return "Unknown";
}

class SessionRecord {
public:
    std::string session_id;
    SessionState state{SessionState::Cold};
    framework::TimePoint created_at{};
    framework::TimePoint last_active_at{};
    std::string current_request_id;
    std::string last_error;
};

class SessionPool {
public:
    SessionPool(std::size_t max_size,
                std::chrono::seconds idle_timeout,
                std::chrono::seconds max_lifetime)
        : max_size_(max_size), idle_timeout_(idle_timeout), max_lifetime_(max_lifetime) {}

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mtx_);
        return sessions_.size();
    }
    [[nodiscard]] std::size_t idle_count() const {
        std::lock_guard lk(mtx_);
        return idle_count_unsafe();
    }
    [[nodiscard]] std::size_t active_count() const {
        std::lock_guard lk(mtx_);
        return sessions_.size() - idle_count_unsafe();
    }
    [[nodiscard]] std::size_t max_size() const noexcept { return max_size_; }
    [[nodiscard]] std::chrono::seconds idle_timeout() const noexcept { return idle_timeout_; }

    bool add(std::unique_ptr<SessionRecord> s) {
        std::lock_guard lk(mtx_);
        if (sessions_.size() >= max_size_) return false;
        const std::string id = s->session_id;
        lru_.push_front(id);
        sessions_[id] = std::move(s);
        return true;
    }

    void touch_active(const std::string& id, framework::TimePoint now) {
        std::lock_guard lk(mtx_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;
        it->second->last_active_at = now;
        move_to_front_locked(id);
    }

    SessionRecord* get(const std::string& id) {
        std::lock_guard lk(mtx_);
        auto it = sessions_.find(id);
        return (it != sessions_.end()) ? it->second.get() : nullptr;
    }

    [[nodiscard]] SessionRecord* find_idle_least_recently_used() {
        std::lock_guard lk(mtx_);
        for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
            auto sit = sessions_.find(*it);
            if (sit == sessions_.end()) continue;
            if (sit->second->state == SessionState::Ready) {
                return sit->second.get();
            }
        }
        return nullptr;
    }

    void mark_active(const std::string& id) {
        std::lock_guard lk(mtx_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;
        it->second->state = SessionState::Streaming;
        it->second->last_active_at = std::chrono::steady_clock::now();
        move_to_front_locked(id);
    }

    void mark_ready(const std::string& id) {
        std::lock_guard lk(mtx_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;
        it->second->state = SessionState::Ready;
        it->second->last_active_at = std::chrono::steady_clock::now();
        move_to_front_locked(id);
    }

    void mark_draining(const std::string& id) {
        std::lock_guard lk(mtx_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;
        it->second->state = SessionState::Draining;
    }

    void mark_idle(const std::string& id) {
        std::lock_guard lk(mtx_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;
        it->second->state = SessionState::Ready;
        it->second->current_request_id.clear();
        it->second->last_active_at = std::chrono::steady_clock::now();
        move_to_front_locked(id);
    }

    void mark_errored(const std::string& id, const std::string& reason) {
        std::lock_guard lk(mtx_);
        auto it = sessions_.find(id);
        if (it == sessions_.end()) return;
        it->second->state = SessionState::Errored;
        it->second->last_error = reason;
    }

    void mark_closed(const std::string& id) {
        std::lock_guard lk(mtx_);
        remove_locked(id);
    }

    void remove_locked(const std::string& id) {
        sessions_.erase(id);
        lru_.remove(id);
    }

    std::vector<std::string> collect_idle_expired(std::chrono::steady_clock::time_point now) {
        std::vector<std::string> out;
        std::lock_guard lk(mtx_);
        for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
            auto sit = sessions_.find(*it);
            if (sit == sessions_.end()) continue;
            if (sit->second->state == SessionState::Ready) {
                if (now - sit->second->last_active_at >= idle_timeout_) {
                    out.push_back(*it);
                }
            }
        }
        return out;
    }

    std::vector<std::string> collect_lifetime_expired(std::chrono::steady_clock::time_point now) {
        std::vector<std::string> out;
        std::lock_guard lk(mtx_);
        for (auto& [id, rec] : sessions_) {
            if (now - rec->created_at >= max_lifetime_) out.push_back(id);
        }
        return out;
    }

    std::vector<std::string> snapshot_ids() const {
        std::lock_guard lk(mtx_);
        std::vector<std::string> out;
        out.reserve(sessions_.size());
        for (auto& [id, _] : sessions_) out.push_back(id);
        return out;
    }

private:
    void move_to_front_locked(const std::string& id) {
        lru_.remove(id);
        lru_.push_front(id);
    }

    std::size_t idle_count_unsafe() const {
        std::size_t n = 0;
        for (auto& [_, rec] : sessions_) {
            if (rec->state == SessionState::Ready) ++n;
        }
        return n;
    }

    mutable std::mutex mtx_;
    std::size_t max_size_;
    std::chrono::seconds idle_timeout_;
    std::chrono::seconds max_lifetime_;
    std::unordered_map<std::string, std::unique_ptr<SessionRecord>> sessions_;
    std::list<std::string> lru_;
};

}  // namespace tts::databoard