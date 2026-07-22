#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include "pb2/tts.grpc.pb.h"

namespace {

std::string base64url_encode(const std::string& in) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO* mem = BIO_new(BIO_s_mem());
    mem = BIO_push(b64, mem);
    BIO_write(mem, in.data(), static_cast<int>(in.size()));
    BIO_flush(mem);
    BUF_MEM* bptr = nullptr;
    BIO_get_mem_ptr(mem, &bptr);
    std::string out(bptr->data, bptr->length);
    BIO_free_all(mem);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == '=')) {
        out.pop_back();
    }
    for (auto& c : out) {
        if (c == '+') c = '-';
        else if (c == '/') c = '_';
    }
    return out;
}

std::string sign_rsa256(EVP_PKEY* pkey, const std::string& data) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestSignInit(ctx, nullptr, EVP_sha256(), nullptr, pkey);
    EVP_DigestSignUpdate(ctx, data.data(), data.size());
    std::size_t len = 0;
    EVP_DigestSignFinal(ctx, nullptr, &len);
    std::vector<unsigned char> sig(len);
    EVP_DigestSignFinal(ctx, sig.data(), &len);
    EVP_MD_CTX_free(ctx);
    return std::string(reinterpret_cast<char*>(sig.data()), len);
}

std::string mint_jwt(const std::filesystem::path& privkey_path,
                     const std::string& iss) {
    FILE* fp = std::fopen(privkey_path.string().c_str(), "r");
    EVP_PKEY* pkey = PEM_read_PrivateKey(fp, nullptr, nullptr, nullptr);
    std::fclose(fp);
    if (!pkey) return {};

    using namespace std::chrono;
    long long exp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count() + 3600;
    std::ostringstream p;
    p << R"({"alg":"RS256","typ":"JWT"})";
    std::string header = p.str();
    p.str("");
    p.clear();
    p << "{\"iss\":\"" << iss << "\",\"sub\":\"smoke\",\"exp\":" << exp << "}";
    std::string payload = p.str();

    std::string h = base64url_encode(header);
    std::string pl = base64url_encode(payload);
    std::string sig = sign_rsa256(pkey, h + "." + pl);
    std::string s = base64url_encode(sig);
    EVP_PKEY_free(pkey);
    return h + "." + pl + "." + s;
}

}  // namespace

int main(int argc, char** argv) {
    std::string target = "localhost:50061";
    std::string privkey = "keys/jwt_private.pem";
    std::string text = "你好，世界。这是 MiniMax-TTS 的中文端到端测试。";
    if (argc > 1) target = argv[1];
    if (argc > 2) privkey = argv[2];
    if (argc > 3) text = argv[3];

    auto token = mint_jwt(privkey, "tts-service");
    if (token.empty()) {
        std::cerr << "failed to mint JWT from " << privkey << "\n";
        return 2;
    }
    std::cerr << "jwt len=" << token.size() << " prefix=" << token.substr(0, 16) << "\n";

    auto stub = ::tts::TTS::NewStub(grpc::CreateChannel(
        target, grpc::InsecureChannelCredentials()));

    ::tts::SynthesizeRequest req;
    req.set_text(text);
    req.set_model(::tts::SPEECH_2_8_TURBO);
    auto* voice = req.mutable_voice();
    voice->set_voice_id("0");
    voice->set_speed(1.0f);
    voice->set_vol(1.0f);
    auto* audio = req.mutable_audio();
    audio->set_sample_rate(16000);
    audio->set_bitrate(128000);
    audio->set_format(::tts::PCM);
    audio->set_channel(1);
    req.set_backend(::tts::BACKEND_LOCAL);

    grpc::ClientContext ctx;
    ctx.AddMetadata("authorization", "Bearer " + token);

    auto reader = stub->SynthesizeStream(&ctx, req);

    std::size_t total = 0;
    int chunks = 0;
    int sample_rate = 0;
    int channels = 0;
    int status = 0;
    std::string status_msg;
    std::string trace_id;

    ::tts::SynthesizeResponse resp;
    std::vector<std::uint8_t> pcm_data;
    while (reader->Read(&resp)) {
        if (!resp.audio().empty()) {
            total += resp.audio().size();
            chunks++;
            pcm_data.insert(pcm_data.end(), resp.audio().begin(), resp.audio().end());
        }
        if (resp.has_meta()) {
            sample_rate = resp.meta().sample_rate();
            channels = resp.meta().channels();
        }
        status = resp.status_code();
        if (!resp.status_msg().empty()) status_msg = resp.status_msg();
        if (!resp.trace_id().empty()) trace_id = resp.trace_id();
        if (resp.is_final()) break;
    }

    auto status_code = reader->Finish().ok() ? 0 : 1;

    std::cout << "=== Chinese end-to-end smoke test ===\n"
              << "text: " << text << "\n"
              << "chunks: " << chunks << "\n"
              << "total_bytes: " << total << "\n"
              << "sample_rate: " << sample_rate << "\n"
              << "channels: " << channels << "\n"
              << "status_code: " << status << "\n"
              << "status_msg: " << status_msg << "\n"
              << "trace_id: " << trace_id << "\n"
              << "rpc_ok: " << (status_code == 0 ? "yes" : "no") << "\n";

    bool ok = (total > 0) && (sample_rate == 16000) && (channels == 1) && (status_code == 0);
    std::cout << (ok ? "PASS" : "FAIL") << "\n";

    if (ok && !pcm_data.empty()) {
        std::string wav_path = "chinese_tts_output.wav";
        if (argc > 4) wav_path = argv[4];
        std::ofstream wav(wav_path, std::ios::binary);
        if (wav.is_open()) {
            const std::uint32_t sr = static_cast<std::uint32_t>(sample_rate);
            const std::uint16_t ch = static_cast<std::uint16_t>(channels);
            const std::uint16_t bits = 16;
            const std::uint32_t byte_rate = sr * ch * bits / 8;
            const std::uint16_t block_align = ch * bits / 8;
            const std::uint32_t data_size = static_cast<std::uint32_t>(pcm_data.size());
            const std::uint32_t chunk_size = 36 + data_size;

            auto write_le32 = [&](std::uint32_t v) {
                wav.put(static_cast<char>(v & 0xff));
                wav.put(static_cast<char>((v >> 8) & 0xff));
                wav.put(static_cast<char>((v >> 16) & 0xff));
                wav.put(static_cast<char>((v >> 24) & 0xff));
            };
            auto write_le16 = [&](std::uint16_t v) {
                wav.put(static_cast<char>(v & 0xff));
                wav.put(static_cast<char>((v >> 8) & 0xff));
            };

            wav.write("RIFF", 4);
            write_le32(chunk_size);
            wav.write("WAVE", 4);
            wav.write("fmt ", 4);
            write_le32(16);
            write_le16(1);
            write_le16(ch);
            write_le32(sr);
            write_le32(byte_rate);
            write_le16(block_align);
            write_le16(bits);
            wav.write("data", 4);
            write_le32(data_size);
            wav.write(reinterpret_cast<const char*>(pcm_data.data()),
                      static_cast<std::streamsize>(pcm_data.size()));
            wav.close();
            std::cout << "wav: " << wav_path << " (" << data_size << " bytes, "
                      << (data_size / (sr * ch * bits / 8)) << "s)\n";
        } else {
            std::cerr << "failed to write wav: " << wav_path << "\n";
        }
    }

    return ok ? 0 : 1;
}