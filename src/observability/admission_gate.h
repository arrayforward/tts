#pragma once

#include <atomic>
#include <memory>

#include "databoard/data_board.h"
#include "framework/event_bus.h"
#include "heartbeat/heartbeat_events.h"

namespace tts::observability {

class AdmissionGate {
public:
    AdmissionGate(std::shared_ptr<databoard::DataBoard> board,
                  std::shared_ptr<framework::EventBus> bus)
        : board_(std::move(board)), bus_(std::move(bus)) { wire(); }
    ~AdmissionGate() { unwire(); }

    [[nodiscard]] bool admit() const noexcept {
        return open_.load(std::memory_order_acquire);
    }

private:
    void wire() {
        sub_tick_ = bus_->subscribe<heartbeat::HeartbeatTickEvent>(
            [this](const heartbeat::HeartbeatTickEvent& t) {
                if (t.changeset_size > 512) {
                    open_.store(false, std::memory_order_release);
                    board_->metrics()->set_backpressure_level(2);
                    spdlog::warn("admission: closing gate (changeset_size={})", t.changeset_size);
                } else if (t.changeset_size < 64) {
                    open_.store(true, std::memory_order_release);
                }
            });
    }

    void unwire() { sub_tick_.reset(); }

    std::shared_ptr<databoard::DataBoard> board_;
    std::shared_ptr<framework::EventBus> bus_;
    framework::ScopedSubscription sub_tick_;
    std::atomic<bool> open_{true};
};

}  // namespace tts::observability