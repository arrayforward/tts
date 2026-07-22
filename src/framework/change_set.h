#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "messages.h"

namespace tts::framework {

enum class OutWsOp : std::uint8_t {
    TaskStart,
    TaskContinue,
    TaskFinish,
};

struct OutWsFrame {
    std::string session_id;
    OutWsOp op{OutWsOp::TaskContinue};
    std::string payload_json;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "OutWsFrame"; }
};

struct WriteToClient {
    std::string request_id;
    std::vector<std::uint8_t> audio;
    int sample_rate{16000};
    int channels{1};
    AudioFormat format{AudioFormat::Mp3};
    bool is_final{false};
    std::int64_t cumulative_bytes{0};
    std::int64_t audio_length_ms{0};
    int status_code{0};
    std::string status_msg;
    std::string trace_id;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "WriteToClient"; }
};

struct CloseSession {
    std::string session_id;
    std::string reason;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "CloseSession"; }
};

struct EmitMessage {
    Message msg;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "EmitMessage"; }
};

struct LocalSynthJob {
    std::string request_id;
    std::string text;
    std::string voice_id;
    float speed{1.0f};
    int session_dummy{0};

    [[nodiscard]] std::string_view kind_name() const noexcept { return "LocalSynthJob"; }
};

class ChangeSet {
public:
    std::vector<OutWsFrame> ws_frames;
    std::vector<WriteToClient> client_writes;
    std::vector<CloseSession> close_sessions;
    std::vector<EmitMessage> emit_messages;
    std::vector<LocalSynthJob> local_jobs;

    [[nodiscard]] bool empty() const noexcept {
        return ws_frames.empty() && client_writes.empty() &&
               close_sessions.empty() && emit_messages.empty() &&
               local_jobs.empty();
    }

    [[nodiscard]] std::size_t total() const noexcept {
        return ws_frames.size() + client_writes.size() +
               close_sessions.size() + emit_messages.size() +
               local_jobs.size();
    }
};

}  // namespace tts::framework