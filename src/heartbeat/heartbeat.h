#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "databoard/data_board.h"
#include "framework/change_set.h"
#include "framework/clock.h"
#include "framework/copy_channel.h"
#include "framework/event_bus.h"
#include "framework/messages.h"
#include "local/local_tts_engine.h"
#include "upstream/upstream_client.h"
#include "upstream/minimax_protocol_adapter.h"
#include "upstream/ws_frame_codec.h"
#include "backpressure_controller.h"
#include "changeset_builder.h"
#include "evolution_engine.h"
#include "heartbeat_events.h"
#include "message_processor.h"

namespace tts::heartbeat {

struct HeartbeatStats {
    std::uint64_t heartbeat_count{0};
    std::uint64_t last_duration_us{0};
};

class Heartbeat {
public:
    Heartbeat(std::shared_ptr<databoard::DataBoard> board,
              std::shared_ptr<framework::IClock> clock,
              std::shared_ptr<framework::EventBus> bus,
              std::shared_ptr<MessageProcessor> processor,
              std::shared_ptr<DataEvolutionEngine> evolution,
              std::shared_ptr<ChangeSetBuilder> changeset,
              std::shared_ptr<BackpressureController> backpressure,
              std::shared_ptr<upstream::WsFrameCodec> codec,
              std::shared_ptr<local::ILocalTtsEngine> local_engine,
              bool prefer_local)
        : board_(std::move(board)),
          clock_(std::move(clock)),
          bus_(std::move(bus)),
          processor_(std::move(processor)),
          evolution_(std::move(evolution)),
          changeset_(std::move(changeset)),
          backpressure_(std::move(backpressure)),
          codec_(std::move(codec)),
          local_engine_(std::move(local_engine)),
          prefer_local_(prefer_local) {
    }

    HeartbeatStats stats() const {
        HeartbeatStats s;
        s.heartbeat_count = count_.load();
        s.last_duration_us = last_duration_us_.load();
        return s;
    }

    [[nodiscard]] local::ILocalTtsEngine* local_engine() const noexcept {
        return local_engine_.get();
    }

    framework::ChangeSet run_once_for_test() {
        return tick();
    }

    framework::ChangeSet tick() {
        auto t0 = clock_->now();

        auto batch = processor_->drain_batch();
        std::set<std::string> changed_sessions;
        std::set<std::string> changed_requests;

        for (auto& m : batch) {
            std::visit([&](auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, framework::TtsRequestMsg>) {
                    handle_request(v, changed_sessions, changed_requests);
                } else if constexpr (std::is_same_v<T, framework::TtsResponseMsg>) {
                    handle_response(v, changed_sessions, changed_requests);
                } else if constexpr (std::is_same_v<T, framework::CancelRequestMsg>) {
                    handle_cancel(v, changed_sessions, changed_requests);
                } else if constexpr (std::is_same_v<T, framework::LocalSynthCompleteMsg>) {
                    handle_local_complete(v);
                } else if constexpr (std::is_same_v<T, framework::LocalSynthFailedMsg>) {
                    handle_local_failed(v, changed_requests);
                }
            }, m);
        }

        auto ready = board_->pending()->find_ready_to_emit();
        for (const auto& rid : ready) emit_completed(rid, changed_sessions, changed_requests);

        auto closes = evolution_->evolve({changed_sessions, changed_requests});
        for (auto& c : closes) changeset_->add_close(c);

        auto cs = changeset_->collect();

        auto t1 = clock_->now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        last_duration_us_.store(static_cast<std::uint64_t>(us));
        count_.fetch_add(1, std::memory_order_relaxed);
        board_->metrics()->inc_heartbeat();
        board_->metrics()->record_heartbeat_latency_us(static_cast<std::uint64_t>(us));

        if (bus_) {
            HeartbeatTickEvent ev{static_cast<std::uint64_t>(us), batch.size(), cs.total()};
            bus_->publish_sync(ev);
        }
        return cs;
    }

