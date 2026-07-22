#include "config.h"

#include <cstdlib>
#include <stdexcept>
#include <thread>

namespace tts {

namespace {

std::string env_or(const char* k, const std::string& def) {
    const char* v = std::getenv(k);
    return v ? std::string(v) : def;
}

std::size_t env_size(const char* k, std::size_t def) {
    const char* v = std::getenv(k);
    if (!v || !*v) return def;
    try {
        return std::stoull(v);
    } catch (...) {
        return def;
    }
}

}  // namespace

Config Config::from_env() {
    Config c;
    c.grpc_bind = env_or("GRPC_BIND", c.grpc_bind);
    c.upstream_url = env_or("MINIMAX_WS_URL", c.upstream_url);
    c.minimax_api_key = env_or("MINIMAX_API_KEY", "");
    const auto pk = env_or("GRPC_JWT_PUBLIC_KEY_FILE", "");
    if (!pk.empty()) c.jwt_public_key_file = pk;
    c.jwt_issuer = env_or("GRPC_JWT_ISSUER", c.jwt_issuer);
    c.auth_enabled = env_or("GRPC_AUTH_ENABLED", "false") == "true";

    if (auto v = std::getenv("TTS_POOL_MAX")) c.pool_max_size = std::stoull(v);
    if (auto v = std::getenv("TTS_POOL_IDLE_MS")) {
        c.pool_idle_timeout = std::chrono::seconds(std::stoull(v) / 1000);
    }
    if (auto v = std::getenv("TTS_POOL_LIFETIME_S")) {
        c.pool_max_lifetime = std::chrono::seconds(std::stoull(v));
    }
    if (auto v = std::getenv("TTS_HEARTBEAT_MS")) {
        c.heartbeat_interval = std::chrono::milliseconds(std::stoull(v));
    }
    c.cpu_workers = env_size("TTS_CPU_WORKERS", std::max<std::size_t>(1, std::thread::hardware_concurrency() * 3 / 2));
    c.io_workers = env_size("TTS_IO_WORKERS", std::max<std::size_t>(2, std::thread::hardware_concurrency() * 2));
    c.bus_workers = env_size("TTS_BUS_WORKERS", 1);

    c.local.enabled = env_or("TTS_LOCAL_ENABLED", "true") != "false";
    c.local.prefer_local = env_or("TTS_LOCAL_PREFER", "true") != "false";
    c.local.backend = env_or("TTS_LOCAL_BACKEND", "sherpa");
    if (auto v = std::getenv("TTS_LOCAL_MODEL_DIR")) c.local.model_dir = v;
    if (auto v = std::getenv("TTS_LOCAL_NUM_THREADS")) {
        try { c.local.num_threads = std::stoi(std::string(v)); } catch (...) {}
    }
    return c;
}

void Config::validate() const {
    if (auth_enabled) {
        if (jwt_public_key_file.empty()) {
            throw std::runtime_error("GRPC_JWT_PUBLIC_KEY_FILE is empty (required when GRPC_AUTH_ENABLED=true)");
        }
        if (!std::filesystem::exists(jwt_public_key_file)) {
            throw std::runtime_error("GRPC_JWT_PUBLIC_KEY_FILE does not exist");
        }
    }
    if (pool_max_size == 0) {
        throw std::runtime_error("TTS_POOL_MAX must be > 0");
    }
    if (local.enabled && local.backend == "sherpa") {
        if (local.model_dir.empty()) {
            throw std::runtime_error("TTS_LOCAL_MODEL_DIR is empty (required when local is enabled)");
        }
        if (!std::filesystem::exists(local.model_dir)) {
            throw std::runtime_error("TTS_LOCAL_MODEL_DIR does not exist: " + local.model_dir);
        }
    }
    if (!local.enabled) {
        if (minimax_api_key.empty()) {
            throw std::runtime_error("MINIMAX_API_KEY is empty (required when local is disabled)");
        }
    }
}

}  // namespace tts