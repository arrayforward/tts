#include <gtest/gtest.h>

#include "local/audio_resampler.h"

using namespace tts::local;

TEST(AudioResampler, IdentityForMatchingRate) {
    std::vector<std::int16_t> in{0, 1000, -1000, 16000, -16000};
    auto out = AudioResampler::resample_linear_int16(in, 16000, 16000);
    EXPECT_EQ(out, in);
}

TEST(AudioResampler, DownsampleThreeToOne) {
    std::vector<std::int16_t> in(48, 1000);
    auto out = AudioResampler::resample_linear_int16(in, 48000, 16000);
    EXPECT_NEAR(out.size(), 16u, 1u);
}

TEST(AudioResampler, UpsampleOneToThree) {
    std::vector<std::int16_t> in(16, 1000);
    auto out = AudioResampler::resample_linear_int16(in, 16000, 48000);
    EXPECT_NEAR(out.size(), 48u, 1u);
}

TEST(AudioResampler, StereoToMono) {
    std::vector<std::int16_t> in{1000, -1000, 500, -500, 0, 0};
    auto out = AudioResampler::stereo_to_mono(in);
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(out[1], 0);
    EXPECT_EQ(out[2], 0);
}

TEST(AudioResampler, FloatTo16kMonoInt16) {
    std::vector<float> in(24000, 0.5f);
    auto out = AudioResampler::to_16k_mono_int16(in, 24000, 1);
    EXPECT_NEAR(out.size(), 16000u, 1u);
    EXPECT_NEAR(out[100], 16383, 2);
}

TEST(AudioResampler, FloatStereoToMono) {
    std::vector<float> in{0.5f, -0.5f, 0.25f, -0.25f};
    auto out = AudioResampler::to_16k_mono_int16(in, 16000, 2);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_NEAR(out[0], 0, 1);
    EXPECT_NEAR(out[1], 0, 1);
}