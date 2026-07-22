#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

#include <grpcpp/server_context.h>

#include "databoard/data_board.h"
#include "framework/copy_channel.h"
#include "framework/messages.h"
#include "grpc_response_writer.h"
#include "jwt_auth_interceptor.h"
#include "pb2/tts.grpc.pb.h"
#include "request_parser.h"

namespace tts::entry {

class TtsServiceImpl final : public ::tts::TTS::Service {
public:
    TtsServiceImpl(std::shared_ptr<RequestParser> parser,
                   std::shared_ptr<framework::CopyChannel<framework::Message>> request_channel,
                   std::unique_ptr<JwtVerifier> jwt,
                   std::shared_ptr<databoard::DataBoard> board)
        : parser_(std::move(parser)),
          request_channel_(std::move(request_channel)),
          jwt_(std::move(jwt)),
          board_(std::move(board)) {}

    ::grpc::Status SynthesizeStream(::grpc::ServerContext* context,
                                    const ::tts::SynthesizeRequest* request,
                                    ::grpc::ServerWriter< ::tts::SynthesizeResponse>* writer) override {
        if (!check_auth(context)) return unauth();
        const std::string rid = generate_request_id("strm");
        try {
            auto parsed = parser_->parse_unary(*request, rid);
            spdlog::info("SynthesizeStream rid={} text_len={} backend={}",
                         rid, parsed.request.text.size(), static_cast<int>(parsed.request.backend));
            request_channel_->send(std::move(parsed.request));
            auto local_queue = std::make_shared<framework::CopyChannel<framework::WriteToClient>>();
            {
                std::lock_guard lk(writers_mtx_);
                local_queues_[rid] = local_queue;
            }
            register_writer(rid, [writer](::tts::SynthesizeResponse* out) {
                return writer->Write(*out);
            });

            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (!context->IsCancelled()) {
                std::vector<framework::WriteToClient> drained;
                local_queue->swap_out_all(drained);
                if (!drained.empty()) {
                    spdlog::info("stream rid={} drained={}", rid, drained.size());
                    for (auto& w : drained) {
                        ::tts::SynthesizeResponse out;
                        GrpcResponseWriter::to_proto(w, &out);
                        if (!writer->Write(out)) {
                            spdlog::warn("stream rid={} write returned false", rid);
                            std::lock_guard lk(writers_mtx_);
                            writers_.erase(rid);
                            local_queues_.erase(rid);
                            return ::grpc::Status::OK;
                        }
                        if (w.is_final) {
                            std::lock_guard lk(writers_mtx_);
                            writers_.erase(rid);
                            local_queues_.erase(rid);
                            return ::grpc::Status::OK;
                        }
                    }
                }
                if (std::chrono::steady_clock::now() > deadline) {
                    spdlog::warn("stream rid={} deadline exceeded", rid);
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            {
                std::lock_guard lk(writers_mtx_);
                writers_.erase(rid);
                local_queues_.erase(rid);
            }
        } catch (const ParseError& e) {
            return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, e.what());
        }
        return ::grpc::Status::OK;
    }

    ::grpc::Status SynthesizeClient(::grpc::ServerContext* context,
                                    ::grpc::ServerReaderWriter< ::tts::SynthesizeResponse, ::tts::TextChunk>* stream) override {
        if (!check_auth(context)) return unauth();
        const std::string rid = generate_request_id("cli");
        register_writer(rid, [stream](::tts::SynthesizeResponse* out) {
            return stream->Write(*out);
        });
        ::tts::SynthesizeRequest initial_settings;
        bool first = true;
        ::tts::TextChunk chunk;
        while (stream->Read(&chunk)) {
            try {
                auto parsed = parser_->parse_chunk(chunk, rid, initial_settings, first);
                request_channel_->send(std::move(parsed.request));
                first = false;
            } catch (const ParseError& e) {
                return ::grpc::Status(::grpc::StatusCode::INVALID_ARGUMENT, e.what());
            }
        }
        return ::grpc::Status::OK;
    }

    ::grpc::Status GetMetrics(::grpc::ServerContext* context,
                              const ::google::protobuf::Empty*,
                              ::tts::MetricsSnapshot* response) override {
        if (!check_auth(context)) return unauth();
        auto m = board_->metrics();
        response->set_inflight_requests(m->inflight());
        response->set_queued_messages(m->queued());
        response->set_heartbeat_p99_us(m->heartbeat_p99_us());
        response->set_upstream_connected(m->upstream_connected());
        response->set_upstream_disconnected_total(m->upstream_disconnected());
        response->set_audio_bytes_emitted_total(m->audio_bytes());
        response->set_failed_requests_total(m->failed_requests());
        response->set_pool_idle_sessions(m->pool_idle());
        response->set_pool_active_sessions(m->pool_active());
        response->set_backpressure_level(m->backpressure_level());
        response->set_heartbeat_count(m->heartbeat_count());
        response->set_bus_worker_pending(m->bus_pending());
        return ::grpc::Status::OK;
    }

    void dispatch_to_writers(const std::vector<framework::WriteToClient>& writes) {
        for (const auto& w : writes) {
            std::shared_ptr<framework::CopyChannel<framework::WriteToClient>> q;
            WriteFn fn;
            {
                std::lock_guard lk(writers_mtx_);
                auto qit = local_queues_.find(w.request_id);
                if (qit != local_queues_.end()) q = qit->second;
                auto wit = writers_.find(w.request_id);
                if (wit != writers_.end()) fn = wit->second;
            }
            if (q) {
                q->send(w);
            } else if (fn) {
                ::tts::SynthesizeResponse out;
                GrpcResponseWriter::to_proto(w, &out);
                fn(&out);
                if (w.is_final) {
                    std::lock_guard lk(writers_mtx_);
                    writers_.erase(w.request_id);
                }
            }
        }
    }

private:
    using WriteFn = std::function<bool(::tts::SynthesizeResponse*)>;

    void register_writer(const std::string& rid, WriteFn fn) {
        std::lock_guard lk(writers_mtx_);
        writers_[rid] = std::move(fn);
    }

    bool check_auth(::grpc::ServerContext* ctx) {
        if (!jwt_) return true;
        auto md = ctx->client_metadata();
        auto it = md.find("authorization");
        if (it == md.end()) {
            spdlog::warn("auth: missing authorization header");
            return false;
        }
        std::string v(it->second.data(), it->second.size());
        const std::string prefix = "Bearer ";
        if (v.rfind(prefix, 0) != 0) {
            spdlog::warn("auth: bad prefix (got '{}')", v.substr(0, std::min<std::size_t>(20, v.size())));
            return false;
        }
        std::string token = v.substr(prefix.size());
        auto r = jwt_->verify(token, "tts-service");
        spdlog::info("auth: token_len={} result={}", token.size(), static_cast<int>(r));
        if (r != JwtVerifier::Result::Ok) return false;
        return true;
    }

    ::grpc::Status unauth() const {
        return ::grpc::Status(::grpc::StatusCode::UNAUTHENTICATED, "auth");
    }

    std::string generate_request_id(const std::string& tag) {
        static std::atomic<std::uint64_t> ctr{1};
        return tag + "-" + std::to_string(ctr.fetch_add(1, std::memory_order_relaxed));
    }

    std::shared_ptr<RequestParser> parser_;
    std::shared_ptr<framework::CopyChannel<framework::Message>> request_channel_;
    std::unique_ptr<JwtVerifier> jwt_;
    std::shared_ptr<databoard::DataBoard> board_;

    std::mutex writers_mtx_;
    std::unordered_map<std::string, WriteFn> writers_;
    std::unordered_map<std::string,
                       std::shared_ptr<framework::CopyChannel<framework::WriteToClient>>>
        local_queues_;
};

}  // namespace tts::entry