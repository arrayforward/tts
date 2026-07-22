#include "mock_local_tts.h"

#include <cmath>
#include <cstdint>
#include <thread>

#include <spdlog/spdlog.h>

namespace tts::local {

MockLocalTts::MockLocalTts(LocalTtsConfig cfg) : cfg_(std::move(cfg)) {
    if (cfg_.target_sample_rate <= 0) cfg_.target_sample_rate = 16000;
    if (cfg_.target_channels <= 0) cfg_.target_channels = 1;
}

LocalTtsResult MockLocalTts::Synthesize(const std::string& text,
                                        const std::string& voice_id,
                                        float speed) {
    spdlog::info("mock_synth: ENTER text_len={} sr={} ch={}",
                 text.size(), cfg_.target_sample_rate, cfg_.target_channels);
    LocalTtsResult r;
    r.sample_rate = cfg_.target_sample_rate;
    r.channels = cfg_.target_channels;
    r.status_code = 0;
    r.status_msg = "ok_mock";

    double base_duration_sec = 0.5;
    if (!text.empty()) {
        base_duration_sec = std::max(0.1, static_cast<double>(text.size()) / 16.0);
    }
    if (speed <= 0.0f) speed = 1.0f;
    double actual_duration = base_duration_sec / static_cast<double>(speed);
    std::size_t total = static_cast<std::size_t>(actual_duration * static_cast<double>(r.sample_rate));
    if (total < r.sample_rate / 4) total = r.sample_rate / 4;
    if (total > r.sample_rate * 30) total = r.sample_rate * 30;
    spdlog::info("mock_synth: total={}", total);

    double voice_offset = 0.0;
    for (char c : voice_id) voice_offset += static_cast<double>(c);
    for (char c : text) voice_offset += static_cast<double>(static_cast<unsigned char>(c));
    double base_freq = 180.0 + std::fmod(voice_offset, 240.0);

    r.samples.clear();
    r.samples.resize(total, 0);
    for (std::size_t i = 0; i < total; ++i) {
        double t = static_cast<double>(i) / static_cast<double>(r.sample_rate);
        double env = std::exp(-2.0 * std::fmod(t, 0.25));
        double phase = 2.0 * 3.14159265358979323846 * base_freq * t;
        double s = 0.45 * env * std::sin(phase);
        if (s > 1.0) s = 1.0;
        if (s < -1.0) s = -1.0;
        r.samples[i] = static_cast<std::int16_t>(std::lrint(s * 32767.0));
    }
    r.duration_ms = static_cast<int>(actual_duration * 1000.0);
    spdlog::info("mock_synth done total={}", total);
    return r;
}

}  // namespace tts::local