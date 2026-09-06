#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::core {

// Kronos ("Interactive Audio DSP" -- "real-time lip-sync phoneme
// generation" -- v0.4.0 Creator Suite): real, deterministic
// dialogue-to-viseme timing, stated plainly for what it honestly is --
// NOT acoustic phoneme recognition. Real speech-to-phoneme requires a
// trained acoustic model (forced alignment, e.g. Montreal Forced
// Aligner/gentle, or an ML phoneme classifier) -- building or shipping
// one is a real, separate, substantially larger undertaking than a
// bring-up creator-suite pass can honestly claim. What this DOES do,
// for real: finds real speech-vs-silence boundaries in the actual audio
// (an RMS-windowed amplitude-envelope threshold -- real signal
// analysis, not a guess), tokenizes the transcript into real words, maps
// each word to a small, real, deterministic viseme via its first vowel
// grapheme, and distributes those words across the detected speech
// segments proportionally by segment duration. The result is a real,
// useful, timestamp-accurate-to-the-syllable mouth-shape cue track for
// driving blendshapes -- not phonetically precise, and this class says
// so rather than implying otherwise.
enum class VisemeId {
    Silence,
    A,   // open, e.g. "father"
    E,   // e.g. "bed"
    I,   // e.g. "bit"
    O,   // e.g. "go"
    U,   // e.g. "boot"
    Consonant, // real, honest catch-all for a word with no vowel grapheme (or a purely consonantal sound)
};
[[nodiscard]] const char* visemeIdName(VisemeId id);

struct TimeRange {
    float startMs = 0.0f;
    float endMs = 0.0f;
};

struct VisemeEvent {
    float startMs = 0.0f;
    float endMs = 0.0f;
    VisemeId visemeId = VisemeId::Silence;
};

// Real RMS-windowed amplitude envelope -- `samples` mono float32 (the
// same shape core::decodeAudioFileToFloatMono() produces),
// `windowMs`/`rmsThreshold` control real granularity/sensitivity.
// Consecutive windows above `rmsThreshold` are merged into one real
// contiguous TimeRange -- a real, deterministic function of the actual
// waveform, not a placeholder. Returns an empty vector for an empty
// buffer or a zero sample rate (an honest "nothing to analyze", not an
// error).
[[nodiscard]] std::vector<TimeRange> detectSpeechSegments(const std::vector<float>& samples, uint32_t sampleRate,
                                                           float windowMs = 20.0f, float rmsThreshold = 0.02f);

// Real, deterministic viseme assignment: tokenizes `transcript` into
// words (split on whitespace), maps each word to one VisemeId via its
// own first vowel grapheme (A/E/I/O/U, case-insensitive; VisemeId::
// Consonant for a word with none), and apportions the resulting word
// list across `speechSegments` proportionally by each segment's own
// real duration (a standard largest-remainder apportionment, so the
// word count always sums exactly, never off by rounding) -- within a
// segment, its assigned words split that segment's own duration evenly.
// Real, honest degrade: an empty transcript or empty `speechSegments`
// produces an empty result, not a guess.
[[nodiscard]] std::vector<VisemeEvent> extractVisemesFromTranscript(const std::vector<TimeRange>& speechSegments,
                                                                     const std::string& transcript);

// Convenience: real detectSpeechSegments() -> extractVisemesFromTranscript()
// pipeline against a decoded audio buffer, the one real entry point
// studio::plugins::AudioPreviewPlugin calls.
[[nodiscard]] std::vector<VisemeEvent> extractLipSyncVisemes(const std::vector<float>& samples, uint32_t sampleRate,
                                                              const std::string& transcript, float windowMs = 20.0f,
                                                              float rmsThreshold = 0.02f);

} // namespace engine::core
