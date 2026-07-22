#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

namespace tts::heartbeat {

struct HeartbeatTickEvent {
    std::uint64_t duration_us{0};
    std::uint64_t batch_size{0};
    std::uint64_t changeset_size{0};

    [[nodiscard]] std::string_view kind_name() const noexcept { return "HeartbeatTickEvent"; }
};

}  // namespace tts::heartbeat