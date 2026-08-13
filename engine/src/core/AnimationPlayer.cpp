#include "core/AnimationPlayer.hpp"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::core {

const char* animationInterpolationName(AnimationInterpolation mode) {
    switch (mode) {
        case AnimationInterpolation::Linear: return "Linear";
        case AnimationInterpolation::Cubic: return "Cubic";
    }
    return "Linear";
}

AnimationPlayer::AnimationPlayer(Skeleton skeleton) : skeleton_(std::move(skeleton)) {
    inverseBindMatrices_ = skeleton_.inverseBindMatrices();
    skinningMatrices_.assign(skeleton_.joints.size(), glm::mat4(1.0f));
}

AnimationPlayer::ActiveClip* AnimationPlayer::findActive(Handle handle) {
    if (handle == kInvalidHandle) return nullptr;
    for (auto& layer : layers_) {
        for (auto& active : layer) {
            if (active.handle == handle && active.alive) return &active;
        }
    }
    return nullptr;
}

const AnimationPlayer::ActiveClip* AnimationPlayer::findActive(Handle handle) const {
    return const_cast<AnimationPlayer*>(this)->findActive(handle);
}

void AnimationPlayer::startFade(ActiveClip& active, float targetWeight, float fadeSeconds) {
    active.fadeStartWeight = active.weight;
    active.fadeTargetWeight = targetWeight;
    active.fadeDuration = std::max(fadeSeconds, 0.0f);
    active.fadeElapsed = 0.0f;
}

AnimationPlayer::Handle AnimationPlayer::play(AnimationClip clip, AnimationLayer layer, bool looping,
                                               float fadeSeconds) {
    auto& clips = layers_[static_cast<size_t>(layer)];

    if (fadeSeconds <= 0.0f) {
        clips.clear(); // instant cut -- nothing to crossfade from
    } else {
        for (auto& existing : clips) {
            if (existing.alive) startFade(existing, 0.0f, fadeSeconds);
        }
    }

    ActiveClip active;
    active.handle = nextHandle_++;
    active.clip = std::move(clip);
    active.looping = looping;
    active.weight = fadeSeconds > 0.0f ? 0.0f : 1.0f;
    active.fadeStartWeight = active.weight;
    active.fadeTargetWeight = 1.0f;
    active.fadeDuration = std::max(fadeSeconds, 0.0f);
    active.fadeElapsed = 0.0f;

    Handle handle = active.handle;
    clips.push_back(std::move(active));
    return handle;
}

void AnimationPlayer::stop(Handle handle, float fadeSeconds) {
    ActiveClip* active = findActive(handle);
    if (active == nullptr) return;
    if (fadeSeconds <= 0.0f) {
        active->alive = false;
    } else {
        startFade(*active, 0.0f, fadeSeconds);
    }
}

void AnimationPlayer::pause(Handle handle) {
    if (ActiveClip* active = findActive(handle)) active->paused = true;
}

void AnimationPlayer::resume(Handle handle) {
    if (ActiveClip* active = findActive(handle)) active->paused = false;
}

bool AnimationPlayer::isPlaying(Handle handle) const {
    const ActiveClip* active = findActive(handle);
    return active != nullptr && !active->paused;
}

void AnimationPlayer::seek(Handle handle, float time) {
    if (ActiveClip* active = findActive(handle)) {
        float duration = active->clip.duration;
        active->playheadTime = duration > 0.0f ? std::clamp(time, 0.0f, duration) : 0.0f;
    }
}

float AnimationPlayer::playhead(Handle handle) const {
    const ActiveClip* active = findActive(handle);
    return active != nullptr ? active->playheadTime : 0.0f;
}

void AnimationPlayer::tickLayer(std::vector<ActiveClip>& clips, float dt) {
    for (auto& active : clips) {
        if (!active.alive) continue;

        float previousTime = active.playheadTime;
        if (!active.paused) {
            float duration = active.clip.duration;
            float newTime = active.playheadTime + dt;
            bool wrapped = false;
            if (active.looping && duration > 0.0f) {
                while (newTime >= duration) {
                    newTime -= duration;
                    wrapped = true;
                }
            } else if (!active.looping && duration > 0.0f) {
                // Holds the last frame past duration rather than
                // auto-removing -- an AnimationPlayer clip participates in
                // blend-weight math, so silently dropping a finished clip
                // could pop the pose mid-blend; the caller decides when to
                // stop()/fade it out instead (see play()'s header comment
                // and this class's header comment on this design choice).
                newTime = std::min(newTime, duration);
            }
            active.playheadTime = newTime;

            // Event crossing -- fires only for an audibly/visibly blended
            // clip (weight above a small epsilon), so a fully-faded-out
            // crossfade partner never fires a stray event on its way out.
            if (active.weight > 1e-3f) {
                for (const auto& event : active.clip.events) {
                    bool crossed = wrapped ? (event.time > previousTime && event.time <= duration) ||
                                                  (event.time >= 0.0f && event.time <= active.playheadTime)
                                            : (event.time > previousTime && event.time <= active.playheadTime);
                    if (crossed) firedEvents_.push_back(event.name);
                }
            }
        }

        if (active.fadeDuration > 0.0f && active.fadeElapsed < active.fadeDuration) {
            active.fadeElapsed = std::min(active.fadeElapsed + dt, active.fadeDuration);
            float t = active.fadeElapsed / active.fadeDuration;
            active.weight = glm::mix(active.fadeStartWeight, active.fadeTargetWeight, t);
        } else {
            active.weight = active.fadeTargetWeight;
        }

        if (active.fadeTargetWeight <= 0.0f && active.fadeDuration > 0.0f && active.fadeElapsed >= active.fadeDuration) {
            active.alive = false;
        }
    }

    clips.erase(std::remove_if(clips.begin(), clips.end(), [](const ActiveClip& c) { return !c.alive; }), clips.end());
}

