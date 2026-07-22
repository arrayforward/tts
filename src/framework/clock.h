#pragma once

#include <chrono>
#include <cstdint>
#include <thread>

namespace tts::framework {

using TimePoint = std::chrono::steady_clock::time_point;
using Duration  = std::chrono::nanoseconds;

class IClock {
public:
    virtual ~IClock() = default;
    [[nodiscard]] virtual TimePoint now() const = 0;
    virtual void sleep_for(Duration d) const = 0;
};

class SystemClock final : public IClock {
public:
    [[nodiscard]] TimePoint now() const override {
        return std::chrono::steady_clock::now();
    }
    void sleep_for(Duration d) const override {
        std::this_thread::sleep_for(d);
    }
};

class VirtualClock final : public IClock {
public:
    VirtualClock() = default;

    [[nodiscard]] TimePoint now() const override {
        return current_;
    }

    void sleep_for(Duration) const override {
    }

    void advance(Duration d) {
        current_ += d;
    }

    void set(TimePoint t) {
        current_ = t;
    }

    [[nodiscard]] Duration elapsed() const {
        return current_.time_since_epoch();
    }

private:
    TimePoint current_{};
};

}  // namespace tts::framework