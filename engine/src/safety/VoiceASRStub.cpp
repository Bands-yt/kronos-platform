#include "safety/VoiceASRStub.hpp"

namespace engine::safety {

void VoiceASRStub::pushAudio(const AudioChunk& /*chunk*/) {
    // Intentional no-op -- see class comment in VoiceASRStub.hpp. Retaining
    // audio ahead of a real ASR model existing would be pure downside.
}

TranscriptionResult VoiceASRStub::transcribeRollingBuffer() const {
    return TranscriptionResult{/*succeeded=*/false, /*text=*/"", /*confidence=*/0.0f};
}

void VoiceASRStub::clearBuffer() {
    // No-op: matches pushAudio() -- nothing is buffered yet.
}

} // namespace engine::safety
