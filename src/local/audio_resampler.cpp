#include "audio_resampler.h"

#include <algorithm>
#include <cmath>

namespace tts::local {

namespace {

inline std::int16_t float_to_int16(float x) {
    x = std::clamp(x, -1.0f, 1.0f);
    return static_cast<std::int16_t>(std::lrintf(x * 32767.0f));
}

inline float int16_to_float(std::int16_t x) {
    return static_cast<float>(x) / 32768.0f;
}

}  // namespace

std::vector<std::int16_t> AudioResampler::to_16k_mono_int16(const std::vector<float>& input,
                                                             int input_sample_rate,
                                                             int input_channels) {
    if (input.empty()) return {};
    std::vector<std::int16_t> pcm;
    pcm.reserve(input.size() / std::max(1, input_channels));

    if (input_channels == 1) {
        for (float v : input) pcm.push_back(float_to_int16(v));
    } else if (input_channels == 2) {
        for (std::size_t i = 0; i + 1 < input.size(); i += 2) {
            float m = 0.5f * (input[i] + input[i + 1]);
            pcm.push_back(float_to_int16(m));
        }
    } else {
        for (std::size_t i = 0; i < input.size(); i += input_channels) {
            float sum = 0.0f;
            for (int c = 0; c < input_channels && i + c < input.size(); ++c) {
                sum += input[i + c];
            }
            pcm.push_back(float_to_int16(sum / static_cast<float>(input_channels)));
        }
    }

    if (input_sample_rate == 16000) return pcm;
    return resample_linear_int16(pcm, input_sample_rate, 16000);
}

std::vector<std::int16_t> AudioResampler::stereo_to_mono(const std::vector<std::int16_t>& interleaved) {
    std::vector<std::int16_t> out;
    out.reserve(interleaved.size() / 2);
    for (std::size_t i = 0; i + 1 < interleaved.size(); i += 2) {
        int sum = static_cast<int>(interleaved[i]) + static_cast<int>(interleaved[i + 1]);
        out.push_back(static_cast<std::int16_t>(sum / 2));
    }
    return out;
}

std::vector<std::int16_t> AudioResampler::resample_linear_int16(const std::vector<std::int16_t>& input,
                                                                 int input_rate,
                                                                 int output_rate) {
    if (input.empty() || input_rate == output_rate) return input;
    double ratio = static_cast<double>(output_rate) / static_cast<double>(input_rate);
    std::size_t out_len = static_cast<std::size_t>(std::ceil(static_cast<double>(input.size()) * ratio));
    std::vector<std::int16_t> out;
    out.reserve(out_len);
    for (std::size_t i = 0; i < out_len; ++i) {
        double pos = static_cast<double>(i) / ratio;
        std::size_t i0 = static_cast<std::size_t>(std::floor(pos));
        std::size_t i1 = std::min(i0 + 1, input.size() - 1);
        double t = pos - static_cast<double>(i0);
        double v = (1.0 - t) * static_cast<double>(input[i0]) + t * static_cast<double>(input[i1]);
        if (v > 32767.0) v = 32767.0;
        if (v < -32768.0) v = -32768.0;
        out.push_back(static_cast<std::int16_t>(std::lrint(v)));
    }
    return out;
}

}  // namespace tts::local