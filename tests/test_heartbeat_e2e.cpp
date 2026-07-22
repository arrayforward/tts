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
#include "upstream/ws_frame_codec.h"

using namespace tts;
using namespace tts::framework;
using namespace tts::databoard;
using namespace tts::heartbeat;
using namespace tts::upstream;
using namespace tts::local;

namespace {

struct Harness {
    std::shared_ptr<DataBoard> board = std::make_shared<DataBoard>();
    std::shared_ptr<SystemClock> clock = std::make_shared<SystemClock>();
    std::shared_ptr<EventBus> bus = std::make_shared<EventBus>();
    std::shared_ptr<CopyChannel<Message>> req_ch =
        std::make_shared<CopyChannel<Message>>();
    std::shared_ptr<CopyChannel<Message>> ws_ch =
        std::make_shared<CopyChannel<Message>>();
    std::shared_ptr<WsFrameCodec> codec = std::make_shared<WsFrameCodec>();
    std::shared_ptr<ILocalTtsEngine> local_engine;
    std::shared_ptr<MessageProcessor> processor;
    std::shared_ptr<DataEvolutionEngine> evolution;
    std::shared_ptr<ChangeSetBuilder> cs;
    std::shared_ptr<BackpressureController> bp;
    std::shared_ptr<Heartbeat> hb;

    Harness(bool with_local = false) {
        if (with_local) {
            LocalTtsConfig c;
            local_engine = std::make_shared<MockLocalTts>(c);
        }
        processor = std::make_shared<MessageProcessor>(board, req_ch, ws_ch);
        evolution = std::make_shared<DataEvolutionEngine>(board, clock);
        cs = std::make_shared<ChangeSetBuilder>(bus);
        bp = std::make_shared<BackpressureController>(board);
        hb = std::make_shared<Heartbeat>(board, clock, bus, processor, evolution,
                                         cs, bp, codec, local_engine, false);
    }
};

TtsRequestMsg make_req(const std::string& rid, const std::string& text) {
    TtsRequestMsg r;
    r.request_id = rid;
    r.kind = RequestKind::OpenStream;
    r.text = text;
    r.model = Model::Speech28Turbo;
    return r;
}

}  // namespace

TEST(HeartbeatE2E, NewRequestAcquiresSessionAndEmitsStartContinue) {
    Harness h;
    h.req_ch->send(make_req("rid-1", "hello"));

    auto cs = h.hb->run_once_for_test();

    EXPECT_GE(cs.ws_frames.size(), 2u);
    bool has_start = false, has_continue = false;
    for (const auto& f : cs.ws_frames) {
        if (f.op == OutWsOp::TaskStart) has_start = true;
        if (f.op == OutWsOp::TaskContinue) has_continue = true;
    }
    EXPECT_TRUE(has_start);
    EXPECT_TRUE(has_continue);
    EXPECT_GE(h.board->pending()->size(), 1u);
}

TEST(HeartbeatE2E, AudioChunkAppendsAndCompletes) {
    Harness h;
    h.req_ch->send(make_req("rid-A", "x"));

    auto cs1 = h.hb->run_once_for_test();
    (void)cs1;

    auto* p = h.board->pending()->get("rid-A");
    ASSERT_NE(p, nullptr);
    h.board->pending()->bind_session("rid-A", p->session_id);

    auto* s = h.board->session_pool()->get(p->session_id);
    s->current_request_id = "rid-A";

    TtsResponseMsg chunk;
    chunk.session_id = p->session_id;
    chunk.kind = WsEventKind::AudioChunk;
    chunk.audio_hex = "4142";
    chunk.is_final = true;
    chunk.sample_rate = 16000;
    chunk.channels = 1;
    chunk.usage_characters = 10;
    h.ws_ch->send(Message{chunk});

    auto cs2 = h.hb->run_once_for_test();
    EXPECT_GE(cs2.client_writes.size(), 1u);
    EXPECT_EQ(cs2.client_writes[0].request_id, "rid-A");
    EXPECT_EQ(cs2.client_writes[0].audio.size(), 2u);
    EXPECT_TRUE(cs2.client_writes[0].is_final);
}

TEST(HeartbeatE2E, SingleLayerEvolutionNoRecursion) {
    Harness h;
    h.req_ch->send(make_req("rid-X", "y"));
    auto cs1 = h.hb->run_once_for_test();
    (void)cs1;

    auto* p = h.board->pending()->get("rid-X");
    ASSERT_NE(p, nullptr);
    const std::string captured_sid = p->session_id;
    h.board->pending()->set_state("rid-X", PendingState::Failed);
    p->status_code = 1004;
    p->status_msg = "auth";

    auto cs2 = h.hb->run_once_for_test();

    auto* p2 = h.board->pending()->get("rid-X");
    EXPECT_EQ(p2, nullptr);

    bool session_closed = false;
    for (const auto& c : cs2.close_sessions) {
        if (c.session_id == captured_sid && c.reason == "request_failed_or_cancelled") {
            session_closed = true;
        }
    }
    EXPECT_TRUE(session_closed);
}

TEST(HeartbeatE2E, CancelMessageMarksCancelled) {
    Harness h;
    h.req_ch->send(make_req("rid-C", "z"));
    h.hb->run_once_for_test();
    h.req_ch->send(CancelRequestMsg{"rid-C", "user_cancelled"});
    h.hb->run_once_for_test();
    EXPECT_EQ(h.board->pending()->get("rid-C"), nullptr);
}