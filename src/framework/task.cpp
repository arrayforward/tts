#include "task.h"

#include <chrono>

#include <spdlog/spdlog.h>

namespace tts::framework {

void Task::run_with_slow_warning() const {
    auto start = std::chrono::steady_clock::now();
    if (fn_) fn_();
    auto end = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    auto threshold_us =
        std::chrono::duration_cast<std::chrono::microseconds>(slow_threshold(kind_));
    if (elapsed > threshold_us) {
        spdlog::warn("slow task id={} name={} kind={} elapsed_us={} threshold_us={} at {}:{}",
                     id_.to_string(), name_,
                     kind_ == TaskKind::Cpu ? "cpu" : "io",
                     elapsed.count(), threshold_us.count(),
                     loc_.file_name(), loc_.line());
    }
}

}  // namespace tts::framework