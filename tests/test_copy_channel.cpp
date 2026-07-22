#include <chrono>
#include <thread>

#include <gtest/gtest.h>

#include "framework/copy_channel.h"

using namespace tts::framework;

TEST(CopyChannel, SendRecv) {
    CopyChannel<int> ch;
    ch.send(42);
    auto got = ch.recv_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, 42);
}

TEST(CopyChannel, DeepCopyString) {
    CopyChannel<std::string> ch;
    std::string s = "hello";
    ch.send(s);
    s[0] = 'X';
    auto got = ch.recv_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, "hello");
}

TEST(CopyChannel, MoveIntoChannel) {
    CopyChannel<std::string> ch;
    std::string s = "world";
    ch.send(std::move(s));
    auto got = ch.recv_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, "world");
}

TEST(CopyChannel, SwapOutAll) {
    CopyChannel<int> ch;
    for (int i = 0; i < 5; ++i) ch.send(i);
    std::vector<int> out;
    ch.swap_out_all(out);
    EXPECT_EQ(out.size(), 5u);
    EXPECT_EQ(ch.size(), 0u);
}

TEST(CopyChannel, Close) {
    CopyChannel<int> ch;
    ch.close();
    EXPECT_TRUE(ch.is_closed());
}

TEST(CopyChannel, Timeout) {
    CopyChannel<int> ch;
    auto got = ch.recv_for(std::chrono::milliseconds(10));
    EXPECT_FALSE(got.has_value());
}

TEST(CopyChannel, BlockingRecvNotified) {
    CopyChannel<int> ch;
    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ch.send(7);
    });
    auto got = ch.recv();
    EXPECT_TRUE(got.has_value());
    EXPECT_EQ(*got, 7);
    t.join();
}