#include <gtest/gtest.h>

#include "databoard/data_board.h"
#include "framework/clock.h"
#include "framework/copy_channel.h"
#include "framework/event_bus.h"
#include "framework/messages.h"
#include "heartbeat/backpressure_controller.h"
#include "heartbeat/changeset_builder.h"
#include "heartbeat/evolution_engine.h"
#include "heartbeat/heartbeat.h"
#include "heartbeat/message_processor.h"
#include "local/local_tts_engine.h"
#include "local/mock_local_tts.h"
#include "upstream/minimax_protocol_adapter.h"
#include "upstream/ws_frame_codec.h"

using namespace tts;
using namespace tts::framework;
using namespace tts::databoard;
using namespace tts::heartbeat;
using namespace tts::local;
using namespace tts::upstream;

namespace {

struct Harness {
    std::shared_ptr<DataBoard> board = std::make_shared<DataBoard>();
    std::shared_ptr<SystemClock> clock = std::make_shared<SystemClock>();
    std::shared_ptr<EventBus> bus = std::make_shared<EventBus>();
    std::shared_ptr<CopyChannel<Message>> req_ch =
        std::make_shared<CopyChannel<Message>>();
    std::shared_ptr<CopyChannel<Message>> ws_ch =
        std::make_shared<CopyChannel<Message>>();
    std::shared_ptr<upstream::WsFrameCodec> codec =
        std::make_shared<upstream::WsFrameCodec>();
    std::shared_ptr<ILocalTtsEngine> local_engine;
    std::shared_ptr<MessageProcessor> processor;
    std::shared_ptr<DataEvolutionEngine> evolution;
    std::shared_ptr<ChangeSetBuilder> cs;
    std::shared_ptr<BackpressureController> bp;
    std::shared_ptr<Heartbeat> hb;

    Harness(bool with_local) {
        LocalTtsConfig c;
        c.target_sample_rate = 16000;
        c.target_channels = 1;
        if (with_local) {
            local_engine = std::make_shared<MockLocalTts>(c);
        }
        processor = std::make_shared<MessageProcessor>(board, req_ch, ws_ch);
        evolution = std::make_shared<DataEvolutionEngine>(board, clock);
        cs = std::make_shared<ChangeSetBuilder>(bus);
        bp = std::make_shared<BackpressureController>(board);
        hb = std::make_shared<Heartbeat>(board, clock, bus, processor, evolution,
                                         cs, bp, codec, local_engine, true);
    }
};

TtsRequestMsg make_local_req(const std::string& rid, const std::string& text) {
    TtsRequestMsg r;
    r.request_id = rid;
    r.kind = RequestKind::OpenStream;
    r.text = text;
    r.model = Model::Speech28Turbo;
    r.backend = Backend::Local;
    return r;
}

TtsRequestMsg make_upstream_req(const std::string& rid, const std::string& text) {
    TtsRequestMsg r = make_local_req(rid, text);
    r.backend = Backend::Upstream;
    return r;
}

}  // namespace

TEST(HeartbeatLocal, RoutesToLocalWhenEngineReady) {
    Harness h(true);
    h.req_ch->send(make_local_req("rid-L", "hello local"));

    auto cs = h.hb->run_once_for_test();

    EXPECT_FALSE(cs.local_jobs.empty());
    EXPECT_TRUE(cs.ws_frames.empty());
}

TEST(HeartbeatLocal, RoutesUpstreamWhenLocalDisabled) {
    Harness h(false);
    h.req_ch->send(make_local_req("rid-U", "hello upstream"));

    auto cs = h.hb->run_once_for_test();

    EXPECT_TRUE(cs.local_jobs.empty());
    EXPECT_FALSE(cs.ws_frames.empty());
}

TEST(HeartbeatLocal, BackendExplicitUpstream) {
    Harness h(true);
    h.req_ch->send(make_upstream_req("rid-E", "force upstream"));

    auto cs = h.hb->run_once_for_test();

    EXPECT_TRUE(cs.local_jobs.empty());
    EXPECT_FALSE(cs.ws_frames.empty());
}

TEST(HeartbeatLocal, LocalCompleteEmitsWrite) {
    Harness h(true);
    h.req_ch->send(make_local_req("rid-C", "x"));
    h.hb->run_once_for_test();

    LocalSynthCompleteMsg c;
    c.request_id = "rid-C";
    c.audio = {0x10, 0x20, 0x30, 0x40};
    c.sample_rate = 16000;
    c.channels = 1;
    c.format = AudioFormat::Pcm;
    c.usage_ms = 100;
    h.req_ch->send(Message{c});

    auto cs = h.hb->run_once_for_test();
    ASSERT_EQ(cs.client_writes.size(), 1u);
    EXPECT_EQ(cs.client_writes[0].request_id, "rid-C");
    EXPECT_EQ(cs.client_writes[0].audio.size(), 4u);
    EXPECT_TRUE(cs.client_writes[0].is_final);
}

TEST(HeartbeatLocal, LocalFailedMarksFailed) {
    Harness h(true);
    h.req_ch->send(make_local_req("rid-F", "y"));
    h.hb->run_once_for_test();

    LocalSynthFailedMsg f;
    f.request_id = "rid-F";
    f.status_code = 1000;
    f.status_msg = "engine_broken";
    h.req_ch->send(Message{f});

    h.hb->run_once_for_test();
    EXPECT_EQ(h.board->pending()->get("rid-F"), nullptr);
}