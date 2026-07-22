#pragma once

#include <memory>

#include "local_tts_engine.h"

namespace tts::local {

class SherpaOnnxLocalTts final : public ILocalTtsEngine {
public:
    explicit SherpaOnnxLocalTts(LocalTtsConfig cfg);
    ~SherpaOnnxLocalTts() override;

    SherpaOnnxLocalTts(const SherpaOnnxLocalTts&) = delete;
    SherpaOnnxLocalTts& operator=(const SherpaOnnxLocalTts&) = delete;

    [[nodiscard]] bool IsReady() const override { return ready_; }
    LocalTtsResult Synthesize(const std::string& text,
                              const std::string& voice_id,
                              float speed) override;

    [[nodiscard]] int native_sample_rate() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    LocalTtsConfig cfg_;
    bool ready_{false};
};

}  // namespace tts::local