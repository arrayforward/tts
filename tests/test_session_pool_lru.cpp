#include <chrono>

#include <gtest/gtest.h>

#include "databoard/session_pool.h"

using namespace tts::databoard;

TEST(SessionPool, AddAndGet) {
    SessionPool pool(4, std::chrono::seconds(60), std::chrono::seconds(3600));
    auto rec = std::make_unique<SessionRecord>();
    rec->session_id = "s-1";
    rec->state = SessionState::Connecting;
    EXPECT_TRUE(pool.add(std::move(rec)));
    EXPECT_EQ(pool.size(), 1u);

    auto* s = pool.get("s-1");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->session_id, "s-1");
}

TEST(SessionPool, MaxSizeLimit) {
    SessionPool pool(2, std::chrono::seconds(60), std::chrono::seconds(3600));
    for (int i = 0; i < 2; ++i) {
        auto r = std::make_unique<SessionRecord>();
        r->session_id = "s-" + std::to_string(i);
        r->state = SessionState::Ready;
        EXPECT_TRUE(pool.add(std::move(r)));
    }
    auto r = std::make_unique<SessionRecord>();
    r->session_id = "s-overflow";
    EXPECT_FALSE(pool.add(std::move(r)));
    EXPECT_EQ(pool.size(), 2u);
}

TEST(SessionPool, IdleEvictionByTime) {
    SessionPool pool(4, std::chrono::seconds(0), std::chrono::seconds(3600));
    auto r = std::make_unique<SessionRecord>();
    r->session_id = "s-1";
    r->state = SessionState::Ready;
    r->created_at = std::chrono::steady_clock::now();
    r->last_active_at = r->created_at;
    EXPECT_TRUE(pool.add(std::move(r)));

    auto expired = pool.collect_idle_expired(std::chrono::steady_clock::now() +
                                             std::chrono::milliseconds(10));
    ASSERT_EQ(expired.size(), 1u);
    EXPECT_EQ(expired[0], "s-1");
}

TEST(SessionPool, IdleLeastRecentlyUsed) {
    SessionPool pool(4, std::chrono::seconds(60), std::chrono::seconds(3600));
    auto r1 = std::make_unique<SessionRecord>();
    r1->session_id = "old";
    r1->state = SessionState::Ready;
    EXPECT_TRUE(pool.add(std::move(r1)));

    auto r2 = std::make_unique<SessionRecord>();
    r2->session_id = "new";
    r2->state = SessionState::Ready;
    EXPECT_TRUE(pool.add(std::move(r2)));

    auto* picked = pool.find_idle_least_recently_used();
    ASSERT_NE(picked, nullptr);
    EXPECT_EQ(picked->session_id, "old");
}

TEST(SessionPool, ActiveCount) {
    SessionPool pool(4, std::chrono::seconds(60), std::chrono::seconds(3600));
    auto r1 = std::make_unique<SessionRecord>();
    r1->session_id = "a";
    r1->state = SessionState::Ready;
    EXPECT_TRUE(pool.add(std::move(r1)));
    auto r2 = std::make_unique<SessionRecord>();
    r2->session_id = "b";
    r2->state = SessionState::Streaming;
    EXPECT_TRUE(pool.add(std::move(r2)));
    EXPECT_EQ(pool.idle_count(), 1u);
    EXPECT_EQ(pool.active_count(), 1u);
}