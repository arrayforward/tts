#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include <grpcpp/server.h>

namespace tts::entry {

class JwtVerifier {
public:
    enum class Result { Ok, MissingHeader, InvalidSignature, Expired, Malformed, IssuerMismatch };

    static std::unique_ptr<JwtVerifier> from_rsa256_public_key_file(
        const std::filesystem::path& path);

    virtual ~JwtVerifier() = default;
    virtual Result verify(const std::string& token,
                          const std::string& required_issuer = {}) const = 0;
};

class JwtAuthInterceptor : public JwtVerifier {
public:
    explicit JwtAuthInterceptor(std::unique_ptr<JwtVerifier> inner)
        : inner_(std::move(inner)) {}

    JwtAuthInterceptor(const JwtAuthInterceptor&) = delete;
    JwtAuthInterceptor& operator=(const JwtAuthInterceptor&) = delete;

    Result verify(const std::string& token, const std::string& issuer) const override {
        if (!inner_) return Result::Malformed;
        return inner_->verify(token, issuer);
    }

private:
    std::unique_ptr<JwtVerifier> inner_;
};

}  // namespace tts::entry