#include "ws_frame_codec.h"

#include <nlohmann/json.hpp>

namespace tts::upstream {

using json = nlohmann::json;

std::string WsFrameCodec::encode_task_start(const std::string& model,
                                            const framework::VoiceSettings& v,
                                            const framework::AudioSettings& a) const {
    json j;
    j["event"] = "task_start";
    j["model"] = model;

    json vs;
    vs["voice_id"] = v.voice_id;
    vs["speed"] = v.speed;
    vs["vol"] = v.vol;
    vs["pitch"] = v.pitch;
    if (v.emotion) vs["emotion"] = *v.emotion;
    vs["english_normalization"] = v.english_normalization;
    j["voice_setting"] = vs;

    json as;
    as["sample_rate"] = a.sample_rate;
    as["bitrate"] = a.bitrate;
    as["format"] = std::string(framework::to_string(a.format));
    as["channel"] = a.channel;
    j["audio_setting"] = as;

    return j.dump();
}

std::string WsFrameCodec::encode_task_continue(const std::string& text) const {
    json j;
    j["event"] = "task_continue";
    j["text"] = text;
    return j.dump();
}

std::string WsFrameCodec::encode_task_finish() const {
    json j;
    j["event"] = "task_finish";
    return j.dump();
}

std::optional<WsFrameCodec::ParsedServerFrame>
WsFrameCodec::parse_server_frame(std::string_view raw) const {
    json j;
    try {
        j = json::parse(raw);
    } catch (...) {
        return std::nullopt;
    }
    ParsedServerFrame p;
    if (j.contains("event") && j["event"].is_string()) p.event = j["event"].get<std::string>();
    if (j.contains("session_id") && j["session_id"].is_string())
        p.session_id = j["session_id"].get<std::string>();
    if (j.contains("trace_id") && j["trace_id"].is_string())
        p.trace_id = j["trace_id"].get<std::string>();
    if (j.contains("is_final") && j["is_final"].is_boolean())
        p.is_final = j["is_final"].get<bool>();
    if (j.contains("data") && j["data"].is_object()) {
        const auto& d = j["data"];
        if (d.contains("audio") && d["audio"].is_string())
            p.audio_hex = d["audio"].get<std::string>();
    }
    if (j.contains("extra_info") && j["extra_info"].is_object()) {
        const auto& e = j["extra_info"];
        if (e.contains("audio_sample_rate") && e["audio_sample_rate"].is_number_integer())
            p.sample_rate = e["audio_sample_rate"].get<int>();
        if (e.contains("audio_channel") && e["audio_channel"].is_number_integer())
            p.channels = e["audio_channel"].get<int>();
        if (e.contains("usage_characters") && e["usage_characters"].is_number_integer())
            p.usage_characters = e["usage_characters"].get<int>();
    }
    if (j.contains("base_resp") && j["base_resp"].is_object()) {
        const auto& b = j["base_resp"];
        if (b.contains("status_code") && b["status_code"].is_number_integer())
            p.status_code = b["status_code"].get<int>();
        if (b.contains("status_msg") && b["status_msg"].is_string())
            p.status_msg = b["status_msg"].get<std::string>();
    }
    return p;
}

std::string WsFrameCodec::default_voice_id() {
    return "male-qn-qingse";
}

}  // namespace tts::upstream