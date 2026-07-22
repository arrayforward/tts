#pragma once

#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "databoard/data_board.h"
#include "databoard/session_pool.h"
#include "framework/change_set.h"
#include "framework/clock.h"

namespace tts::heartbeat {

struct EvolveInputs {
    std::set<std::string> changed_sessions;
    std::set<std::string> changed_requests;
};

class DataEvolutionEngine {
public:
    DataEvolutionEngine(std::shared_ptr<databoard::DataBoard> board,
                        std::shared_ptr<framework::IClock> clock)
        : board_(std::move(board)), clock_(std::move(clock)) {}

    std::vector<framework::CloseSession> evolve(const EvolveInputs& inputs) {
        std::vector<framework::CloseSession> out;
        auto now = clock_->now();

        for (const auto& sid : inputs.changed_sessions) {
            auto* s = board_->session_pool()->get(sid);
            if (!s) continue;
            if (s->state == databoard::SessionState::Errored ||
                s->state == databoard::SessionState::Closed) {
                out.push_back({sid, "evolve_session_closed"});
                board_->session_pool()->mark_closed(sid);
            }
        }

        auto lifetime_expired = board_->session_pool()->collect_lifetime_expired(now);
        for (const auto& sid : lifetime_expired) {
            out.push_back({sid, "lifetime_exceeded"});
            board_->session_pool()->mark_closed(sid);
        }

        auto idle_expired = board_->session_pool()->collect_idle_expired(now);
        for (const auto& sid : idle_expired) {
            out.push_back({sid, "idle_timeout"});
            board_->session_pool()->mark_closed(sid);
        }

        auto terminal_rids = board_->pending()->collect_terminal();
        for (const auto& rid : terminal_rids) {
            auto* p = board_->pending()->get(rid);
            if (!p) continue;
            out.push_back({p->session_id, "request_failed_or_cancelled"});
            board_->pending()->remove(rid);
        }

        return out;
    }

private:
    std::shared_ptr<databoard::DataBoard> board_;
    std::shared_ptr<framework::IClock> clock_;
};

}  // namespace tts::heartbeat