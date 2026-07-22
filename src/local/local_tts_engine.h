#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tts::local {

enum class BackendKind : std::uint8_t {
    Onnx,
    Mock,
};

struct LocalTtsConfig {
    bool enabled{true};
    BackendKind backend{BackendKind::Onnx};
    std::string model_dir;

    std::string model_file{"model.onnx"};
    std::string detokenizer_file{"detokenizer.onnx"};
    std::string tokenizer_file{"tokenizer.json"};

    int target_sample_rate{16000};
    int target_channels{1};

    int num_threads{0};
    int max_decode_steps{512};
    int eos_token_id{-1};
    int audio_token_offset{0};

    bool use_int8{false};
};

struct LocalTtsResult {
    std::vector<std::int16_t> samples;
    int sample_rate{16000};
    int channels{1};
    int duration_ms{0};
    int usage_ms{0};
    int status_code{0};
    std::string status_msg;
};

class ILocalTtsEngine {
public:
    virtual ~ILocalTtsEngine() = default;
    [[nodiscard]] virtual bool IsReady() const = 0;
    virtual LocalTtsResult Synthesize(const std::string& text,
                                      const std::string& voice_id = {},
                                      float speed = 1.0f) = 0;
};

std::unique_ptr<ILocalTtsEngine> CreateOnnxLocalTts(const LocalTtsConfig& cfg);
std::unique_ptr<ILocalTtsEngine> CreateMockLocalTts(const LocalTtsConfig& cfg);
std::unique_ptr<ILocalTtsEngine> CreateLocalTts(const LocalTtsConfig& cfg);

}  // namespace tts::local