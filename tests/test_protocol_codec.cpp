#include <gtest/gtest.h>

#include "framework/messages.h"
#include "upstream/ws_frame_codec.h"

using namespace tts::upstream;
using namespace tts::framework;

TEST(WsFrameCodec, EncodeTaskStart) {
    WsFrameCodec c;
    VoiceSettings v;
    v.voice_id = "male-qn-qingse";
    v.speed = 1.0f;
    v.vol = 1.0f;
    v.pitch = 0;
    AudioSettings a;
    a.sample_rate = 16000;
    a.bitrate = 128000;
    a.format = AudioFormat::Mp3;
    a.channel = 1;
    auto s = c.encode_task_start("speech-2.8-turbo", v, a);
    EXPECT_NE(s.find("\"event\":\"task_start\""), std::string::npos);
    EXPECT_NE(s.find("\"model\":\"speech-2.8-turbo\""), std::string::npos);
    EXPECT_NE(s.find("\"voice_id\":\"male-qn-qingse\""), std::string::npos);
    EXPECT_NE(s.find("\"sample_rate\":16000"), std::string::npos);
    EXPECT_NE(s.find("\"channel\":1"), std::string::npos);
    EXPECT_NE(s.find("\"format\":\"mp3\""), std::string::npos);
}

TEST(WsFrameCodec, EncodeTaskContinue) {
    WsFrameCodec c;
    auto s = c.encode_task_continue("hello world");
    EXPECT_NE(s.find("\"event\":\"task_continue\""), std::string::npos);
    EXPECT_NE(s.find("\"text\":\"hello world\""), std::string::npos);
}

TEST(WsFrameCodec, EncodeTaskFinish) {
    WsFrameCodec c;
    auto s = c.encode_task_finish();
    EXPECT_NE(s.find("\"event\":\"task_finish\""), std::string::npos);
}

TEST(WsFrameCodec, ParseConnectedSuccess) {
    WsFrameCodec c;
    auto r = c.parse_server_frame(
        R"({"event":"connected_success","session_id":"abc","base_resp":{"status_code":0}})");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->event, "connected_success");
    ASSERT_TRUE(r->session_id.has_value());
    EXPECT_EQ(*r->session_id, "abc");
}

TEST(WsFrameCodec, ParseTaskContinuedWithHex) {
    WsFrameCodec c;
    auto r = c.parse_server_frame(R"({
        "event":"task_continued",
        "data":{"audio":"AABBCC"},
        "is_final":false,
        "extra_info":{"audio_sample_rate":16000,"audio_channel":1,"usage_characters":42},
        "base_resp":{"status_code":0}
    })");
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->event, "task_continued");
    ASSERT_TRUE(r->audio_hex.has_value());
    EXPECT_EQ(*r->audio_hex, "AABBCC");
    ASSERT_TRUE(r->sample_rate.has_value());
    EXPECT_EQ(*r->sample_rate, 16000);
    ASSERT_TRUE(r->usage_characters.has_value());
    EXPECT_EQ(*r->usage_characters, 42);
}

TEST(WsFrameCodec, ParseMalformedReturnsEmpty) {
    WsFrameCodec c;
    auto r = c.parse_server_frame("not json");
    EXPECT_FALSE(r.has_value());
}