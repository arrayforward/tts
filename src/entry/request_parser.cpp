#include "request_parser.h"

#include "pb2/tts.pb.h"

namespace tts::entry {

namespace {

framework::Model to_model(::tts::Model m) {
    switch (m) {
        case ::tts::SPEECH_2_8_HD:    return framework::Model::Speech28Hd;
        case ::tts::SPEECH_2_8_TURBO: return framework::Model::Speech28Turbo;
        case ::tts::SPEECH_2_6_HD:    return framework::Model::Speech26Hd;
        case ::tts::SPEECH_2_6_TURBO: return framework::Model::Speech26Turbo;
        case ::tts::SPEECH_02_HD:     return framework::Model::Speech02Hd;
        case ::tts::SPEECH_02_TURBO:  return framework::Model::Speech02Turbo;
        case ::tts::SPEECH_01_HD:     return framework::Model::Speech01Hd;
        case ::tts::SPEECH_01_TURBO:  return framework::Model::Speech01Turbo;
        default:                      return framework::Model::Unspecified;
    }
}

framework::AudioFormat to_format(::tts::AudioFormat f) {
    switch (f) {
        case ::tts::MP3:  return framework::AudioFormat::Mp3;
        case ::tts::PCM:  return framework::AudioFormat::Pcm;
        case ::tts::FLAC: return framework::AudioFormat::Flac;
        case ::tts::WAV:  return framework::AudioFormat::Wav;
        default:          return framework::AudioFormat::Unspecified;
    }
}

void apply_voice(const ::tts::VoiceSetting& src, framework::VoiceSettings& dst) {
    if (!src.voice_id().empty()) dst.voice_id = src.voice_id();
    if (src.speed() != 0.0f)     dst.speed = src.speed();
    if (src.vol() != 0.0f)       dst.vol = src.vol();
    dst.pitch = src.pitch();
    if (src.emotion() != ::tts::EMOTION_UNSPECIFIED) {
        dst.emotion = [&]() -> std::string {
            switch (src.emotion()) {
                case ::tts::HAPPY:      return "happy";
                case ::tts::SAD:        return "sad";
                case ::tts::ANGRY:      return "angry";
                case ::tts::FEARFUL:    return "fearful";
                case ::tts::DISGUSTED:  return "disgusted";
                case ::tts::SURPRISED:  return "surprised";
                case ::tts::CALM:       return "calm";
                case ::tts::FLUENT:     return "fluent";
                case ::tts::WHISPER:    return "whisper";
                default:                return {};
            }
        }();
    }
    dst.english_normalization = src.english_normalization();
}

void apply_audio(const ::tts::AudioSetting& src, framework::AudioSettings& dst) {
    if (src.sample_rate() != 0) dst.sample_rate = src.sample_rate();
    if (src.bitrate() != 0)     dst.bitrate = src.bitrate();
    if (src.channel() != 0)     dst.channel = src.channel();
    if (src.format() != ::tts::AUDIO_FORMAT_UNSPECIFIED) {
        dst.format = to_format(src.format());
    }
}

void enforce_audio_invariants(framework::AudioSettings& a) {
    if (a.sample_rate != 8000 && a.sample_rate != 16000) {
        throw ParseError("sample_rate must be 8000 or 16000");
    }
    if (a.channel != 1) {
        throw ParseError("channel must be 1 (mono)");
    }
    if (a.format != framework::AudioFormat::Mp3 &&
        a.format != framework::AudioFormat::Pcm) {
        throw ParseError("format must be mp3 or pcm in MVP");
    }
}

}  // namespace

ParsedSynthesize RequestParser::parse_unary(const SynthesizeRequest& proto,
                                            const std::string& request_id) const {
    ParsedSynthesize out;
    auto& r = out.request;
    r.request_id = request_id;
    r.kind = framework::RequestKind::OpenStream;
    r.text = proto.text();
    r.model = to_model(proto.model());
    apply_voice(proto.voice(), r.voice);
    apply_audio(proto.audio(), r.audio);
    enforce_audio_invariants(r.audio);
    r.backend = to_backend(proto.backend());
    return out;
}

framework::Backend RequestParser::to_backend(::tts::Backend b) const {
    switch (b) {
        case ::tts::BACKEND_LOCAL:    return framework::Backend::Local;
        case ::tts::BACKEND_UPSTREAM: return framework::Backend::Upstream;
        case ::tts::BACKEND_AUTO:
        case ::tts::BACKEND_UNSPECIFIED:
        default:                      return framework::Backend::Auto;
    }
}

ParsedSynthesize RequestParser::parse_chunk(const TextChunk& proto,
                                            const std::string& request_id,
                                            const SynthesizeRequest& initial_settings,
                                            bool first_chunk) const {
    ParsedSynthesize out;
    auto& r = out.request;
    r.request_id = request_id;
    if (first_chunk) {
        r.kind = framework::RequestKind::OpenStream;
        r.model = to_model(initial_settings.model());
        apply_voice(initial_settings.voice(), r.voice);
        apply_audio(initial_settings.audio(), r.audio);
        enforce_audio_invariants(r.audio);
        r.backend = to_backend(initial_settings.backend());
    } else {
        r.kind = framework::RequestKind::StreamChunk;
    }
    switch (proto.body_case()) {
        case TextChunk::kText:
            r.text = proto.text();
            break;
        case TextChunk::kFinish:
            r.kind = framework::RequestKind::FinishStream;
            r.text.clear();
            break;
        default:
            break;
    }
    return out;
}

}  // namespace tts::entry