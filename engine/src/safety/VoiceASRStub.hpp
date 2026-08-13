#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>

namespace engine::safety {

struct AudioChunk {
    const float* samples = nullptr;
    size_t frameCount = 0;
    uint32_t sampleRate = 48000;
};

struct TranscriptionResult {
    bool succeeded = false;
    std::string text;
    float confidence = 0.0f;
};

// Structural stand-in for docs/ARCHITECTURE.md §10's voice moderation:
// "a small edge-deployed ASR model feeds the same text classifier used
// for chat. Voice is ephemeral by default: only a short rolling buffer
// (30-60s) is retained, and only when a risk threshold is crossed."
//
// transcribeRollingBuffer() is not implemented -- real-time speech-to-text
// is a genuine ML model (a quantized Whisper-family model per
// docs/ARCHITECTURE.md §3), not something worth faking here. Consistent
// with the ephemeral-by-default policy, pushAudio() also does not
// actually retain anything yet: buffering audio this class does nothing
// useful with would be a pure privacy liability for zero benefit, which
// is exactly the failure mode §10 warns against ("always-on recording is
// itself a serious privacy liability"). A real implementation's buffer
// and its retention-window eviction logic land here together, not staged
// separately.
class VoiceASRStub {
public:
    explicit VoiceASRStub(std::chrono::seconds retentionWindow = std::chrono::seconds(60))
        : retentionWindow_(retentionWindow) {}

    void pushAudio(const AudioChunk& chunk);
    [[nodiscard]] TranscriptionResult transcribeRollingBuffer() const;
    void clearBuffer();

    [[nodiscard]] std::chrono::seconds retentionWindow() const { return retentionWindow_; }

private:
    std::chrono::seconds retentionWindow_;
};

} // namespace engine::safety
