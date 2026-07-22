#include "sherpa_local_tts.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "audio_resampler.h"

#ifdef TTS_HAS_SHERPA_ONNX
#include <sherpa-onnx/c-api/c-api.h>
#endif

namespace tts::local {

namespace {

std::string slurp(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

struct SherpaOnnxLocalTts::Impl {
#if TTS_HAS_SHERPA_ONNX
    const SherpaOnnxOfflineTts* tts{nullptr};
    int native_sr{24000};
#else
    int dummy_{0};
#endif
};

SherpaOnnxLocalTts::SherpaOnnxLocalTts(LocalTtsConfig cfg) : cfg_(std::move(cfg)) {
    impl_ = std::make_unique<Impl>();

#if TTS_HAS_SHERPA_ONNX
    namespace fs = std::filesystem;
    const fs::path model_path = fs::path(cfg_.model_dir) / cfg_.model_file;
    const fs::path tokens_path = fs::path(cfg_.model_dir) / "tokens.txt";
    const fs::path data_dir = fs::path(cfg_.model_dir) / "espeak-ng-data";

    if (!fs::exists(model_path) || !fs::exists(tokens_path)) {
        spdlog::warn("sherpa-onnx: model files missing at {}; engine not ready",
                     model_path.string());
        return;
    }

    SherpaOnnxOfflineTtsConfig sc{};
    sc.model.vits.model = strdup(model_path.string().c_str());
    sc.model.vits.tokens = strdup(tokens_path.string().c_str());
    if (fs::exists(data_dir)) {
        sc.model.vits.data_dir = strdup(data_dir.string().c_str());
    }
    sc.model.num_threads = cfg_.num_threads > 0
                               ? cfg_.num_threads
                               : std::max(1, (int)(std::thread::hardware_concurrency() / 2));
    sc.model.provider = strdup("cpu");
    sc.model.debug = 0;
    sc.max_num_sentences = 1;

    const SherpaOnnxOfflineTts* h = SherpaOnnxCreateOfflineTts(&sc);
    free((void*)sc.model.vits.model);
    free((void*)sc.model.vits.tokens);
    free((void*)sc.model.vits.data_dir);
    free((void*)sc.model.provider);

    if (h) {
        impl_->tts = h;
        impl_->native_sr = SherpaOnnxOfflineTtsSampleRate(h);
        ready_ = true;
        spdlog::info("sherpa-onnx TTS ready: model={} sr={} threads={}",
                     model_path.string(), impl_->native_sr, sc.model.num_threads);
    } else {
        spdlog::error("sherpa-onnx: SherpaOnnxCreateOfflineTts returned null");
    }
#else
    spdlog::warn("sherpa-onnx headers not found; build with TTS_SHERPA_LOCAL=ON");
#endif
}

SherpaOnnxLocalTts::~SherpaOnnxLocalTts() {
#if TTS_HAS_SHERPA_ONNX
    if (impl_ && impl_->tts) {
        SherpaOnnxDestroyOfflineTts(impl_->tts);
        impl_->tts = nullptr;
    }
#endif
}

int SherpaOnnxLocalTts::native_sample_rate() const noexcept {
#if TTS_HAS_SHERPA_ONNX
    return impl_ ? impl_->native_sr : 24000;
#else
    return 24000;
#endif
}

LocalTtsResult SherpaOnnxLocalTts::Synthesize(const std::string& text,
                                              const std::string& voice_id,
                                              float speed) {
    LocalTtsResult r;
    r.sample_rate = cfg_.target_sample_rate;
    r.channels = cfg_.target_channels;
    r.status_code = 0;
    r.status_msg = "ok_local";

#if TTS_HAS_SHERPA_ONNX
    if (!ready_ || !impl_->tts) {
        r.status_code = 1004;
        r.status_msg = "local_engine_not_ready";
        return r;
    }

    int sid = 0;
    if (!voice_id.empty()) {
        try { sid = std::stoi(voice_id); } catch (...) { sid = 0; }
    }
    float spd = speed > 0 ? speed : 1.0f;

    auto t0 = std::chrono::steady_clock::now();
    SherpaOnnxGenerationConfig gc{};
    gc.sid = sid;
    gc.speed = spd;
    gc.silence_scale = 0.2f;
    const SherpaOnnxGeneratedAudio* audio = SherpaOnnxOfflineTtsGenerateWithConfig(
        impl_->tts, text.c_str(), &gc, nullptr, nullptr);
    (void)spd;
    if (!audio) {
        r.status_code = 1000;
        r.status_msg = "local_generate_failed";
        return r;
    }
    auto t1 = std::chrono::steady_clock::now();
    r.usage_ms = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

    int native_sr = impl_->native_sr;
    int channels = 1;

    std::vector<float> in_samples(audio->samples, audio->samples + audio->n);
    r.samples = AudioResampler::to_16k_mono_int16(in_samples, native_sr, channels);
    r.sample_rate = cfg_.target_sample_rate;
    r.channels = cfg_.target_channels;
    r.duration_ms = static_cast<int>(static_cast<int64_t>(r.samples.size()) * 1000 / r.sample_rate);

    SherpaOnnxDestroyOfflineTtsGeneratedAudio(audio);
#else
    r.status_code = 1000;
    r.status_msg = "sherpa_onnx_not_compiled";
#endif
    return r;
}

}  // namespace tts::local