#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "framework/change_set.h"
#include "framework/event_bus.h"
#include "framework/messages.h"
#include "hex_codec.h"
#include "ws_frame_codec.h"

namespace tts::upstream {

struct UpstreamConnectedEvent {
    std::string session_id;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "UpstreamConnectedEvent"; }
};

struct UpstreamDisconnectedEvent {
    std::string session_id;
    int code{0};
    std::string reason;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "UpstreamDisconnectedEvent"; }
};

struct WsFrameDecodedEvent {
    std::string session_id;
    std::string request_id;
    std::string event_name;
    std::string audio_hex;
    bool is_final{false};
    int sample_rate{16000};
    int channels{1};
    int usage_characters{0};
    int status_code{0};
    std::string status_msg;
    std::string trace_id;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "WsFrameDecodedEvent"; }
};

struct WsFrameSendFailedEvent {
    std::string session_id;
    std::string error;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "WsFrameSendFailedEvent"; }
};

class MiniMaxProtocolAdapter {
public:
    MiniMaxProtocolAdapter() = default;

    std::vector<framework::TtsResponseMsg> translate(const WsFrameDecodedEvent& e) {
        std::vector<framework::TtsResponseMsg> out;
        if (e.event_name == "connected_success") {
            framework::TtsResponseMsg r;
            r.session_id = e.session_id;
            r.request_id = "";
            r.kind = framework::WsEventKind::Connected;
            out.push_back(std::move(r));
            return out;
        }
        if (e.event_name == "task_started") {
            framework::TtsResponseMsg r;
            r.session_id = e.session_id;
            r.kind = framework::WsEventKind::TaskStarted;
            out.push_back(std::move(r));
            return out;
        }
        if (e.event_name == "task_continued") {
            framework::TtsResponseMsg r;
            r.session_id = e.session_id;
            r.kind = framework::WsEventKind::AudioChunk;
            r.audio_hex = e.audio_hex;
            r.is_final = e.is_final;
            r.sample_rate = e.sample_rate;
            r.channels = e.channels;
            r.usage_characters = e.usage_characters;
            r.status_code = e.status_code;
            r.status_msg = e.status_msg;
            r.trace_id = e.trace_id;
            out.push_back(std::move(r));
            return out;
        }
        if (e.event_name == "task_finished") {
            framework::TtsResponseMsg r;
            r.session_id = e.session_id;
            r.kind = framework::WsEventKind::TaskFinished;
            r.status_code = e.status_code;
            r.status_msg = e.status_msg;
            out.push_back(std::move(r));
            return out;
        }
        if (e.event_name == "task_failed") {
            framework::TtsResponseMsg r;
            r.session_id = e.session_id;
            r.kind = framework::WsEventKind::TaskFailed;
            r.status_code = e.status_code;
            r.status_msg = e.status_msg;
            out.push_back(std::move(r));
            return out;
        }
        return out;
    }
};

}  // namespace tts::upstream