    void handle_request(const framework::TtsRequestMsg& req,
                        std::set<std::string>& changed_sessions,
                        std::set<std::string>& changed_requests) {
        auto d = backpressure_->evaluate();
        if (!d.admit) {
            bus_->publish_async(upstream::WsFrameSendFailedEvent{
                req.request_id, "backpressure_admission_denied"});
            return;
        }

        const bool use_local = should_use_local(req);
        if (use_local) {
            auto* p = board_->pending()->get(req.request_id);
            if (!p) {
                auto np = std::make_unique<databoard::PendingRequest>();
                np->request_id = req.request_id;
                np->state = databoard::PendingState::Streaming;
                np->sample_rate = req.audio.sample_rate;
                np->channels = req.audio.channel;
                np->format = framework::AudioFormat::Pcm;
                board_->pending()->add(std::move(np));
            } else {
                p->sample_rate = req.audio.sample_rate;
                p->channels = req.audio.channel;
                p->format = framework::AudioFormat::Pcm;
                p->state = databoard::PendingState::Streaming;
            }
            board_->metrics()->inc_inflight();
            changed_requests.insert(req.request_id);

            framework::LocalSynthJob job;
            job.request_id = req.request_id;
            job.text = req.text;
            job.voice_id = req.voice.voice_id;
            job.speed = req.voice.speed > 0 ? req.voice.speed : 1.0f;
            changeset_->add_local_job(std::move(job));
            return;
        }

        auto h = processor_->acquire_or_create_session(req, dummy_cs_);
        if (h.session_id.empty()) {
            bus_->publish_async(upstream::WsFrameSendFailedEvent{
                req.request_id, "no_session_available"});
            return;
        }
        processor_->bind_request(req.request_id, h, req);
        changed_sessions.insert(h.session_id);
        changed_requests.insert(req.request_id);

        if (auto* s = board_->session_pool()->get(h.session_id)) {
            s->current_request_id = req.request_id;
        }

        if (!h.reused) {
            framework::OutWsFrame f;
            f.session_id = h.session_id;
            f.op = framework::OutWsOp::TaskStart;
            f.payload_json = codec_->encode_task_start(
                std::string(framework::to_string(req.model)), req.voice, req.audio);
            changeset_->add_ws_frame(std::move(f));
        }
        framework::OutWsFrame cont;
        cont.session_id = h.session_id;
        cont.op = framework::OutWsOp::TaskContinue;
        cont.payload_json = codec_->encode_task_continue(req.text);
        changeset_->add_ws_frame(std::move(cont));
    }

    void handle_local_complete(const framework::LocalSynthCompleteMsg& c) {
        framework::WriteToClient w;
        w.request_id = c.request_id;
        w.audio = c.audio;
        w.sample_rate = c.sample_rate;
        w.channels = c.channels;
        w.format = c.format;
        w.is_final = true;
        w.cumulative_bytes = static_cast<std::int64_t>(c.audio.size());
        w.audio_length_ms = c.usage_ms;
        w.status_code = c.status_code;
        w.status_msg = c.status_msg;
        changeset_->add_client_write(std::move(w));
        board_->metrics()->add_audio_bytes(c.audio.size());
        board_->metrics()->dec_inflight();
        board_->pending()->remove(c.request_id);
    }

    void handle_local_failed(const framework::LocalSynthFailedMsg& f,
                             std::set<std::string>& changed_requests) {
        framework::WriteToClient w;
        w.request_id = f.request_id;
        w.is_final = true;
        w.sample_rate = 16000;
        w.channels = 1;
        w.format = framework::AudioFormat::Pcm;
        w.status_code = f.status_code;
        w.status_msg = f.status_msg;
        changeset_->add_client_write(std::move(w));
        board_->metrics()->inc_failed_requests();
        board_->metrics()->dec_inflight();
        board_->pending()->remove(f.request_id);
        changed_requests.insert(f.request_id);
    }

    [[nodiscard]] bool should_use_local(const framework::TtsRequestMsg& req) const {
        if (!local_engine_ || !local_engine_->IsReady()) return false;
        if (req.backend == framework::Backend::Upstream) return false;
        if (req.backend == framework::Backend::Local) return true;
        return prefer_local_;
    }

