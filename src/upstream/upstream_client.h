#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

namespace tts::upstream {

struct WsEndpointConfig {
    std::string url{"wss://api.minimaxi.com/ws/v1/t2a_v2"};
    std::string api_key;
    std::size_t max_payload_bytes{4 * 1024 * 1024};
    int connect_timeout_ms{5000};
    int idle_ping_ms{15000};
};

struct WsRawFrame {
    enum class Kind { Text, Binary, Close, Error, Open } kind{Kind::Text};
    std::string payload;
    int close_code{0};
    std::string close_reason;
    std::string error;
};

class IWsTransport {
public:
    virtual ~IWsTransport() = default;

    using OpenCallback = std::function<void(const std::string& session_id)>;
    using FrameCallback = std::function<void(WsRawFrame)>;
    using CloseCallback = std::function<void(int code, const std::string& reason)>;

    virtual void open(const WsEndpointConfig& cfg,
                      OpenCallback on_open,
                      FrameCallback on_frame,
                      CloseCallback on_close) = 0;

    virtual bool send_text(const std::string& payload) = 0;
    virtual void close(int code, const std::string& reason) = 0;

    virtual bool is_open() const = 0;
};

class MockWsTransport final : public IWsTransport {
public:
    OpenCallback on_open_;
    FrameCallback on_frame_;
    CloseCallback on_close_;

    void open(const WsEndpointConfig&,
              OpenCallback on_open,
              FrameCallback on_frame,
              CloseCallback on_close) override {
        on_open_ = std::move(on_open);
        on_frame_ = std::move(on_frame);
        on_close_ = std::move(on_close);
        open_ = true;
    }

    bool send_text(const std::string&) override { return true; }
    void close(int code, const std::string& reason) override {
        open_ = false;
        if (on_close_) on_close_(code, reason);
    }
    bool is_open() const override { return open_; }

    void inject_open(const std::string& session_id) {
        if (on_open_) on_open_(session_id);
    }

    void inject_text(const std::string& text) {
        if (on_frame_) on_frame_(WsRawFrame{WsRawFrame::Kind::Text, text, 0, {}, {}});
    }

    void inject_close(int code, const std::string& reason) {
        open_ = false;
        if (on_frame_) on_frame_(WsRawFrame{WsRawFrame::Kind::Close, {}, code, reason, {}});
        if (on_close_) on_close_(code, reason);
    }

private:
    std::atomic<bool> open_{false};
};

class UpstreamClient {
public:
    explicit UpstreamClient(std::unique_ptr<IWsTransport> transport)
        : transport_(std::move(transport)) {}

    ~UpstreamClient() {
        if (transport_) transport_->close(1000, "shutdown");
    }

    IWsTransport* transport() const noexcept { return transport_.get(); }

    void open(const WsEndpointConfig& cfg) {
        transport_->open(cfg,
            [this](const std::string& sid) {
                session_id_ = sid;
            },
            [this](WsRawFrame f) {
                handle_frame(std::move(f));
            },
            [this](int code, const std::string& reason) {
                session_id_.clear();
            });
    }

    void send(const std::string& text) {
        if (transport_ && transport_->is_open()) transport_->send_text(text);
    }

    void close() {
        if (transport_) transport_->close(1000, "client_close");
    }

    [[nodiscard]] const std::string& session_id() const noexcept { return session_id_; }
    [[nodiscard]] bool is_open() const noexcept {
        return transport_ && transport_->is_open();
    }

private:
    void handle_frame(WsRawFrame) {}

    std::unique_ptr<IWsTransport> transport_;
    std::string session_id_;
};

std::unique_ptr<IWsTransport> make_lws_transport();

}  // namespace tts::upstream