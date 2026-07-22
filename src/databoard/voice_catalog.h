#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace tts::databoard {

struct VoiceInfo {
    std::string id;
    std::string display_name;
    std::string language;
};

class VoiceCatalog {
public:
    VoiceCatalog() = default;

    void set_voices(std::vector<VoiceInfo> voices) {
        voices_ = std::move(voices);
        indexed_ = false;
        if (!indexed_) index();
    }

    [[nodiscard]] bool empty() const noexcept { return voices_.empty(); }

    [[nodiscard]] std::size_t size() const noexcept { return voices_.size(); }

    [[nodiscard]] bool has(const std::string& id) const {
        ensure_indexed();
        return index_.find(id) != index_.end();
    }

    [[nodiscard]] const std::vector<VoiceInfo>& voices() const noexcept { return voices_; }

private:
    void ensure_indexed() const {
        if (!indexed_) index();
    }

    void index() const {
        index_.clear();
        for (const auto& v : voices_) index_.insert(v.id);
        indexed_ = true;
    }

    std::vector<VoiceInfo> voices_;
    mutable std::unordered_set<std::string> index_;
    mutable bool indexed_{false};
};

}  // namespace tts::databoard