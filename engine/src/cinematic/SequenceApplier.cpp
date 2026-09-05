#include "cinematic/SequenceApplier.hpp"

#include <glm/gtc/quaternion.hpp>

#include "core/Components.hpp"

namespace engine::cinematic {

namespace {
// Real, honest "does this track have a real, non-empty channel by this
// name" check -- an absent/empty channel leaves `outValue` untouched and
// returns false, which is exactly what lets a caller skip writing that
// one component field instead of overwriting it with a stale default.
bool sampleTrackChannel(const SequencerTrack& track, const char* channelName, float timeSeconds, float& outValue) {
    const TrackChannel* channel = track.findChannel(channelName);
    if (channel == nullptr || channel->keys.empty()) return false;
    outValue = sampleCurve(channel->keys, timeSeconds);
    return true;
}
} // namespace

void applySequenceToScene(const Sequence& sequence, float timeSeconds, core::ECS& ecs) {
    for (const SequencerTrack& track : sequence.tracks()) {
        if (track.muted || track.targetId == 0) continue;
        auto entity = static_cast<core::EntityId>(static_cast<uint32_t>(track.targetId));
        if (!ecs.raw().valid(entity)) continue;

        if (track.kind == TrackKind::Transform) {
            auto* transform = ecs.tryGetComponent<core::Transform>(entity);
            if (transform == nullptr) continue;
            float v;
            if (sampleTrackChannel(track, "position.x", timeSeconds, v)) transform->position.x = v;
            if (sampleTrackChannel(track, "position.y", timeSeconds, v)) transform->position.y = v;
            if (sampleTrackChannel(track, "position.z", timeSeconds, v)) transform->position.z = v;

            glm::vec3 euler = glm::degrees(glm::eulerAngles(transform->rotation));
            bool rotated = false;
            if (sampleTrackChannel(track, "rotation.x", timeSeconds, v)) {
                euler.x = v;
                rotated = true;
            }
            if (sampleTrackChannel(track, "rotation.y", timeSeconds, v)) {
                euler.y = v;
                rotated = true;
            }
            if (sampleTrackChannel(track, "rotation.z", timeSeconds, v)) {
                euler.z = v;
                rotated = true;
            }
            if (rotated) transform->rotation = glm::quat(glm::radians(euler));
        } else if (track.kind == TrackKind::LightIntensity) {
            auto* light = ecs.tryGetComponent<core::Light>(entity);
            if (light == nullptr) continue;
            float v;
            if (sampleTrackChannel(track, "intensity", timeSeconds, v)) light->intensity = v;
        }
    }
}

} // namespace engine::cinematic
