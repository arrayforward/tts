#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace tts::upstream {

inline constexpr std::string_view kDefaultEndpoint = "wss://api.minimaxi.com/ws/v1/t2a_v2";

[[nodiscard]] inline std::string hex_decode(std::string_view hex) {
    static const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    if (hex.size() % 2 != 0) return {};
    std::string out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        int hi = nibble(hex[i]);
        int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return {};
        out.push_back(static_cast<char>((hi << 4) | lo));
    }
    return out;
}

inline std::string hex_encode(std::string_view bytes) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out.push_back(digits[(c >> 4) & 0xF]);
        out.push_back(digits[c & 0xF]);
    }
    return out;
}

}  // namespace tts::upstream