#include <gtest/gtest.h>

#include "entry/request_parser.h"
#include "pb2/tts.pb.h"

using namespace tts;
using namespace tts::entry;
using namespace tts::framework;

using namespace tts::entry;

TEST(RequestParser, UnaryAccepts16kMonoMp3) {
    RequestParser p;
    SynthesizeRequest req;
    req.set_text("hello");
    req.set_model(SPEECH_2_8_TURBO);
    auto* v = req.mutable_voice();
    v->set_voice_id("male-qn-qingse");
    v->set_speed(1.0f);
    v->set_vol(1.0f);
    auto* a = req.mutable_audio();
    a->set_sample_rate(16000);
    a->set_bitrate(128000);
    a->set_format(MP3);
    a->set_channel(1);
    auto parsed = p.parse_unary(req, "rid-1");
    EXPECT_EQ(parsed.request.text, "hello");
    EXPECT_EQ(parsed.request.audio.sample_rate, 16000);
    EXPECT_EQ(parsed.request.audio.channel, 1);
    EXPECT_EQ(parsed.request.audio.format, framework::AudioFormat::Mp3);
    EXPECT_EQ(parsed.request.request_id, "rid-1");
}

TEST(RequestParser, UnaryRejectsNonMono) {
    RequestParser p;
    SynthesizeRequest req;
    req.set_text("hello");
    req.set_model(SPEECH_2_8_TURBO);
    auto* a = req.mutable_audio();
    a->set_sample_rate(16000);
    a->set_format(MP3);
    a->set_channel(2);
    EXPECT_THROW(p.parse_unary(req, "rid"), ParseError);
}

TEST(RequestParser, UnaryRejectsBadSampleRate) {
    RequestParser p;
    SynthesizeRequest req;
    req.set_text("x");
    req.set_model(SPEECH_2_8_TURBO);
    auto* a = req.mutable_audio();
    a->set_sample_rate(44100);
    a->set_format(MP3);
    a->set_channel(1);
    EXPECT_THROW(p.parse_unary(req, "rid"), ParseError);
}

TEST(RequestParser, UnaryAcceptsPcm) {
    RequestParser p;
    SynthesizeRequest req;
    req.set_text("hi");
    req.set_model(SPEECH_2_8_TURBO);
    auto* a = req.mutable_audio();
    a->set_sample_rate(16000);
    a->set_format(PCM);
    a->set_channel(1);
    auto parsed = p.parse_unary(req, "r");
    EXPECT_EQ(parsed.request.audio.format, framework::AudioFormat::Pcm);
}

TEST(RequestParser, ChunkFirstUsesInitialSettings) {
    RequestParser p;
    SynthesizeRequest init;
    init.set_model(SPEECH_2_8_TURBO);
    auto* a = init.mutable_audio();
    a->set_sample_rate(16000);
    a->set_format(MP3);
    a->set_channel(1);

    TextChunk chunk;
    chunk.set_text("first chunk");
    auto parsed = p.parse_chunk(chunk, "rid-2", init, true);
    EXPECT_EQ(parsed.request.kind, framework::RequestKind::OpenStream);
    EXPECT_EQ(parsed.request.text, "first chunk");
}

TEST(RequestParser, ChunkFinishSetsKind) {
    RequestParser p;
    SynthesizeRequest init;
    TextChunk chunk;
    chunk.mutable_finish()->set_placeholder(true);
    auto parsed = p.parse_chunk(chunk, "rid", init, false);
    EXPECT_EQ(parsed.request.kind, framework::RequestKind::FinishStream);
}