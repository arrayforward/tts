#pragma once

#include <atomic>
#include <memory>

#include "framework/event_bus.h"
#include "heartbeat/heartbeat.h"
#include "upstream/minimax_protocol_adapter.h"

namespace tts::observability {

class EventLogger {
public:
    explicit EventLogger(std::shared_ptr<framework::EventBus> bus)
        : bus_(std::move(bus)) { wire(); }
    ~EventLogger() { unwire(); }

private:
    void wire() {
        sub_connected_ = bus_->subscribe<upstream::UpstreamConnectedEvent>(
            [](const upstream::UpstreamConnectedEvent& e) {
                spdlog::info("upstream connected session={}", e.session_id);
            });
        sub_disconnected_ = bus_->subscribe<upstream::UpstreamDisconnectedEvent>(
            [](const upstream::UpstreamDisconnectedEvent& e) {
                spdlog::info("upstream disconnected session={} code={} reason={}",
                             e.session_id, e.code, e.reason);
            });
        sub_send_failed_ = bus_->subscribe<upstream::WsFrameSendFailedEvent>(
            [](const upstream::WsFrameSendFailedEvent& e) {
                spdlog::warn("upstream send failed session={} err={}",
                             e.session_id, e.error);
            });
    }

    void unwire() {
        sub_connected_.reset();
        sub_disconnected_.reset();
        sub_send_failed_.reset();
    }

    std::shared_ptr<framework::EventBus> bus_;
    framework::ScopedSubscription sub_connected_;
    framework::ScopedSubscription sub_disconnected_;
    framework::ScopedSubscription sub_send_failed_;
};

}  // namespace tts::observability