#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "framework/change_set.h"
#include "framework/copy_channel.h"
#include "framework/event_bus.h"
#include "upstream_client.h"

namespace tts::upstream {

struct WsOutbound {
    std::string session_id;
    OutWsFrame frame;
};

struct WsInboundFrame {
    std::string session_id;
    WsRawFrame raw;
};

struct UpstreamClosedEvent {
    std::string session_id;
    int code{0};
    std::string reason;

    [[nodiscard]] std::string_view kind_name() const noexcept { return "UpstreamClosedEvent"; }
};

class WsTaskScheduler {
public:
    WsTaskScheduler(std::shared_ptr<framework::CopyChannel<WsOutbound>> outbound,
                    std::unordered_map<std::string, std::unique_ptr<UpstreamClient>>* clients)
        : outbound_(std::move(outbound)), clients_(clients) {}

    void pump(const std::vector<framework::OutWsFrame>& frames) {
        for (const auto& f : frames) {
            outbound_->send(WsOutbound{f.session_id, f});
        }
    }

    void dispatch_pending() {
        std::vector<WsOutbound> drained;
        outbound_->swap_out_all(drained);
        for (auto& o : drained) {
            auto it = clients_->find(o.session_id);
            if (it == clients_->end()) continue;
            switch (o.frame.op) {
                case framework::OutWsOp::TaskStart:
                case framework::OutWsOp::TaskContinue:
                case framework::OutWsOp::TaskFinish:
                    it->second->send(o.frame.payload_json);
                    break;
            }
        }
    }

private:
    std::shared_ptr<framework::CopyChannel<WsOutbound>> outbound_;
    std::unordered_map<std::string, std::unique_ptr<UpstreamClient>>* clients_;
};

}  // namespace tts::upstream