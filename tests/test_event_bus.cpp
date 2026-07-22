#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "framework/event_bus.h"

using namespace tts::framework;

namespace {

struct TestEvent {
    int v{0};
    std::string_view kind_name() const noexcept { return "TestEvent"; }
};

struct OtherEvent {
    std::string_view kind_name() const noexcept { return "OtherEvent"; }
};

}  // namespace

TEST(EventBus, SyncDispatch) {
    EventBus bus;
    int got = 0;
    auto sub = bus.subscribe<TestEvent>([&](const TestEvent& e) { got = e.v; });
    bus.publish_sync(TestEvent{42});
    EXPECT_EQ(got, 42);
}

TEST(EventBus, UnsubscribeOnDestruction) {
    EventBus bus;
    int got = 0;
    {
        auto sub = bus.subscribe<TestEvent>([&](const TestEvent& e) { got = e.v; });
        bus.publish_sync(TestEvent{1});
        EXPECT_EQ(got, 1);
    }
    bus.publish_sync(TestEvent{2});
    EXPECT_EQ(got, 1);
}

TEST(EventBus, AsyncDispatchCrossThread) {
    EventBus bus;
    bus.start_workers(1);
    std::atomic<int> got{0};
    auto sub = bus.subscribe<TestEvent>([&](const TestEvent& e) {
        got.store(e.v, std::memory_order_release);
    });

    for (int i = 0; i < 100; ++i) {
        bus.publish_async(TestEvent{i + 1});
    }
    for (int i = 0; i < 200 && got.load(std::memory_order_acquire) < 100; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_EQ(got.load(), 100);
    bus.stop_workers();
}

TEST(EventBus, DistinctTypes) {
    EventBus bus;
    int a = 0, b = 0;
    auto sa = bus.subscribe<TestEvent>([&](const TestEvent& e) { a = e.v; });
    auto sb = bus.subscribe<OtherEvent>([&](const OtherEvent&) { b = 7; });
    bus.publish_sync(TestEvent{5});
    EXPECT_EQ(a, 5);
    EXPECT_EQ(b, 0);
    bus.publish_sync(OtherEvent{});
    EXPECT_EQ(b, 7);
}

TEST(EventBus, ManySubscribers) {
    EventBus bus;
    std::vector<int> counts(8, 0);
    std::vector<ScopedSubscription> subs;
    for (int i = 0; i < 8; ++i) {
        subs.push_back(bus.subscribe<TestEvent>([i, &counts](const TestEvent& e) {
            counts[i] = e.v;
        }));
    }
    bus.publish_sync(TestEvent{99});
    for (int v : counts) EXPECT_EQ(v, 99);
}