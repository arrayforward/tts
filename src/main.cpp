#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <memory>
#include <thread>

#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "config.h"
#include "databoard/data_board.h"
#include "entry/jwt_auth_interceptor.h"
#include "entry/request_parser.h"
#include "entry/tts_service_impl.h"
#include "framework/clock.h"
#include "framework/copy_channel.h"
#include "framework/event_bus.h"
#include "framework/messages.h"
#include "framework/reactor.h"
#include "heartbeat/backpressure_controller.h"
#include "heartbeat/changeset_builder.h"
#include "heartbeat/evolution_engine.h"
#include "heartbeat/heartbeat.h"
#include "heartbeat/message_processor.h"
#include "local/local_tts_engine.h"
#include "observability/admission_gate.h"
#include "observability/event_logger.h"
#include "observability/metrics_collector.h"
#include "upstream/ws_frame_codec.h"

namespace {
std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop.store(true); }
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    auto console = spdlog::stdout_color_mt("tts");
    spdlog::set_default_logger(console);
    spdlog::set_pattern("{\"ts\":\"%Y-%m-%dT%H:%M:%S.%e%z\",\"lvl\":\"%l\",\"name\":\"%n\",\"msg\":%v}");
    spdlog::set_level(spdlog::level::info);
    if (std::getenv("GRPC_TRACE") || std::getenv("GRPC_VERBOSITY")) {
        spdlog::set_level(spdlog::level::debug);
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    tts::Config cfg;
    try {
        cfg = tts::Config::from_env();
        cfg.validate();
    } catch (const std::exception& e) {
        spdlog::critical("config invalid: {}", e.what());
        return 1;
    }
    spdlog::info("starting tts_server bind={} local.enabled={} local.prefer={} local.model_dir={}",
                 cfg.grpc_bind, cfg.local.enabled, cfg.local.prefer_local,
                 cfg.local.model_dir);
    
    auto clock = std::make_shared<tts::framework::SystemClock>();
    auto bus = std::make_shared<tts::framework::EventBus>();
    bus->start_workers(cfg.bus_workers);

    auto board = std::make_shared<tts::databoard::DataBoard>();

    auto reactor = std::make_shared<tts::framework::Reactor>(clock, cfg.cpu_workers, cfg.io_workers);

    auto request_channel = std::make_shared<tts::framework::CopyChannel<tts::framework::Message>>();
    auto ws_event_channel = std::make_shared<tts::framework::CopyChannel<tts::framework::Message>>();

    auto codec = std::make_shared<tts::upstream::WsFrameCodec>();

    tts::local::LocalTtsConfig lcfg;
    lcfg.enabled = cfg.local.enabled;
    lcfg.model_dir = cfg.local.model_dir;
    lcfg.model_file = cfg.local.model_file;
    lcfg.tokenizer_file = cfg.local.tokenizer_file;
    lcfg.detokenizer_file = cfg.local.detokenizer_file;
    lcfg.target_sample_rate = cfg.local.target_sample_rate;
    lcfg.target_channels = cfg.local.target_channels;
    lcfg.num_threads = cfg.local.num_threads;
    lcfg.max_decode_steps = cfg.local.max_decode_steps;
    lcfg.backend = (cfg.local.backend == "mock") ? tts::local::BackendKind::Mock
                                                  : tts::local::BackendKind::Onnx;
    std::shared_ptr<tts::local::ILocalTtsEngine> local_engine_shared(
        tts::local::CreateLocalTts(lcfg).release());

    auto processor = std::make_shared<tts::heartbeat::MessageProcessor>(
        board, request_channel, ws_event_channel);
    auto evolution = std::make_shared<tts::heartbeat::DataEvolutionEngine>(board, clock);
    auto changeset = std::make_shared<tts::heartbeat::ChangeSetBuilder>(bus);
    auto backpressure = std::make_shared<tts::heartbeat::BackpressureController>(board);
    auto heartbeat = std::make_shared<tts::heartbeat::Heartbeat>(
        board, clock, bus, processor, evolution, changeset, backpressure, codec,
        local_engine_shared,
        cfg.local.prefer_local);

    tts::observability::MetricsCollector metrics_collector(board, bus);
    tts::observability::EventLogger event_logger(bus);
    tts::observability::AdmissionGate admission_gate(board, bus);

    std::unique_ptr<tts::entry::JwtVerifier> jwt;
    if (cfg.auth_enabled) {
        jwt = tts::entry::JwtVerifier::from_rsa256_public_key_file(cfg.jwt_public_key_file);
        if (!jwt) {
            spdlog::critical("failed to load JWT public key from {}", cfg.jwt_public_key_file.string());
            return 2;
        }
    } else {
        spdlog::warn("GRPC_AUTH_ENABLED=false: JWT verification disabled (intranet mode)");
    }
    auto parser = std::make_shared<tts::entry::RequestParser>();
    auto svc = std::make_shared<tts::entry::TtsServiceImpl>(
        parser, request_channel, std::move(jwt), board);

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();

    ::grpc::ServerBuilder builder;
    builder.AddListeningPort(cfg.grpc_bind, ::grpc::InsecureServerCredentials());
    builder.RegisterService(svc.get());
    std::unique_ptr<::grpc::Server> server(builder.BuildAndStart());
    if (!server) {
        spdlog::critical("failed to build gRPC server");
        return 3;
    }
    spdlog::info("gRPC listening on {}", cfg.grpc_bind);

    std::thread server_wait_thread([server_ptr = server.get()]() {
        server_ptr->Wait();
    });

    std::thread heartbeat_thread([heartbeat, interval = cfg.heartbeat_interval, svc, reactor, request_channel]() {
        while (!g_stop.load()) {
            auto cs = heartbeat->tick();
            if (!cs.client_writes.empty()) {
                svc->dispatch_to_writers(cs.client_writes);
            }
            for (auto& job : cs.local_jobs) {
                auto* engine = heartbeat->local_engine();
                if (!engine) continue;
                spdlog::info("local_synth rid={} text_len={}", job.request_id, job.text.size());
                auto r = engine->Synthesize(job.text, job.voice_id, job.speed);
                spdlog::info("local_synth rid={} done status={} samples={}",
                             job.request_id, r.status_code, r.samples.size());
                if (r.status_code != 0) {
                    tts::framework::LocalSynthFailedMsg m;
                    m.request_id = job.request_id;
                    m.status_code = r.status_code;
                    m.status_msg = r.status_msg;
                    request_channel->send(tts::framework::Message{m});
                } else {
                    tts::framework::LocalSynthCompleteMsg m;
                    m.request_id = job.request_id;
                    m.audio.assign(reinterpret_cast<const std::uint8_t*>(r.samples.data()),
                                   reinterpret_cast<const std::uint8_t*>(r.samples.data() + r.samples.size()));
                    m.sample_rate = r.sample_rate;
                    m.channels = r.channels;
                    m.format = tts::framework::AudioFormat::Pcm;
                    m.usage_ms = r.duration_ms;
                    m.status_code = r.status_code;
                    m.status_msg = r.status_msg;
                    request_channel->send(tts::framework::Message{m});
                }
            }
            std::this_thread::sleep_for(interval);
        }
    });

    reactor->run([]() {});

    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    spdlog::info("shutting down...");
    server->Shutdown();
    reactor->stop();
    if (server_wait_thread.joinable()) server_wait_thread.join();
    if (heartbeat_thread.joinable()) heartbeat_thread.join();
    bus->stop_workers();
    spdlog::info("bye");
    return 0;
}