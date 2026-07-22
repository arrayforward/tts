#pragma once

#include <stdexcept>
#include <string>

#include "framework/messages.h"

namespace tts {
class SynthesizeRequest;
class TextChunk;
enum Backend : int;
}

namespace tts::entry {

class ParseError : public std::runtime_error {
public:
    explicit ParseError(const std::string& w) : std::runtime_error(w) {}
};

struct ParsedSynthesize {
    framework::TtsRequestMsg request;
};

class RequestParser {
public:
    ParsedSynthesize parse_unary(const ::tts::SynthesizeRequest& proto,
                                 const std::string& request_id) const;

    ParsedSynthesize parse_chunk(const ::tts::TextChunk& proto,
                                 const std::string& request_id,
                                 const ::tts::SynthesizeRequest& initial_settings,
                                 bool first_chunk) const;

private:
    framework::Backend to_backend(::tts::Backend proto_backend) const;
};

}  // namespace tts::entry