#include "upstream/upstream_client.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include <libwebsockets.h>
#include <spdlog/spdlog.h>

namespace tts::upstream {

class LwsClient;

namespace {

struct PerSessionData {
    LwsClient* owner{nullptr};
};

}  // namespace

class LwsClient final : public IWsTransport {
public:
    LwsClient() {
        std::memset(protocols_, 0, sizeof(protocols_));
        protocols_[0].name = "tts";
        protocols_[0].callback = &LwsClient::callback_static;
        protocols_[0].per_session_data_size = 0;
        protocols_[0].rx_buffer_size = 65536;
    }
    ~LwsClient() override { close_internal(1000, "shutdown"); }

    void open(const WsEndpointConfig& cfg,
              OpenCallback on_open,
              FrameCallback on_frame,
              CloseCallback on_close) override {
        on_open_ = std::move(on_open);
        on_frame_ = std::move(on_frame);
        on_close_ = std::move(on_close);
        endpoint_ = cfg;
        connect_thread_ = std::jthread([this](std::stop_token st) { run_loop(st); });
    }

    bool send_text(const std::string& payload) override {
        {
            std::lock_guard lk(write_mtx_);
            write_queue_.push(payload);
        }
        if (wsi_) lws_callback_on_writable(wsi_);
        return true;
    }

    void close(int code, const std::string& reason) override {
        close_internal(code, reason);
    }

    bool is_open() const override { return connected_.load(); }

private:
    void close_internal(int code, const std::string& reason) {
        if (closed_) return;
        closed_ = true;
        connected_ = false;
        if (wsi_) {
            lws_callback_on_writable(wsi_);
        }
        if (connect_thread_.joinable()) connect_thread_.request_stop();
        if (loop_thread_.joinable()) loop_thread_.join();
        if (connect_thread_.joinable()) connect_thread_.join();
        if (on_close_) on_close_(code, reason);
    }

    void run_loop(std::stop_token st) {
        lws_context_creation_info info{};
        info.port = CONTEXT_PORT_NO_LISTEN;
        info.gid = -1;
        info.uid = -1;
        info.protocols = protocols_;
        info.options = LWS_SERVER_OPTION_VALIDATE_UTF8;

        context_ = lws_create_context(&info);
        if (!context_) {
            if (on_close_) on_close_(1006, "context_create_failed");
            return;
        }
        connect_wsi();
        loop_thread_ = std::jthread([this](std::stop_token st2) { service_loop(st2); });
        while (!st.stop_requested() && !closed_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (context_) {
            lws_context_destroy(context_);
            context_ = nullptr;
        }
    }

    void service_loop(std::stop_token st) {
        while (!st.stop_requested() && !closed_ && context_) {
            lws_service(context_, 50);
        }
    }

    void connect_wsi() {
        lws_client_connect_info ci{};
        ci.context = context_;
        ci.address = "api.minimaxi.com";
        ci.port = 443;
        ci.path = "/ws/v1/t2a_v2";
        ci.host = "api.minimaxi.com";
        ci.origin = "api.minimaxi.com";
        ci.protocol = "";
        ci.pwsi = &wsi_;
        ci.userdata = &psd_;
        psd_.owner = this;

        wsi_ = lws_client_connect_via_info(&ci);
    }

    int callback(struct lws* wsi, lws_callback_reasons reason,
                 void* user, void* in, std::size_t len) {
        switch (reason) {
            case LWS_CALLBACK_CLIENT_ESTABLISHED: {
                connected_ = true;
                session_id_ = generate_session_id();
                if (on_open_) on_open_(session_id_);
                lws_callback_on_writable(wsi);
                break;
            }
            case LWS_CALLBACK_CLIENT_RECEIVE: {
                if (in && len > 0) {
                    std::string chunk(static_cast<const char*>(in), len);
                    {
                        std::lock_guard lk(recv_mtx_);
                        recv_buffer_ += chunk;
                        auto pos = recv_buffer_.find('\n');
                        if (pos != std::string::npos) {
                            std::string line = recv_buffer_.substr(0, pos);
                            recv_buffer_.erase(0, pos + 1);
                            if (on_frame_) on_frame_(
                                WsRawFrame{WsRawFrame::Kind::Text, line, 0, {}, {}});
                        }
                    }
                }
                break;
            }
            case LWS_CALLBACK_CLIENT_WRITEABLE: {
                std::string payload;
                {
                    std::lock_guard lk(write_mtx_);
                    if (!write_queue_.empty()) {
                        payload = std::move(write_queue_.front());
                        write_queue_.pop();
                    }
                }
                if (!payload.empty()) {
                    std::vector<unsigned char> buf(LWS_PRE + payload.size());
                    std::memcpy(buf.data() + LWS_PRE, payload.data(), payload.size());
                    lws_write(wsi, buf.data() + LWS_PRE, payload.size(), LWS_WRITE_TEXT);
                }
                if (!write_queue_.empty()) lws_callback_on_writable(wsi);
                break;
            }
            case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
                if (on_frame_) on_frame_(
                    WsRawFrame{WsRawFrame::Kind::Error, {}, 0, {},
                               in ? std::string(static_cast<const char*>(in)) : "connection_error"});
                close_internal(1006, "connection_error");
                break;
            }
            case LWS_CALLBACK_CLIENT_CLOSED: {
                close_internal(1000, "closed");
                break;
            }
            default:
                break;
        }
        return 0;
    }

    static int callback_static(struct lws* wsi, lws_callback_reasons reason,
                               void* user, void* in, std::size_t len) {
        auto* psd = static_cast<PerSessionData*>(user);
        if (!psd || !psd->owner) return 0;
        return psd->owner->callback(wsi, reason, user, in, len);
    }

    static std::string generate_session_id() {
        static std::atomic<std::uint64_t> ctr{1};
        return "sess-" + std::to_string(ctr.fetch_add(1));
    }

    PerSessionData psd_;
    lws* wsi_{nullptr};
    lws_context* context_{nullptr};
    lws_protocols protocols_[2];

    WsEndpointConfig endpoint_;
    OpenCallback on_open_;
    FrameCallback on_frame_;
    CloseCallback on_close_;

    std::atomic<bool> connected_{false};
    std::atomic<bool> closed_{false};
    std::string session_id_;

    std::mutex write_mtx_;
    std::queue<std::string> write_queue_;

    std::mutex recv_mtx_;
    std::string recv_buffer_;

    std::jthread connect_thread_;
    std::jthread loop_thread_;
};

std::unique_ptr<IWsTransport> make_lws_transport() {
    return std::make_unique<LwsClient>();
}

}  // namespace tts::upstream