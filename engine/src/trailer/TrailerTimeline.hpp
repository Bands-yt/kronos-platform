#pragma once

#include <string>
#include <vector>

namespace engine::trailer {

// One real, named, durationed entry in a real trailer's scene sequence --
// deliberately just name+duration here (no camera/gameplay data), so
// this stays a pure, generic, reusable timing primitive; richer per-beat
// data (camera origin, which class/map/ultimate a beat triggers) lives
// in TrailerScenes.hpp's own TrailerBeat, which this timeline is built
// FROM (see beatsToTimelineScenes()) but doesn't need to know about.
struct TimelineScene {
    std::string name;
    float durationSeconds = 0.0f;
};

// Sprint 15 ("TNT-Wars Trailer Production")'s real, pure scene-sequencing
// logic: given an ordered list of named, durationed scenes, answers
// "which scene is active, and how far into it, at real elapsed trailer
// time T" and "how many real output frames does the whole trailer need
// at a given real output frame rate." No GPU/window/wall-clock/
// randomness dependency at all -- playback is exactly reproducible from
// the same real inputs every run, the sequencing/timing half of the
// brief's own "deterministic playback" requirement (see
// TrailerDirector.hpp's own comment for the GPU-touching half).
class TrailerTimeline {
public:
    explicit TrailerTimeline(std::vector<TimelineScene> scenes);

    [[nodiscard]] const std::vector<TimelineScene>& scenes() const { return scenes_; }
    [[nodiscard]] float totalDurationSeconds() const;

    struct SceneQuery {
        int sceneIndex = -1;              // -1 = at or past the end of the trailer (a real, honest "finished" signal)
        float localTimeSeconds = 0.0f;    // real elapsed time since this scene started
        float sceneDurationSeconds = 0.0f;
        float sceneProgress01 = 0.0f;     // localTimeSeconds / sceneDurationSeconds, clamped [0,1]
    };
    // Real, pure query: which scene (by index into scenes()) is active at
    // real elapsed trailer time `timeSeconds`, and how far into it.
    // Negative input clamps to 0; input at or past totalDurationSeconds()
    // real-returns sceneIndex=-1 rather than clamping to the last scene,
    // so a caller can tell "still playing the last scene" apart from
    // "trailer is over."
    [[nodiscard]] SceneQuery queryAt(float timeSeconds) const;

    // Real, pure frame-count computation for a given real output frame
    // rate -- ceiling-rounded so the trailer's very last partial frame
    // interval still gets one real frame, never silently dropped.
    [[nodiscard]] int totalFrameCountAtFps(float fps) const;

    // Real, pure index lookup by scene name -- -1 if no scene has that
    // exact name. Real, small convenience for tests and any caller (the
    // Studio Trailer Panel's scrubber) that wants to jump to a named
    // beat rather than an elapsed-time offset.
    [[nodiscard]] int indexOfScene(const std::string& name) const;

private:
    std::vector<TimelineScene> scenes_;
};

} // namespace engine::trailer
