// mediator_synth_test — 调用 mediator.TtsService/Synth 并校验返回 PCM。
// 用法: mediator_synth_test <host:port> <text> <session_id> <clip_id> [out.wav]
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <grpcpp/grpcpp.h>
#include "pb2/mediator_tts.grpc.pb.h"

namespace {
void write_wav_header(std::ostream& os, std::uint32_t data_bytes, std::uint32_t sample_rate) {
    auto put = [&](const char* tag, std::uint32_t v) { os.write(tag, 4); os.write(reinterpret_cast<const char*>(&v), 4); };
    std::uint32_t byte_rate = sample_rate * 2;  // mono int16
    std::uint16_t block_align = 2, bits = 16, fmt_tag = 1, channels = 1, sub1 = 16;
    put("RIFF", 36 + data_bytes);
    os.write("WAVE", 4);
    os.write("fmt ", 4);
    os.write(reinterpret_cast<const char*>(&sub1), 4);
    os.write(reinterpret_cast<const char*>(&fmt_tag), 2);
    os.write(reinterpret_cast<const char*>(&channels), 2);
    os.write(reinterpret_cast<const char*>(&sample_rate), 4);
    os.write(reinterpret_cast<const char*>(&byte_rate), 4);
    os.write(reinterpret_cast<const char*>(&block_align), 2);
    os.write(reinterpret_cast<const char*>(&bits), 2);
    put("data", data_bytes);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: " << argv[0] << " <host:port> <text> <session_id> <clip_id> [out.wav]\n";
        return 2;
    }
    std::string target = argv[1];
    std::string text = argv[2];
    std::string session_id = argv[3];
    std::uint32_t clip_id = static_cast<std::uint32_t>(std::stoul(argv[4]));

    auto channel = grpc::CreateChannel(target, grpc::InsecureChannelCredentials());
    auto stub = ::mediator::TtsService::NewStub(channel);

    grpc::ClientContext ctx;
    ctx.set_deadline(std::chrono::system_clock::now() + std::chrono::seconds(120));
    ::mediator::TtsRequest req;
    req.set_text(text);
    req.set_session_id(session_id);
    req.set_clip_id(clip_id);
    ::mediator::TtsResponse resp;
    auto status = stub->Synth(&ctx, req, &resp);
    if (!status.ok()) {
        std::cerr << "Synth failed: code=" << status.error_code()
                  << " msg=" << status.error_message() << "\n";
        return 1;
    }

    const auto& pcm = resp.pcm();
    std::cout << "pcm_bytes=" << pcm.size() << "\n";
    if (pcm.empty()) {
        std::cerr << "FAIL: pcm empty\n";
        return 1;
    }
    if (pcm.size() % 2 != 0) {
        std::cerr << "FAIL: pcm byte count not a multiple of 2\n";
        return 1;
    }
    const double seconds = static_cast<double>(pcm.size()) / 2.0 / 16000.0;
    std::cout << "duration_s=" << seconds << "\n";
    if (seconds < 0.2 || seconds > 60.0) {
        std::cerr << "FAIL: unreasonable duration\n";
        return 1;
    }

    if (argc > 5) {
        std::ofstream os(argv[5], std::ios::binary);
        write_wav_header(os, static_cast<std::uint32_t>(pcm.size()), 16000);
        os.write(pcm.data(), static_cast<std::streamsize>(pcm.size()));
        std::cout << "wav_written=" << argv[5] << "\n";
    }
    std::cout << "PASS\n";
    return 0;
}
