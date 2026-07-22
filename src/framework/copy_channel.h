#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>
#include <vector>

namespace tts::framework {

class ChannelClosed : public std::runtime_error {
public:
    ChannelClosed() : std::runtime_error("channel closed") {}
};

template <typename T>
class CopyChannel {
public:
    CopyChannel() = default;
    CopyChannel(const CopyChannel&) = delete;
    CopyChannel& operator=(const CopyChannel&) = delete;

    ~CopyChannel() { close(); }

    void send(T value) {
        {
            std::lock_guard lk(mtx_);
            if (closed_) throw ChannelClosed{};
            buffer_.push(std::move(value));
        }
        cv_.notify_one();
    }

    bool try_send(T value) {
        {
            std::lock_guard lk(mtx_);
            if (closed_) return false;
            buffer_.push(std::move(value));
        }
        cv_.notify_one();
        return true;
    }

    [[nodiscard]] std::optional<T> recv(std::chrono::milliseconds timeout) {
        std::unique_lock lk(mtx_);
        if (!cv_.wait_for(lk, timeout, [this] { return !buffer_.empty() || closed_; })) {
            return std::nullopt;
        }
        if (buffer_.empty()) return std::nullopt;
        T v = std::move(buffer_.front());
        buffer_.pop();
        return v;
    }

    [[nodiscard]] std::optional<T> recv() {
        std::unique_lock lk(mtx_);
        cv_.wait(lk, [this] { return !buffer_.empty() || closed_; });
        if (buffer_.empty()) return std::nullopt;
        T v = std::move(buffer_.front());
        buffer_.pop();
        return v;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] std::optional<T> recv_for(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lk(mtx_);
        if (!cv_.wait_for(lk, timeout, [this] { return !buffer_.empty() || closed_; })) {
            return std::nullopt;
        }
        if (buffer_.empty()) return std::nullopt;
        T v = std::move(buffer_.front());
        buffer_.pop();
        return v;
    }

    void swap_out_all(std::vector<T>& out) {
        std::lock_guard lk(mtx_);
        while (!buffer_.empty()) {
            out.push_back(std::move(buffer_.front()));
            buffer_.pop();
        }
    }

    [[nodiscard]] std::size_t size() const {
        std::lock_guard lk(mtx_);
        return buffer_.size();
    }

    void close() {
        {
            std::lock_guard lk(mtx_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    [[nodiscard]] bool is_closed() const {
        std::lock_guard lk(mtx_);
        return closed_;
    }

private:
    mutable std::mutex mtx_;
    std::condition_variable cv_;
    std::queue<T> buffer_;
    bool closed_{false};
};

}  // namespace tts::framework