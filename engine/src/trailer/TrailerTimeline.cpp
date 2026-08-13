#include "trailer/TrailerTimeline.hpp"

#include <algorithm>
#include <cmath>

namespace engine::trailer {

TrailerTimeline::TrailerTimeline(std::vector<TimelineScene> scenes) : scenes_(std::move(scenes)) {}

float TrailerTimeline::totalDurationSeconds() const {
    float total = 0.0f;
    for (const TimelineScene& scene : scenes_) total += scene.durationSeconds;
    return total;
}

TrailerTimeline::SceneQuery TrailerTimeline::queryAt(float timeSeconds) const {
    SceneQuery result;
    float t = std::max(0.0f, timeSeconds);
    float elapsed = 0.0f;
    for (size_t i = 0; i < scenes_.size(); ++i) {
        float duration = scenes_[i].durationSeconds;
        if (t < elapsed + duration) {
            result.sceneIndex = static_cast<int>(i);
            result.localTimeSeconds = t - elapsed;
            result.sceneDurationSeconds = duration;
            result.sceneProgress01 = duration > 0.0f ? std::clamp(result.localTimeSeconds / duration, 0.0f, 1.0f) : 1.0f;
            return result;
        }
        elapsed += duration;
    }
    // t >= totalDurationSeconds() -- real, honest "trailer finished", not
    // a clamp onto the last scene (see this method's own header comment).
    result.sceneIndex = -1;
    result.localTimeSeconds = 0.0f;
    result.sceneDurationSeconds = 0.0f;
    result.sceneProgress01 = 1.0f;
    return result;
}

int TrailerTimeline::totalFrameCountAtFps(float fps) const {
    if (fps <= 0.0f) return 0;
    return static_cast<int>(std::ceil(totalDurationSeconds() * fps));
}

int TrailerTimeline::indexOfScene(const std::string& name) const {
    for (size_t i = 0; i < scenes_.size(); ++i) {
        if (scenes_[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

} // namespace engine::trailer
