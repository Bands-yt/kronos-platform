#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Audio.hpp"

namespace engine::brokenbones {

enum class Sfx {
    Crack,
    SkullCrack,
    Thud,
    Splash,
    Bomb,
    RocketIgnite,
    RocketBurn,
    RocketBlast,
    FloatInflate,
    FloatPop,
    Boing,
    Wind,
    ComboChime,
    Coin,
    Denied,
    Click,
    Contract,
    Fanfare,
    Squelch,
    Heartbeat,
    Ringing,
};
inline constexpr size_t kSfxCount = 21;
inline constexpr uint32_t kSfxSampleRate = 44100;

[[nodiscard]] const char* sfxName(Sfx sfx);
[[nodiscard]] bool sfxLoops(Sfx sfx);
// Mono samples in [-1, 1]; `variant` gives each copy a slightly different take.
[[nodiscard]] std::vector<float> synthesizeSfx(Sfx sfx, uint32_t variant);

// Synthesizes every effect to WAV once, loads a few voices of each so rapid
// repeats overlap instead of cutting each other off, and plays them unspatialized.
class SoundBank {
public:
    bool load(core::Audio& audio, const std::string& directory);
    void play(Sfx sfx, float volume = 1.0f, float pitch = 1.0f);
    // Starts the loop if needed, then updates its volume and pitch.
    void setLoop(Sfx sfx, float volume, float pitch = 1.0f);
    void stopLoop(Sfx sfx);
    void stopAllLoops();
    [[nodiscard]] bool loaded() const { return audio_ != nullptr; }

private:
    core::Audio* audio_ = nullptr;
    std::array<std::vector<core::SoundHandle>, kSfxCount> voices_;
    std::array<size_t, kSfxCount> nextVoice_{};
    std::array<bool, kSfxCount> loopPlaying_{};
};

} // namespace engine::brokenbones
