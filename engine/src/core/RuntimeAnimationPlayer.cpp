#include "core/RuntimeAnimationPlayer.hpp"

#include <cmath>
#include <cstdio>

#include "core/Components.hpp"

namespace engine::core {

namespace {
EntityId findEntityByName(ECS& ecs, const std::string& targetName) {
    for (auto entity : ecs.view<Name>()) {
        const auto* nameComp = ecs.tryGetComponent<Name>(entity);
        if (nameComp != nullptr && nameComp->value == targetName) return entity;
    }
    return kNullEntity;
}
} // namespace

RuntimeAnimationPlayer::Handle RuntimeAnimationPlayer::play(const std::string& clipPath, bool looping) {
    AnimationClip clip;
    if (!clip.loadFromFile(clipPath)) {
        std::fprintf(stderr, "RuntimeAnimationPlayer: failed to load clip \"%s\"\n", clipPath.c_str());
        return kInvalidHandle;
    }

    ActiveClip entry{std::move(clip), 0.0f, looping, /*alive=*/true};
    for (size_t i = 0; i < active_.size(); ++i) {
        if (!active_[i].alive) {
            active_[i] = std::move(entry);
            return static_cast<Handle>(i);
        }
    }
    active_.push_back(std::move(entry));
    return static_cast<Handle>(active_.size() - 1);
}

void RuntimeAnimationPlayer::stop(Handle handle) {
    if (handle >= active_.size()) return;
    active_[handle].alive = false;
}

void RuntimeAnimationPlayer::tick(float dt, ECS& ecs) {
    for (auto& entry : active_) {
        if (!entry.alive) continue;

        entry.playhead += dt;
        if (entry.playhead > entry.clip.duration) {
            if (entry.looping && entry.clip.duration > 0.0f) {
                entry.playhead = std::fmod(entry.playhead, entry.clip.duration);
            } else {
                entry.playhead = entry.clip.duration;
                entry.alive = false; // finished -- this tick still applies its final pose below
            }
        }

        for (const auto& track : entry.clip.tracks) {
            if (track.keyframes().empty()) continue;
            EntityId target = findEntityByName(ecs, track.targetName());
            if (target == kNullEntity) continue;
            auto* transform = ecs.tryGetComponent<Transform>(target);
            if (transform == nullptr) continue;

            AnimatedPose pose = track.evaluate(entry.playhead);
            transform->position = pose.position;
            transform->rotation = pose.rotation;
            transform->scale = pose.scale;
        }
    }
}

} // namespace engine::core
