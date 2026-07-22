#include "jwt_auth_interceptor.h"

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace tts::entry {

namespace {

std::string base64url_decode(std::string_view s) {
    std::string in(s);
    std::replace(in.begin(), in.end(), '-', '+');
    std::replace(in.begin(), in.end(), '_', '/');
    while (in.size() % 4 != 0) in.push_back('=');

    BIO* b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO* mem = BIO_new_mem_buf(in.data(), static_cast<int>(in.size()));
    mem = BIO_push(b64, mem);

    std::string out;
    out.resize(in.size());
    int n = BIO_read(mem, out.data(), static_cast<int>(out.size()));
    BIO_free_all(mem);
    if (n < 0) throw std::runtime_error("base64 decode failed");
    out.resize(n);
    return out;
}

std::vector<std::string_view> split_dot(std::string_view s) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '.') {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    parts.push_back(s.substr(start));
    return parts;
}

bool constant_time_eq(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char acc = 0;
    for (std::size_t i = 0; i < a.size(); ++i) acc |= a[i] ^ b[i];
    return acc == 0;
}

bool verify_rsa_sha256(EVP_PKEY* pkey,
                       const std::string& signed_data,
                       const std::string& signature) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;
    bool ok = false;
    if (EVP_DigestVerifyInit(ctx, nullptr, EVP_sha256(), nullptr, pkey) == 1) {
        if (EVP_DigestVerifyUpdate(ctx, signed_data.data(), signed_data.size()) == 1) {
            int rc = EVP_DigestVerifyFinal(ctx,
                                           reinterpret_cast<const unsigned char*>(signature.data()),
                                           signature.size());
            ok = (rc == 1);
        }
    }
    EVP_MD_CTX_free(ctx);
    return ok;
}

EVP_PKEY* load_public_key_pem(const std::filesystem::path& path) {
    FILE* fp = std::fopen(path.string().c_str(), "r");
    if (!fp) return nullptr;
    EVP_PKEY* pkey = PEM_read_PUBKEY(fp, nullptr, nullptr, nullptr);
    std::fclose(fp);
    return pkey;
}

}  // namespace

class Rsa256Verifier final : public JwtVerifier {
public:
    explicit Rsa256Verifier(EVP_PKEY* pkey) : pkey_(pkey) {}
    ~Rsa256Verifier() override { if (pkey_) EVP_PKEY_free(pkey_); }

    Result verify(const std::string& token, const std::string& required_issuer) const override {
        if (token.empty()) return Result::MissingHeader;
        auto parts = split_dot(token);
        if (parts.size() != 3) return Result::Malformed;

        std::string header_json, payload_json;
        try {
            header_json = base64url_decode(parts[0]);
            payload_json = base64url_decode(parts[1]);
        } catch (...) {
            return Result::Malformed;
        }
        std::string sig_bytes;
        try {
            sig_bytes = base64url_decode(parts[2]);
        } catch (...) {
            return Result::Malformed;
        }

        std::string signed_data;
        signed_data.append(parts[0]).append(".").append(parts[1]);
        if (!verify_rsa_sha256(pkey_, signed_data, sig_bytes)) {
            return Result::InvalidSignature;
        }

        if (header_json.find("\"alg\":\"RS256\"") == std::string::npos) {
            return Result::InvalidSignature;
        }

        auto find_value = [](const std::string& json, const std::string& key) -> std::string {
            auto k = "\"" + key + "\":";
            auto p = json.find(k);
            if (p == std::string::npos) return {};
            p += k.size();
            if (p < json.size() && json[p] == '"') {
                auto e = json.find('"', p + 1);
                if (e == std::string::npos) return {};
                return json.substr(p + 1, e - p - 1);
            }
            auto e = json.find_first_of(",}", p);
            if (e == std::string::npos) e = json.size();
            return json.substr(p, e - p);
        };

        auto exp_str = find_value(payload_json, "exp");
        if (!exp_str.empty()) {
            try {
                long long exp = std::stoll(exp_str);
                auto now = std::chrono::duration_cast<std::chrono::seconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
                if (now >= exp) return Result::Expired;
            } catch (...) {}
        }
        if (!required_issuer.empty()) {
            auto iss = find_value(payload_json, "iss");
            if (!constant_time_eq(iss, required_issuer)) {
                return Result::IssuerMismatch;
            }
        }
        return Result::Ok;
    }

private:
    EVP_PKEY* pkey_;
};

std::unique_ptr<JwtVerifier> JwtVerifier::from_rsa256_public_key_file(
    const std::filesystem::path& path) {
    EVP_PKEY* pkey = load_public_key_pem(path);
    if (!pkey) return nullptr;
    return std::make_unique<Rsa256Verifier>(pkey);
}

}  // namespace tts::entry