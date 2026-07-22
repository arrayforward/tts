#include "grpc_response_writer.h"

#include "pb2/tts.pb.h"

namespace tts::entry {

void GrpcResponseWriter::to_proto(const framework::WriteToClient& w,
                                  SynthesizeResponse* out) {
    if (!out) return;
    out->set_audio(w.audio.data(), w.audio.size());
    auto* meta = out->mutable_meta();
    meta->set_sample_rate(w.sample_rate);
    meta->set_channels(w.channels);
    meta->set_audio_length_ms(w.audio_length_ms);
    meta->set_audio_size_bytes(static_cast<int64_t>(w.audio.size()));
    meta->set_usage_characters(static_cast<int64_t>(w.audio_length_ms));
    switch (w.format) {
        case framework::AudioFormat::Mp3:  meta->set_format(::tts::MP3);  break;
        case framework::AudioFormat::Pcm:  meta->set_format(::tts::PCM);  break;
        case framework::AudioFormat::Flac: meta->set_format(::tts::FLAC); break;
        case framework::AudioFormat::Wav:  meta->set_format(::tts::WAV);  break;
        default:                           meta->set_format(::tts::AUDIO_FORMAT_UNSPECIFIED); break;
    }
    out->set_is_final(w.is_final);
    out->set_cumulative_bytes(w.cumulative_bytes);
    out->set_status_code(w.status_code);
    out->set_status_msg(w.status_msg);
    out->set_trace_id(w.trace_id);
}

}  // namespace tts::entry