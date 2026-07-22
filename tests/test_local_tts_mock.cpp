#include <gtest/gtest.h>

#include "local/local_tts_engine.h"
#include "local/mock_local_tts.h"

using namespace tts::local;

TEST(MockLocalTts, IsReady) {
    LocalTtsConfig c;
    MockLocalTts m(c);
    EXPECT_TRUE(m.IsReady());
}

TEST(MockLocalTts, SynthesizeProduces16kMono) {
    LocalTtsConfig c;
    c.target_sample_rate = 16000;
    c.target_channels = 1;
    MockLocalTts m(c);
    auto r = m.Synthesize("hello", "", 1.0f);
    EXPECT_EQ(r.sample_rate, 16000);
    EXPECT_EQ(r.channels, 1);
    EXPECT_FALSE(r.samples.empty());
    EXPECT_EQ(r.status_code, 0);
    EXPECT_GT(r.duration_ms, 0);
}

TEST(MockLocalTts, SpeedScalesDuration) {
    LocalTtsConfig c;
    c.target_sample_rate = 16000;
    MockLocalTts m(c);
    auto fast = m.Synthesize("hello world test string", "", 2.0f);
    auto slow = m.Synthesize("hello world test string", "", 0.5f);
    EXPECT_LT(fast.duration_ms, slow.duration_ms);
    EXPECT_LT(fast.samples.size(), slow.samples.size());
}

TEST(MockLocalTts, DeterministicSameText) {
    LocalTtsConfig c;
    MockLocalTts m1(c), m2(c);
    auto r1 = m1.Synthesize("deterministic", "", 1.0f);
    auto r2 = m2.Synthesize("deterministic", "", 1.0f);
    EXPECT_EQ(r1.duration_ms, r2.duration_ms);
    EXPECT_EQ(r1.samples.size(), r2.samples.size());
    EXPECT_EQ(r1.samples, r2.samples);
}

TEST(MockLocalTts, VoiceChangesPitch) {
    LocalTtsConfig c;
    MockLocalTts m(c);
    auto a = m.Synthesize("text", "voice_a", 1.0f);
    auto b = m.Synthesize("text", "voice_b", 1.0f);
    EXPECT_NE(a.samples, b.samples);
}