bool AnimationPlayer::evaluateLayerPoseForJoint(const std::vector<ActiveClip>& clips, const std::string& jointName,
                                                 AnimatedPose& outPose, float& outWeightSum) const {
    glm::vec3 posSum(0.0f);
    glm::vec4 rotSum(0.0f);
    glm::vec3 scaleSum(0.0f);
    float weightSum = 0.0f;
    glm::quat reference(1.0f, 0.0f, 0.0f, 0.0f);
    bool haveReference = false;
    bool touched = false;

    for (const auto& active : clips) {
        if (active.weight <= 1e-4f) continue;
        for (const auto& track : active.clip.tracks) {
            if (track.targetName() != jointName) continue;

            AnimatedPose pose = interpolation_ == AnimationInterpolation::Cubic ? track.evaluateCubic(active.playheadTime)
                                                                                 : track.evaluate(active.playheadTime);
            if (!haveReference) {
                reference = pose.rotation;
                haveReference = true;
            }
            // Flip to the reference's hemisphere first -- a weighted sum
            // of quaternions from opposite hemispheres cancels instead of
            // blending (the same "shortest path" fix glm::slerp applies
            // internally, done by hand here since this is a component-wise
            // weighted average, not a single slerp).
            glm::quat r = glm::dot(pose.rotation, reference) < 0.0f ? -pose.rotation : pose.rotation;

            posSum += pose.position * active.weight;
            rotSum += glm::vec4(r.x, r.y, r.z, r.w) * active.weight;
            scaleSum += pose.scale * active.weight;
            weightSum += active.weight;
            touched = true;
            break; // one track per clip targets a given joint name
        }
    }

    outWeightSum = weightSum;
    if (!touched || weightSum <= 1e-4f) return false;

    outPose.position = posSum / weightSum;
    outPose.scale = scaleSum / weightSum;
    glm::vec4 normalizedRot = glm::length(rotSum) > 1e-8f ? glm::normalize(rotSum) : glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    outPose.rotation = glm::quat(normalizedRot.w, normalizedRot.x, normalizedRot.y, normalizedRot.z);
    return true;
}

void AnimationPlayer::tick(float dt) {
    firedEvents_.clear();

    for (auto& layer : layers_) tickLayer(layer, dt);

    const auto& baseClips = layers_[static_cast<size_t>(AnimationLayer::Base)];
    const auto& upperClips = layers_[static_cast<size_t>(AnimationLayer::UpperBody)];

    std::vector<glm::mat4> world(skeleton_.joints.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
        const Joint& joint = skeleton_.joints[i];

        AnimatedPose basePose;
        float baseWeight = 0.0f;
        bool baseTouched = evaluateLayerPoseForJoint(baseClips, joint.name, basePose, baseWeight);

        AnimatedPose upperPose;
        float upperWeight = 0.0f;
        bool upperTouched = evaluateLayerPoseForJoint(upperClips, joint.name, upperPose, upperWeight);

        // Pose composition: a joint no clip touches holds its own bind
        // pose (so an un-animated skeleton doesn't collapse to the
        // origin/identity). A joint only Base or only UpperBody touches
        // uses that layer's pose outright. A joint both layers touch has
        // UpperBody override Base, blended by UpperBody's own aggregate
        // weight (clamped to [0,1]) -- so an UpperBody clip crossfading in
        // smoothly takes over from Base rather than popping.
        AnimatedPose finalPose;
        if (!baseTouched && !upperTouched) {
            finalPose.position = joint.localPosition;
            finalPose.rotation = joint.localRotation;
            finalPose.scale = joint.localScale;
        } else if (baseTouched && !upperTouched) {
            finalPose = basePose;
        } else if (!baseTouched && upperTouched) {
            finalPose = upperPose;
        } else {
            float f = std::clamp(upperWeight, 0.0f, 1.0f);
            finalPose.position = glm::mix(basePose.position, upperPose.position, f);
            finalPose.rotation = glm::slerp(basePose.rotation, upperPose.rotation, f);
            finalPose.scale = glm::mix(basePose.scale, upperPose.scale, f);
        }

        glm::mat4 local = glm::translate(glm::mat4(1.0f), finalPose.position) * glm::mat4_cast(finalPose.rotation) *
                           glm::scale(glm::mat4(1.0f), finalPose.scale);
        world[i] = joint.parentIndex >= 0 ? world[static_cast<size_t>(joint.parentIndex)] * local : local;
    }

    skinningMatrices_.resize(world.size());
    for (size_t i = 0; i < world.size(); ++i) skinningMatrices_[i] = world[i] * inverseBindMatrices_[i];
}

std::vector<std::string> AnimationPlayer::consumeFiredEvents() {
    std::vector<std::string> result = std::move(firedEvents_);
    firedEvents_.clear();
    return result;
}

} // namespace engine::core
