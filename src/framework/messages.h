#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tts::framework {

enum class Model : std::uint8_t {
    Unspecified = 0,
    Speech28Hd = 1,
    Speech28Turbo = 2,
    Speech26Hd = 3,
    Speech26Turbo = 4,
    Speech02Hd = 5,
    Speech02Turbo = 6,
    Speech01Hd = 7,
    Speech01Turbo = 8,
};

[[nodiscard]] inline std::string_view to_string(Model m) {
    switch (m) {
        case Model::Speech28Hd:    return "speech-2.8-hd";
        case Model::Speech28Turbo: return "speech-2.8-turbo";
        case Model::Speech26Hd:    return "speech-2.6-hd";
        case Model::Speech26Turbo: return "speech-2.6-turbo";
        case Model::Speech02Hd:    return "speech-02-hd";
        case Model::Speech02Turbo: return "speech-02-turbo";
        case Model::Speech01Hd:    return "speech-01-hd";
        case Model::Speech01Turbo: return "speech-01-turbo";
        default:                   return "";
    }
}

[[nodiscard]] inline Model model_from_string(std::string_view s) {
    if (s == "speech-2.8-hd")    return Model::Speech28Hd;
    if (s == "speech-2.8-turbo") return Model::Speech28Turbo;
    if (s == "speech-2.6-hd")    return Model::Speech26Hd;
    if (s == "speech-2.6-turbo") return Model::Speech26Turbo;
    if (s == "speech-02-hd")    return Model::Speech02Hd;
    if (s == "speech-02-turbo") return Model::Speech02Turbo;
    if (s == "speech-01-hd")    return Model::Speech01Hd;
    if (s == "speech-01-turbo") return Model::Speech01Turbo;
    return Model::Unspecified;
}

enum class AudioFormat : std::uint8_t {
    Unspecified = 0,
    Mp3 = 1,
    Pcm = 2,
    Flac = 3,
    Wav = 4,
};

[[nodiscard]] inline std::string_view to_string(AudioFormat f) {
    switch (f) {
        case AudioFormat::Mp3:  return "mp3";
        case AudioFormat::Pcm:  return "pcm";
        case AudioFormat::Flac: return "flac";
        case AudioFormat::Wav:  return "wav";
        default:                return "";
    }
}

[[nodiscard]] inline AudioFormat format_from_string(std::string_view s) {
    if (s == "mp3")  return AudioFormat::Mp3;
    if (s == "pcm")  return AudioFormat::Pcm;
    if (s == "flac") return AudioFormat::Flac;
    if (s == "wav")  return AudioFormat::Wav;
    return AudioFormat::Unspecified;
}

enum class RequestKind : std::uint8_t {
    OpenStream,
    StreamChunk,
    FinishStream,
};

enum class Backend : std::uint8_t {
    Auto,
    Local,
    Upstream,
};

struct VoiceSettings {
    std::string voice_id{"male-qn-qingse"};
    float speed{1.0f};
    float vol{1.0f};
    int pitch{0};
    std::optional<std::string> emotion;
    bool english_normalization{false};
};

struct AudioSettings {
    int sample_rate{16000};
    int bitrate{128000};
    AudioFormat format{AudioFormat::Mp3};
    int channel{1};
};

struct TtsRequestMsg {
    std::string request_id;
    RequestKind kind{RequestKind::StreamChunk};
    std::string text;
    Model model{Model::Speech28Turbo};
    VoiceSettings voice;
    AudioSettings audio;
    Backend backend{Backend::Auto};
    std::chrono::steady_clock::time_point deadline{};

    [[nodiscard]] std::string_view kind_name() const noexcept { return "TtsRequestMsg"; }
};

enum class WsEventKind : std::uint8_t {
    Connected,
    TaskStarted,
    AudioChunk,
    TaskFinished,
    TaskFailed,
    Disconnected,
};

struct TtsResponseMsg {
    std::string session_id;
    std::string request_id;
    WsEventKind kind{WsEventKind::AudioChunk};
    std::string audio_hex;
    bool is_final{false};
    int sample_rate{0};
    int channels{0};
    int usage_characters{0};
    int status_code{0};
    std::string status_msg;
    std::string trace_id;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "TtsResponseMsg"; }
};

struct CancelRequestMsg {
    std::string request_id;
    std::string reason;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "CancelRequestMsg"; }
};

struct LocalSynthCompleteMsg {
    std::string request_id;
    std::string session_id;
    std::vector<std::uint8_t> audio;
    int sample_rate{16000};
    int channels{1};
    AudioFormat format{AudioFormat::Pcm};
    int usage_ms{0};
    int status_code{0};
    std::string status_msg;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "LocalSynthCompleteMsg"; }
};

struct LocalSynthFailedMsg {
    std::string request_id;
    std::string session_id;
    int status_code{0};
    std::string status_msg;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "LocalSynthFailedMsg"; }
};

using Message = std::variant<TtsRequestMsg, TtsResponseMsg, CancelRequestMsg,
                             LocalSynthCompleteMsg, LocalSynthFailedMsg>;

}  // namespace tts::framework