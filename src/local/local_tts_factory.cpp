#include "local_tts_engine.h"

#include <utility>

#include "mock_local_tts.h"

#ifdef TTS_HAS_SHERPA_ONNX
#include "sherpa_local_tts.h"
#endif

namespace tts::local {

std::unique_ptr<ILocalTtsEngine> CreateMockLocalTts(const LocalTtsConfig& cfg) {
    return std::make_unique<MockLocalTts>(cfg);
}

std::unique_ptr<ILocalTtsEngine> CreateOnnxLocalTts(const LocalTtsConfig& cfg) {
#ifdef TTS_HAS_SHERPA_ONNX
    return std::make_unique<SherpaOnnxLocalTts>(cfg);
#else
    return nullptr;
#endif
}

std::unique_ptr<ILocalTtsEngine> CreateLocalTts(const LocalTtsConfig& cfg) {
    if (cfg.backend == BackendKind::Mock) {
        return CreateMockLocalTts(cfg);
    }
    auto e = CreateOnnxLocalTts(cfg);
    if (e && e->IsReady()) return e;
    auto fb = LocalTtsConfig{cfg};
    fb.backend = BackendKind::Mock;
    return CreateMockLocalTts(fb);
}

}  // namespace tts::local