    void handle_response(const framework::TtsResponseMsg& rsp,
                         std::set<std::string>& changed_sessions,
                         std::set<std::string>& changed_requests) {
        switch (rsp.kind) {
            case framework::WsEventKind::Connected: {
                board_->session_pool()->mark_ready(rsp.session_id);
                changed_sessions.insert(rsp.session_id);
                board_->metrics()->inc_upstream_connected();
                break;
            }
            case framework::WsEventKind::TaskStarted: {
                break;
            }
            case framework::WsEventKind::AudioChunk: {
                auto* s = board_->session_pool()->get(rsp.session_id);
                std::string rid = s ? s->current_request_id : std::string{};
                if (rid.empty()) break;
                auto bytes = upstream::hex_decode(rsp.audio_hex);
                board_->pending()->append_chunk(
                    rid, reinterpret_cast<const std::uint8_t*>(bytes.data()),
                    bytes.size(), rsp.is_final, rsp.usage_characters);
                changed_requests.insert(rid);
                break;
            }
            case framework::WsEventKind::TaskFinished: {
                changed_sessions.insert(rsp.session_id);
                break;
            }
            case framework::WsEventKind::TaskFailed: {
                auto ids = board_->pending()->collect_by_session(rsp.session_id);
                for (const auto& rid : ids) {
                    board_->pending()->set_state(rid, databoard::PendingState::Failed);
                    board_->pending()->get(rid)->status_code = rsp.status_code;
                    board_->pending()->get(rid)->status_msg = rsp.status_msg;
                    board_->metrics()->inc_failed_requests();
                    changed_requests.insert(rid);
                }
                board_->session_pool()->mark_errored(rsp.session_id, rsp.status_msg);
                changed_sessions.insert(rsp.session_id);
                break;
            }
            case framework::WsEventKind::Disconnected: {
                auto ids = board_->pending()->collect_by_session(rsp.session_id);
                for (const auto& rid : ids) {
                    board_->pending()->set_state(rid, databoard::PendingState::Failed);
                    changed_requests.insert(rid);
                }
                board_->session_pool()->mark_closed(rsp.session_id);
                changed_sessions.insert(rsp.session_id);
                break;
            }
        }
    }

    void handle_cancel(const framework::CancelRequestMsg& cancel,
                       std::set<std::string>&, std::set<std::string>& changed_requests) {
        board_->pending()->set_state(cancel.request_id, databoard::PendingState::Cancelled);
        changed_requests.insert(cancel.request_id);
    }

    void emit_completed(const std::string& rid,
                        std::set<std::string>& changed_sessions,
                        std::set<std::string>& changed_requests) {
        auto* p = board_->pending()->get(rid);
        if (!p) return;
        framework::WriteToClient w;
        w.request_id = rid;
        w.audio = std::move(p->accumulated_audio);
        w.sample_rate = p->sample_rate;
        w.channels = p->channels;
        w.format = p->format;
        w.is_final = p->final_received;
        w.cumulative_bytes = p->cumulative_bytes;
        w.audio_length_ms = static_cast<std::int64_t>(p->usage_characters);
        w.status_code = p->status_code;
        w.status_msg = p->status_msg;
        changeset_->add_client_write(std::move(w));
        board_->metrics()->add_audio_bytes(static_cast<std::uint64_t>(p->cumulative_bytes));
        board_->metrics()->dec_inflight();

        board_->session_pool()->mark_draining(p->session_id);
        changed_sessions.insert(p->session_id);
        changed_requests.insert(rid);
        board_->pending()->remove(rid);
    }

private:
    std::shared_ptr<databoard::DataBoard> board_;
    std::shared_ptr<framework::IClock> clock_;
    std::shared_ptr<framework::EventBus> bus_;
    std::shared_ptr<MessageProcessor> processor_;
    std::shared_ptr<DataEvolutionEngine> evolution_;
    std::shared_ptr<ChangeSetBuilder> changeset_;
    std::shared_ptr<BackpressureController> backpressure_;
    std::shared_ptr<upstream::WsFrameCodec> codec_;
    std::shared_ptr<local::ILocalTtsEngine> local_engine_;
    bool prefer_local_{true};

    framework::ChangeSet dummy_cs_;
    std::atomic<std::uint64_t> count_{0};
    std::atomic<std::uint64_t> last_duration_us_{0};
};

}  // namespace tts::heartbeat