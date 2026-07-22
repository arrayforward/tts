#pragma once

#include "framework/change_set.h"

namespace tts {
class SynthesizeResponse;
}

namespace tts::entry {

class GrpcResponseWriter {
public:
    static void to_proto(const framework::WriteToClient& w, ::tts::SynthesizeResponse* out);
};

}  // namespace tts::entry