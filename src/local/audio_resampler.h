#pragma once

#include <cstdint>
#include <vector>

namespace tts::local {

class AudioResampler {
public:
    static std::vector<std::int16_t> to_16k_mono_int16(const std::vector<float>& input,
                                                       int input_sample_rate,
                                                       int input_channels);
    static std::vector<std::int16_t> stereo_to_mono(const std::vector<std::int16_t>& interleaved);
    static std::vector<std::int16_t> resample_linear_int16(const std::vector<std::int16_t>& input,
                                                           int input_rate,
                                                           int output_rate);
};

}  // namespace tts::local