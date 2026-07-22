#pragma once

#include "framework/change_set.h"
#include "framework/event_bus.h"

namespace tts::heartbeat {

class ChangeSetBuilder {
public:
    explicit ChangeSetBuilder(std::shared_ptr<framework::EventBus> bus)
        : bus_(std::move(bus)) {}

    void add_ws_frame(framework::OutWsFrame f) {
        cs_.ws_frames.push_back(std::move(f));
    }
    void add_client_write(framework::WriteToClient w) {
        cs_.client_writes.push_back(std::move(w));
    }
    void add_close(framework::CloseSession c) {
        cs_.close_sessions.push_back(std::move(c));
    }
    void add_emit(framework::EmitMessage m) {
        cs_.emit_messages.push_back(std::move(m));
    }
    void add_local_job(framework::LocalSynthJob j) {
        cs_.local_jobs.push_back(std::move(j));
    }

    framework::ChangeSet collect() {
        auto out = std::move(cs_);
        cs_ = {};
        return out;
    }

    [[nodiscard]] const framework::ChangeSet& current() const noexcept { return cs_; }

private:
    std::shared_ptr<framework::EventBus> bus_;
    framework::ChangeSet cs_;
};

}  // namespace tts::heartbeat