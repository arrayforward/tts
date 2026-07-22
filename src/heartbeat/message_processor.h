#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "databoard/data_board.h"
#include "framework/change_set.h"
#include "framework/copy_channel.h"
#include "framework/messages.h"

namespace tts::heartbeat {

struct SessionHandle {
    std::string session_id;
    bool reused{false};
};

class MessageProcessor {
public:
    MessageProcessor(std::shared_ptr<databoard::DataBoard> board,
                     std::shared_ptr<framework::CopyChannel<framework::Message>> request_channel,
                     std::shared_ptr<framework::CopyChannel<framework::Message>> ws_event_channel)
        : board_(std::move(board)),
          request_channel_(std::move(request_channel)),
          ws_event_channel_(std::move(ws_event_channel)) {}

    std::vector<framework::Message> drain_batch() {
        std::vector<framework::Message> out;
        request_channel_->swap_out_all(out);
        std::vector<framework::Message> ws;
        ws_event_channel_->swap_out_all(ws);
        for (auto& m : ws) out.push_back(std::move(m));
        return out;
    }

    std::vector<framework::TtsRequestMsg> collect_new_requests(
        const std::vector<framework::Message>& batch) {
        std::vector<framework::TtsRequestMsg> out;
        for (const auto& m : batch) {
            if (auto* p = std::get_if<framework::TtsRequestMsg>(&m)) {
                out.push_back(*p);
            }
        }
        return out;
    }

    std::vector<framework::TtsResponseMsg> collect_responses(
        const std::vector<framework::Message>& batch) {
        std::vector<framework::TtsResponseMsg> out;
        for (const auto& m : batch) {
            if (auto* p = std::get_if<framework::TtsResponseMsg>(&m)) {
                out.push_back(std::move(*p));
            }
        }
        return out;
    }

    SessionHandle acquire_or_create_session(const framework::TtsRequestMsg& req,
                                            framework::ChangeSet& cs) {
        (void)cs;
        SessionHandle h;
        if (auto* idle = board_->session_pool()->find_idle_least_recently_used()) {
            h.session_id = idle->session_id;
            h.reused = true;
            board_->session_pool()->mark_active(h.session_id);
            return h;
        }
        h.session_id = generate_session_id();
        auto rec = std::make_unique<databoard::SessionRecord>();
        rec->session_id = h.session_id;
        rec->state = databoard::SessionState::Connecting;
        if (!board_->session_pool()->add(std::move(rec))) {
            h.session_id.clear();
        }
        return h;
    }

    void bind_request(const std::string& rid, const SessionHandle& h,
                      const framework::TtsRequestMsg& req) {
        auto p = std::make_unique<databoard::PendingRequest>();
        p->request_id = rid;
        p->session_id = h.session_id;
        p->state = databoard::PendingState::AwaitingSession;
        p->sample_rate = req.audio.sample_rate;
        p->channels = req.audio.channel;
        p->format = req.audio.format;
        board_->pending()->add(std::move(p));
        board_->metrics()->inc_inflight();
    }

    std::vector<databoard::PendingRequest> take_completed(const std::string& rid) {
        std::vector<databoard::PendingRequest> out;
        auto* p = board_->pending()->get(rid);
        if (!p) return out;
        databoard::PendingRequest copy;
        copy.request_id = p->request_id;
        copy.session_id = p->session_id;
        copy.accumulated_audio = p->accumulated_audio;
        copy.cumulative_bytes = p->cumulative_bytes;
        copy.sample_rate = p->sample_rate;
        copy.channels = p->channels;
        copy.format = p->format;
        copy.usage_characters = p->usage_characters;
        copy.status_code = p->status_code;
        copy.status_msg = p->status_msg;
        copy.final_received = p->final_received;
        out.push_back(std::move(copy));
        return out;
    }

private:
    std::string generate_session_id() {
        static thread_local std::mt19937_64 rng{std::random_device{}()};
        char buf[32];
        std::snprintf(buf, sizeof(buf), "s-%016llx",
                      static_cast<unsigned long long>(rng()));
        return std::string(buf);
    }

    std::shared_ptr<databoard::DataBoard> board_;
    std::shared_ptr<framework::CopyChannel<framework::Message>> request_channel_;
    std::shared_ptr<framework::CopyChannel<framework::Message>> ws_event_channel_;
};

}  // namespace tts::heartbeat