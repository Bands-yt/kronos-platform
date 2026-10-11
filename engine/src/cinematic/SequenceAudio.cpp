#include "cinematic/SequenceAudio.hpp"

#include <cmath>
#include <filesystem>

namespace engine::cinematic {

namespace {
constexpr double kJumpTolerance = 0.25;  // larger playhead moves count as a scrub
constexpr double kDriftTolerance = 0.05; // audio clock vs. playhead
constexpr int kMaxLoopSegments = 64;
} // namespace

void SequenceAudioPlayer::anchor(const Sequence& sequence, core::Audio& audio) {
    audio.cancelCues(audio.engineTimeFrames(), true);
    anchorFrame_ = audio.engineTimeFrames();
    anchorPlayhead_ = sequence.playheadSeconds();
    lastPlayhead_ = sequence.playheadSeconds();
    played_ = 0.0;
    scheduledUntil_ = 0.0;
    running_ = true;
}

void SequenceAudioPlayer::stop(core::Audio& audio) {
    if (running_) audio.cancelCues(audio.engineTimeFrames(), true);
    running_ = false;
}

void SequenceAudioPlayer::update(const Sequence& sequence, core::Audio& audio, const Resolver& resolve) {
    if (!audio.isInitialized() || audio.sampleRate() == 0) return;
    if (!sequence.isPlaying()) {
        stop(audio);
        return;
    }
    if (!running_) anchor(sequence, audio);

    double delta = static_cast<double>(sequence.playheadSeconds()) - lastPlayhead_;
    if (delta < 0.0 && sequence.loopEnabled()) delta += sequence.loopEnd() - sequence.loopStart();
    const double rate = audio.sampleRate();
    const double clock = static_cast<double>(audio.engineTimeFrames() - anchorFrame_) / rate;
    if (delta < 0.0 || delta > kJumpTolerance || std::abs(clock - (played_ + delta)) > kDriftTolerance) {
        anchor(sequence, audio);
        delta = 0.0;
    }
    played_ += delta;
    lastPlayhead_ = sequence.playheadSeconds();

    const double until = played_ + lookahead_;
    if (until > scheduledUntil_) {
        scheduleWindow(sequence, audio, resolve, scheduledUntil_, until);
        scheduledUntil_ = until;
    }
}

// Schedules events whose unrolled time u (seconds after the anchor) is in (from, to].
void SequenceAudioPlayer::scheduleWindow(const Sequence& sequence, core::Audio& audio, const Resolver& resolve,
                                         double from, double to) {
    const double start = anchorPlayhead_;
    const bool looping = sequence.loopEnabled() && start < sequence.loopEnd();
    const double loopStart = sequence.loopStart();
    const double loopEnd = sequence.loopEnd();
    const double rate = audio.sampleRate();

    auto emit = [&](const TrackEvent& event, double u) {
        if (u <= from || u > to) return;
        const core::SoundHandle handle = resolve(event.payload);
        if (handle == core::kInvalidSoundHandle) return;
        const uint64_t frame = anchorFrame_ + static_cast<uint64_t>(std::llround(u * rate));
        if (audio.scheduleCue(handle, frame, bus_) != 0) ++scheduled_;
    };

    for (const SequencerTrack& track : sequence.tracks()) {
        if (track.muted || track.kind != TrackKind::Audio) continue;
        for (const TrackEvent& event : track.events) {
            const double t = event.timeSeconds;
            if (!looping) {
                if (t > start) emit(event, t - start);
                continue;
            }
            // Segment 0 runs from the anchor to the loop end, then the loop repeats.
            if (t > start && t <= loopEnd) emit(event, t - start);
            if (t < loopStart || t > loopEnd) continue;
            const double span = loopEnd - loopStart;
            for (int k = 0; k < kMaxLoopSegments; ++k) {
                const double u = (loopEnd - start) + k * span + (t - loopStart);
                if (u > to) break;
                emit(event, u);
            }
        }
    }
}

core::SoundHandle SoundPayloadCache::resolve(core::Audio& audio, const std::string& payload) {
    if (payload.empty()) return core::kInvalidSoundHandle;
    if (auto it = cache_.find(payload); it != cache_.end()) return it->second;

    core::SoundHandle handle = core::kInvalidSoundHandle;
    std::vector<std::filesystem::path> bases{payload};
    for (const std::string& root : roots_) bases.push_back(std::filesystem::path(root) / payload);
    for (const auto& base : bases) {
        for (const char* ext : {"", ".wav", ".ogg", ".mp3", ".flac"}) {
            std::filesystem::path candidate = base;
            candidate += ext;
            std::error_code ec;
            if (!std::filesystem::is_regular_file(candidate, ec)) continue;
            handle = audio.loadSound(candidate.string());
            if (handle != core::kInvalidSoundHandle) break;
        }
        if (handle != core::kInvalidSoundHandle) break;
    }
    cache_[payload] = handle;
    return handle;
}

void SoundPayloadCache::forgetMissing() {
    std::erase_if(cache_, [](const auto& entry) { return entry.second == core::kInvalidSoundHandle; });
}

void SoundPayloadCache::clear(core::Audio& audio) {
    for (const auto& [payload, handle] : cache_) {
        if (handle != core::kInvalidSoundHandle) audio.unloadSound(handle);
    }
    cache_.clear();
}

size_t SoundPayloadCache::loadedCount() const {
    size_t count = 0;
    for (const auto& [payload, handle] : cache_) count += handle != core::kInvalidSoundHandle;
    return count;
}

} // namespace engine::cinematic
