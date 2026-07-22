#include <chrono>
#include <set>

#include <gtest/gtest.h>

#include "databoard/data_board.h"
#include "framework/clock.h"
#include "heartbeat/evolution_engine.h"

using namespace tts;
using namespace tts::framework;
using namespace tts::databoard;
using namespace tts::heartbeat;

TEST(EvolutionSingleLayer, IdleSessionsCollected) {
    auto board = std::make_shared<DataBoard>();
    auto clock = std::make_shared<VirtualClock>();

    auto rec = std::make_unique<SessionRecord>();
    rec->session_id = "s-idle";
    rec->state = SessionState::Ready;
    rec->created_at = clock->now();
    rec->last_active_at = rec->created_at;
    ASSERT_TRUE(board->session_pool()->add(std::move(rec)));

    clock->advance(std::chrono::seconds(120));

    DataEvolutionEngine engine(board, clock);
    auto closes = engine.evolve({{}, {}});
    ASSERT_EQ(closes.size(), 1u);
    EXPECT_EQ(closes[0].session_id, "s-idle");
    EXPECT_EQ(closes[0].reason, "idle_timeout");
}

TEST(EvolutionSingleLayer, ActiveSessionNotClosed) {
    auto board = std::make_shared<DataBoard>();
    auto clock = std::make_shared<VirtualClock>();

    auto rec = std::make_unique<SessionRecord>();
    rec->session_id = "s-active";
    rec->state = SessionState::Streaming;
    rec->created_at = clock->now();
    rec->last_active_at = rec->created_at;
    ASSERT_TRUE(board->session_pool()->add(std::move(rec)));

    clock->advance(std::chrono::seconds(120));
    DataEvolutionEngine engine(board, clock);
    auto closes = engine.evolve({{"s-active"}, {}});
    EXPECT_TRUE(closes.empty());
}

TEST(EvolutionSingleLayer, FailedRequestTriggersCloseNoCascade) {
    auto board = std::make_shared<DataBoard>();
    auto clock = std::make_shared<VirtualClock>();

    auto rec = std::make_unique<SessionRecord>();
    rec->session_id = "s-fail";
    rec->state = SessionState::Streaming;
    rec->created_at = clock->now();
    rec->last_active_at = rec->created_at;
    ASSERT_TRUE(board->session_pool()->add(std::move(rec)));

    auto p = std::make_unique<PendingRequest>();
    p->request_id = "rid-1";
    p->session_id = "s-fail";
    p->state = PendingState::Failed;
    board->pending()->add(std::move(p));

    DataEvolutionEngine engine(board, clock);
    auto closes = engine.evolve({{}, {"rid-1"}});

    EXPECT_EQ(closes.size(), 1u);
    EXPECT_EQ(closes[0].session_id, "s-fail");

    auto second_pass = engine.evolve({{}, {}});
    EXPECT_TRUE(second_pass.empty());
}

TEST(EvolutionSingleLayer, LifetimeCap) {
    auto board = std::make_shared<DataBoard>();
    auto clock = std::make_shared<VirtualClock>();

    auto rec = std::make_unique<SessionRecord>();
    rec->session_id = "s-old";
    rec->state = SessionState::Ready;
    rec->created_at = clock->now();
    rec->last_active_at = rec->created_at;
    ASSERT_TRUE(board->session_pool()->add(std::move(rec)));

    clock->advance(std::chrono::seconds(3700));
    DataEvolutionEngine engine(board, clock);
    auto closes = engine.evolve({{}, {}});
    EXPECT_EQ(closes.size(), 1u);
    EXPECT_EQ(closes[0].reason, "lifetime_exceeded");
}