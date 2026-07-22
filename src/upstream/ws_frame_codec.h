#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "databoard/voice_catalog.h"
#include "framework/messages.h"

namespace tts::upstream {

class WsFrameCodec {
public:
    [[nodiscard]] std::string encode_task_start(const std::string& model,
                                                const framework::VoiceSettings& v,
                                                const framework::AudioSettings& a) const;

    [[nodiscard]] std::string encode_task_continue(const std::string& text) const;

    [[nodiscard]] std::string encode_task_finish() const;

    struct ParsedServerFrame {
        std::string event;
        std::optional<std::string> session_id;
        std::optional<std::string> trace_id;
        std::optional<std::string> audio_hex;
        std::optional<bool> is_final;
        std::optional<int> sample_rate;
        std::optional<int> channels;
        std::optional<int> usage_characters;
        int status_code{0};
        std::string status_msg;
    };

    [[nodiscard]] std::optional<ParsedServerFrame> parse_server_frame(std::string_view json) const;

    [[nodiscard]] static std::string default_voice_id();
};

}  // namespace tts::upstream