#pragma once

#include <atomic>
#include <chrono>
#include <memory>

#include "databoard/data_board.h"
#include "framework/clock.h"

namespace tts::heartbeat {

class BackpressureController {
public:
    BackpressureController(std::shared_ptr<databoard::DataBoard> board)
        : board_(std::move(board)) {}

    struct Decision {
        bool admit{true};
        std::uint64_t level{0};
    };

    Decision evaluate() {
        Decision d;
        const auto inflight = board_->metrics()->inflight();
        const auto queued = board_->metrics()->queued();
        d.level = std::max(inflight, queued);
        if (inflight >= high_watermark_ || queued >= queue_high_watermark_) {
            d.admit = false;
            board_->metrics()->set_backpressure_level(2);
        } else if (inflight >= low_watermark_) {
            d.admit = true;
            board_->metrics()->set_backpressure_level(1);
        } else {
            d.admit = true;
            board_->metrics()->set_backpressure_level(0);
        }
        return d;
    }

    void set_high_watermark(std::uint64_t v) { high_watermark_ = v; }
    void set_low_watermark(std::uint64_t v) { low_watermark_ = v; }
    void set_queue_high_watermark(std::uint64_t v) { queue_high_watermark_ = v; }

private:
    std::shared_ptr<databoard::DataBoard> board_;
    std::uint64_t high_watermark_{1024};
    std::uint64_t low_watermark_{512};
    std::uint64_t queue_high_watermark_{2048};
};

}  // namespace tts::heartbeat