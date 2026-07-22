#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <source_location>
#include <string>
#include <string_view>

namespace tts::framework {

enum class TaskKind {
    Cpu,
    Io,
};

struct TaskId {
    std::uint64_t value{0};

    [[nodiscard]] std::string to_string() const {
        return "T" + std::to_string(value);
    }
};

class TaskIdGenerator {
public:
    static TaskId next() {
        static std::atomic<std::uint64_t> counter{1};
        return TaskId{counter.fetch_add(1, std::memory_order_relaxed)};
    }
};

class Task {
public:
    using Fn = std::function<void()>;

    Task() = default;
    Task(std::string name, TaskKind kind, Fn fn,
         std::source_location loc = std::source_location::current())
        : id_(TaskIdGenerator::next()),
          name_(std::move(name)),
          kind_(kind),
          fn_(std::move(fn)),
          loc_(loc) {}

    [[nodiscard]] TaskId id() const noexcept { return id_; }
    [[nodiscard]] std::string_view name() const noexcept { return name_; }
    [[nodiscard]] TaskKind kind() const noexcept { return kind_; }
    [[nodiscard]] std::source_location location() const noexcept { return loc_; }

    void operator()() const {
        if (fn_) fn_();
    }

    void run_with_slow_warning() const;

    [[nodiscard]] static std::chrono::milliseconds slow_threshold(TaskKind k) {
        switch (k) {
            case TaskKind::Cpu: return std::chrono::milliseconds(10);
            case TaskKind::Io:  return std::chrono::seconds(1);
        }
        return std::chrono::milliseconds(10);
    }

private:
    TaskId id_{};
    std::string name_;
    TaskKind kind_{TaskKind::Cpu};
    Fn fn_;
    std::source_location loc_{};
};

}  // namespace tts::framework