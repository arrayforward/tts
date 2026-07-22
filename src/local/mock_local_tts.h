#pragma once

#include "local_tts_engine.h"

namespace tts::local {

class MockLocalTts final : public ILocalTtsEngine {
public:
    explicit MockLocalTts(LocalTtsConfig cfg);
    [[nodiscard]] bool IsReady() const override { return ready_; }
    LocalTtsResult Synthesize(const std::string& text,
                              const std::string& voice_id,
                              float speed) override;

private:
    LocalTtsConfig cfg_;
    bool ready_{true};
};

}  // namespace tts::local