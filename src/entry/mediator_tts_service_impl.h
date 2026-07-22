#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <grpcpp/server_context.h>
#include <spdlog/spdlog.h>

#include "local/local_tts_engine.h"
#include "pb2/mediator_tts.grpc.pb.h"

namespace tts::entry {

// Mediator 网关协议适配：mediator.TtsService/Synth（一元）。
// 复用本地合成引擎（与 SynthesizeStream 同一条引擎路径），
// 将全部 PCM chunk 拼成一整段 16kHz mono int16 一次性返回。
class MediatorTtsServiceImpl final : public ::mediator::TtsService::Service {
public:
    explicit MediatorTtsServiceImpl(std::shared_ptr<local::ILocalTtsEngine> engine)
        : engine_(std::move(engine)) {}

    ::grpc::Status Synth(::grpc::ServerContext* context,
                         const ::mediator::TtsRequest* request,
                         ::mediator::TtsResponse* response) override {
        (void)context;
        spdlog::info("mediator Synth session_id={} clip_id={} text_len={}",
                     request->session_id(), request->clip_id(), request->text().size());
        if (!engine_ || !engine_->IsReady()) {
            return ::grpc::Status(::grpc::StatusCode::UNAVAILABLE, "local tts engine not ready");
        }
        auto r = engine_->Synthesize(request->text());
        if (r.status_code != 0) {
            spdlog::warn("mediator Synth failed status={} msg={}", r.status_code, r.status_msg);
            return ::grpc::Status(::grpc::StatusCode::INTERNAL,
                                  "synthesis failed: " + r.status_msg);
        }
        response->mutable_pcm()->assign(
            reinterpret_cast<const char*>(r.samples.data()),
            r.samples.size() * sizeof(std::int16_t));
        spdlog::info("mediator Synth done session_id={} clip_id={} samples={} bytes={} sr={} ch={}",
                     request->session_id(), request->clip_id(), r.samples.size(),
                     response->pcm().size(), r.sample_rate, r.channels);
        return ::grpc::Status::OK;
    }

private:
    std::shared_ptr<local::ILocalTtsEngine> engine_;
};

}  // namespace tts::entry
