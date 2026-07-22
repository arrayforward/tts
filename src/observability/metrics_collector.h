#pragma once

#include <memory>

#include "databoard/data_board.h"
#include "framework/event_bus.h"
#include "heartbeat/heartbeat.h"
#include "upstream/minimax_protocol_adapter.h"

namespace tts::observability {

class MetricsCollector {
public:
    MetricsCollector(std::shared_ptr<databoard::DataBoard> board,
                     std::shared_ptr<framework::EventBus> bus)
        : board_(std::move(board)), bus_(std::move(bus)) {
        wire();
    }

    ~MetricsCollector() { unwire(); }

private:
    void wire() {
        sub_connected_ = bus_->subscribe<upstream::UpstreamConnectedEvent>(
            [this](const upstream::UpstreamConnectedEvent&) {
                board_->metrics()->inc_upstream_connected();
            });
        sub_disconnected_ = bus_->subscribe<upstream::UpstreamDisconnectedEvent>(
            [this](const upstream::UpstreamDisconnectedEvent&) {
                board_->metrics()->inc_upstream_disconnected();
            });
        sub_send_failed_ = bus_->subscribe<upstream::WsFrameSendFailedEvent>(
            [this](const upstream::WsFrameSendFailedEvent&) {
                board_->metrics()->inc_failed_requests();
            });
    }

    void unwire() {
        sub_connected_.reset();
        sub_disconnected_.reset();
        sub_send_failed_.reset();
    }

    std::shared_ptr<databoard::DataBoard> board_;
    std::shared_ptr<framework::EventBus> bus_;
    framework::ScopedSubscription sub_connected_;
    framework::ScopedSubscription sub_disconnected_;
    framework::ScopedSubscription sub_send_failed_;
};

}  // namespace tts::observability