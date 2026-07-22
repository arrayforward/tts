#pragma once

#include <memory>

#include "metrics_snapshot.h"
#include "pending_request_store.h"
#include "session_pool.h"
#include "voice_catalog.h"

namespace tts::databoard {

class DataBoard {
public:
    DataBoard()
        : session_pool_(std::make_shared<SessionPool>(
              16, std::chrono::seconds(60), std::chrono::seconds(3600))),
          pending_(std::make_shared<PendingRequestStore>()),
          catalog_(std::make_shared<VoiceCatalog>()),
          metrics_(std::make_shared<MetricsSnapshot>()) {}

    [[nodiscard]] std::shared_ptr<SessionPool> session_pool() const noexcept {
        return session_pool_;
    }
    [[nodiscard]] std::shared_ptr<PendingRequestStore> pending() const noexcept {
        return pending_;
    }
    [[nodiscard]] std::shared_ptr<VoiceCatalog> voice_catalog() const noexcept {
        return catalog_;
    }
    [[nodiscard]] std::shared_ptr<MetricsSnapshot> metrics() const noexcept {
        return metrics_;
    }

private:
    std::shared_ptr<SessionPool> session_pool_;
    std::shared_ptr<PendingRequestStore> pending_;
    std::shared_ptr<VoiceCatalog> catalog_;
    std::shared_ptr<MetricsSnapshot> metrics_;
};

}  // namespace tts::databoard