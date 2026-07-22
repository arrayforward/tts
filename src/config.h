#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace tts {

struct LocalConfig {
    bool enabled{true};
    bool prefer_local{true};
    std::string backend{"sherpa"};
    std::string model_dir;
    std::string model_file{"model.onnx"};
    std::string detokenizer_file{"detokenizer.onnx"};
    std::string tokenizer_file{"tokens.txt"};
    int target_sample_rate{16000};
    int target_channels{1};
    int num_threads{0};
    int max_decode_steps{512};
};

struct Config {
    std::string grpc_bind{"0.0.0.0:50061"};
    std::string upstream_url{"wss://api.minimaxi.com/ws/v1/t2a_v2"};
    std::string minimax_api_key;
    std::filesystem::path jwt_public_key_file;
    std::string jwt_issuer{"tts-service"};
    bool auth_enabled{false};
    std::chrono::milliseconds heartbeat_interval{50};
    std::chrono::seconds pool_idle_timeout{60};
    std::chrono::seconds pool_max_lifetime{3600};
    std::size_t pool_max_size{16};
    std::size_t cpu_workers{0};
    std::size_t io_workers{0};
    std::size_t bus_workers{1};
    LocalConfig local;

    static Config from_env();
    void validate() const;
};

}  // namespace tts