#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

#include "entry/jwt_auth_interceptor.h"

using namespace tts::entry;

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
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == '=')) out.pop_back();
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

struct KeyPair {
    EVP_PKEY* pkey{nullptr};
    ~KeyPair() { if (pkey) EVP_PKEY_free(pkey); }
};

KeyPair generate_rsa() {
    KeyPair kp;
    EVP_PKEY_CTX* pctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY_keygen_init(pctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(pctx, 2048);
    EVP_PKEY_keygen(pctx, &kp.pkey);
    EVP_PKEY_CTX_free(pctx);
    return kp;
}

std::filesystem::path write_public_key(EVP_PKEY* pkey) {
    auto p = std::filesystem::temp_directory_path() /
             ("jwt_pub_" + std::to_string(std::rand()) + ".pem");
    FILE* fp = std::fopen(p.string().c_str(), "w");
    PEM_write_PUBKEY(fp, pkey);
    std::fclose(fp);
    return p;
}

std::string make_token(EVP_PKEY* pkey, const std::string& payload_json,
                       long long exp_offset_seconds) {
    using namespace std::chrono;
    auto exp = duration_cast<seconds>(system_clock::now().time_since_epoch()).count()
               + exp_offset_seconds;
    std::string payload = payload_json;
    payload += ",\"exp\":" + std::to_string(exp);
    std::string header = R"({"alg":"RS256","typ":"JWT"})";
    std::string h = base64url_encode(header);
    std::string p = base64url_encode(payload);
    std::string sig = sign_rsa256(pkey, h + "." + p);
    std::string s = base64url_encode(sig);
    return h + "." + p + "." + s;
}

}  // namespace

TEST(JwtVerifier, ValidToken) {
    auto kp = generate_rsa();
    auto path = write_public_key(kp.pkey);
    auto v = JwtVerifier::from_rsa256_public_key_file(path);
    ASSERT_NE(v, nullptr);
    auto t = make_token(kp.pkey, R"({"iss":"tts-service","sub":"alice"})", 3600);
    EXPECT_EQ(v->verify(t, "tts-service"), JwtVerifier::Result::Ok);
    std::filesystem::remove(path);
}

TEST(JwtVerifier, ExpiredToken) {
    auto kp = generate_rsa();
    auto path = write_public_key(kp.pkey);
    auto v = JwtVerifier::from_rsa256_public_key_file(path);
    auto t = make_token(kp.pkey, R"({"iss":"tts-service"})", -10);
    EXPECT_EQ(v->verify(t, "tts-service"), JwtVerifier::Result::Expired);
    std::filesystem::remove(path);
}

TEST(JwtVerifier, WrongIssuer) {
    auto kp = generate_rsa();
    auto path = write_public_key(kp.pkey);
    auto v = JwtVerifier::from_rsa256_public_key_file(path);
    auto t = make_token(kp.pkey, R"({"iss":"other-service"})", 3600);
    EXPECT_EQ(v->verify(t, "tts-service"), JwtVerifier::Result::IssuerMismatch);
    std::filesystem::remove(path);
}

TEST(JwtVerifier, TamperedSignature) {
    auto kp = generate_rsa();
    auto path = write_public_key(kp.pkey);
    auto v = JwtVerifier::from_rsa256_public_key_file(path);
    auto t = make_token(kp.pkey, R"({"iss":"tts-service"})", 3600);
    t.back() = 'A';
    EXPECT_NE(v->verify(t, "tts-service"), JwtVerifier::Result::Ok);
    std::filesystem::remove(path);
}

TEST(JwtVerifier, Malformed) {
    auto kp = generate_rsa();
    auto path = write_public_key(kp.pkey);
    auto v = JwtVerifier::from_rsa256_public_key_file(path);
    EXPECT_EQ(v->verify("not.a.token.but.four.parts", ""), JwtVerifier::Result::Malformed);
    std::filesystem::remove(path);
}

TEST(JwtVerifier, Missing) {
    auto kp = generate_rsa();
    auto path = write_public_key(kp.pkey);
    auto v = JwtVerifier::from_rsa256_public_key_file(path);
    EXPECT_EQ(v->verify("", ""), JwtVerifier::Result::MissingHeader);
    std::filesystem::remove(path);
}