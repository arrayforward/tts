#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "framework/messages.h"

namespace tts::databoard {

enum class PendingState : std::uint8_t {
    AwaitingSession,
    Streaming,
    Complete,
    Failed,
    Cancelled,
};

[[nodiscard]] inline const char* to_string(PendingState s) {
    switch (s) {
        case PendingState::AwaitingSession: return "AwaitingSession";
        case PendingState::Streaming:       return "Streaming";
        case PendingState::Complete:        return "Complete";
        case PendingState::Failed:          return "Failed";
        case PendingState::Cancelled:       return "Cancelled";
    }
    return "Unknown";
}

struct PendingRequest {
    std::string request_id;
    std::string session_id;
    PendingState state{PendingState::AwaitingSession};
    std::vector<std::uint8_t> accumulated_audio;
    std::int64_t cumulative_bytes{0};
    int sample_rate{16000};
    int channels{1};
    framework::AudioFormat format{framework::AudioFormat::Mp3};
    int usage_characters{0};
    std::chrono::steady_clock::time_point created_at{};
    std::chrono::steady_clock::time_point last_chunk_at{};
    int status_code{0};
    std::string status_msg;
    bool final_received{false};
};

class PendingRequestStore {
public:
    void add(std::unique_ptr<PendingRequest> p) {
        std::lock_guard lk(mtx_);
        p->created_at = std::chrono::steady_clock::now();
        p->last_chunk_at = p->created_at;
        const std::string id = p->request_id;
        store_[id] = std::move(p);
    }

    PendingRequest* get(const std::string& id) {
        std::lock_guard lk(mtx_);
        auto it = store_.find(id);
        return (it != store_.end()) ? it->second.get() : nullptr;
    }

    void remove(const std::string& id) {
        std::lock_guard lk(mtx_);
        store_.erase(id);
    }

    void set_state(const std::string& id, PendingState s) {
        std::lock_guard lk(mtx_);
        auto it = store_.find(id);
        if (it == store_.end()) return;
        it->second->state = s;
    }

    void bind_session(const std::string& id, const std::string& session_id) {
        std::lock_guard lk(mtx_);
        auto it = store_.find(id);
        if (it == store_.end()) return;
        it->second->session_id = session_id;
        it->second->state = PendingState::Streaming;
    }

    bool append_chunk(const std::string& id, const std::uint8_t* data, std::size_t len,
                      bool is_final, int usage) {
        std::lock_guard lk(mtx_);
        auto it = store_.find(id);
        if (it == store_.end()) return false;
        auto& p = *it->second;
        p.accumulated_audio.insert(p.accumulated_audio.end(), data, data + len);
        p.cumulative_bytes += static_cast<std::int64_t>(len);
        p.usage_characters = usage;
        p.last_chunk_at = std::chrono::steady_clock::now();
        if (is_final) p.final_received = true;
        return true;
    }

    std::vector<std::string> find_ready_to_emit() {
        std::lock_guard lk(mtx_);
        std::vector<std::string> out;
        for (auto& [id, p] : store_) {
            if (p->state == PendingState::Streaming && p->final_received) {
                out.push_back(id);
            }
        }
        return out;
    }

    std::vector<std::string> snapshot_ids() const {
        std::lock_guard lk(mtx_);
        std::vector<std::string> out;
        for (auto& [id, _] : store_) out.push_back(id);
        return out;
    }

    std::vector<std::string> collect_by_session(const std::string& session_id) {
        std::lock_guard lk(mtx_);
        std::vector<std::string> out;
        for (auto& [id, p] : store_) {
            if (p->session_id == session_id) out.push_back(id);
        }
        return out;
    }

    std::vector<std::string> collect_terminal() {
        std::lock_guard lk(mtx_);
        std::vector<std::string> out;
        for (auto& [id, p] : store_) {
            if (p->state == PendingState::Failed ||
                p->state == PendingState::Cancelled) {
                out.push_back(id);
            }
        }
        return out;
    }

    std::size_t size() const {
        std::lock_guard lk(mtx_);
        return store_.size();
    }

private:
    mutable std::mutex mtx_;
    std::unordered_map<std::string, std::unique_ptr<PendingRequest>> store_;
};

}  // namespace tts::databoard