#include "core/Animation.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include "core/Skeleton.hpp"

namespace engine::core {

namespace {
constexpr float kKeyframeMergeEpsilon = 0.001f; // 1ms -- re-keying at "the same time" replaces, not duplicates

int easingToIndex(EasingMode mode) { return static_cast<int>(mode); }

// Standard Catmull-Rom basis -- see AnimationTrack::evaluateCubic's header
// comment for why this shape (passes exactly through p1 at t=0 and p2 at
// t=1, uses p0/p3 only to shape the curve's tangent through that segment).
glm::vec3 catmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                   (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
}

EasingMode easingFromIndex(int index) {
    switch (index) {
        case 0: return EasingMode::Linear;
        case 1: return EasingMode::EaseIn;
        case 2: return EasingMode::EaseOut;
        case 3: return EasingMode::EaseInOut;
        case 4: return EasingMode::Constant;
        default: return EasingMode::Linear; // unrecognized on load -- fail soft, not fail closed
    }
}
} // namespace

const char* easingModeName(EasingMode mode) {
    switch (mode) {
        case EasingMode::Linear: return "Linear";
        case EasingMode::EaseIn: return "Ease In";
        case EasingMode::EaseOut: return "Ease Out";
        case EasingMode::EaseInOut: return "Ease In Out";
        case EasingMode::Constant: return "Constant";
    }
    return "Linear";
}

float applyEasing(EasingMode mode, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    switch (mode) {
        case EasingMode::Linear: return t;
        case EasingMode::EaseIn: return t * t;
        case EasingMode::EaseOut: return t * (2.0f - t);
        case EasingMode::EaseInOut: return t < 0.5f ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
        case EasingMode::Constant: return 0.0f; // AnimationTrack::evaluate special-cases Constant before this matters
    }
    return t;
}

void AnimationTrack::addKeyframe(Keyframe keyframe) {
    for (auto& existing : keyframes_) {
        if (std::abs(existing.time - keyframe.time) < kKeyframeMergeEpsilon) {
            existing = keyframe;
            return;
        }
    }
    auto it = std::lower_bound(keyframes_.begin(), keyframes_.end(), keyframe.time,
                                [](const Keyframe& k, float t) { return k.time < t; });
    keyframes_.insert(it, keyframe);
}

void AnimationTrack::removeKeyframeAt(size_t index) {
    if (index < keyframes_.size()) {
        keyframes_.erase(keyframes_.begin() + static_cast<std::ptrdiff_t>(index));
    }
}

AnimatedPose AnimationTrack::evaluate(float time) const {
    AnimatedPose pose;
    if (keyframes_.empty()) return pose;

    if (keyframes_.size() == 1 || time <= keyframes_.front().time) {
        const Keyframe& k = keyframes_.front();
        pose.position = k.position;
        pose.rotation = k.rotation;
        pose.scale = k.scale;
        return pose;
    }
    if (time >= keyframes_.back().time) {
        const Keyframe& k = keyframes_.back();
        pose.position = k.position;
        pose.rotation = k.rotation;
        pose.scale = k.scale;
        return pose;
    }

    // Find the bracketing segment [a, b] with a.time <= time < b.time.
    // Guaranteed to terminate with 1 <= hi <= keyframes_.size()-1 given the
    // early-outs above (time is strictly between front and back here).
    size_t hi = 0;
    while (hi < keyframes_.size() && keyframes_[hi].time <= time) ++hi;
    const Keyframe& a = keyframes_[hi - 1];
    const Keyframe& b = keyframes_[hi];

    if (a.easing == EasingMode::Constant) {
        pose.position = a.position;
        pose.rotation = a.rotation;
        pose.scale = a.scale;
        return pose;
    }

    float span = b.time - a.time;
    float t = span > 0.0f ? (time - a.time) / span : 0.0f;
    t = applyEasing(a.easing, t);

    pose.position = glm::mix(a.position, b.position, t);
    pose.rotation = glm::slerp(a.rotation, b.rotation, t);
    pose.scale = glm::mix(a.scale, b.scale, t);
    return pose;
}

AnimatedPose AnimationTrack::evaluateCubic(float time) const {
    if (keyframes_.size() < 3) return evaluate(time); // no meaningful cubic shape with fewer than 3 keyframes

    AnimatedPose pose;
    if (time <= keyframes_.front().time) {
        const Keyframe& k = keyframes_.front();
        pose.position = k.position;
        pose.rotation = k.rotation;
        pose.scale = k.scale;
        return pose;
    }
    if (time >= keyframes_.back().time) {
        const Keyframe& k = keyframes_.back();
        pose.position = k.position;
        pose.rotation = k.rotation;
        pose.scale = k.scale;
        return pose;
    }

    size_t hi = 0;
    while (hi < keyframes_.size() && keyframes_[hi].time <= time) ++hi;
    size_t i1 = hi - 1;
    size_t i2 = hi;
    const Keyframe& k1 = keyframes_[i1];
    const Keyframe& k2 = keyframes_[i2];

    if (k1.easing == EasingMode::Constant) {
        pose.position = k1.position;
        pose.rotation = k1.rotation;
        pose.scale = k1.scale;
        return pose;
    }

    // Clamp/duplicate at the clip's own boundaries -- the standard
    // Catmull-Rom edge handling (no keyframe before the first/after the
    // last, so reuse the nearest one as its own extra control point).
    const Keyframe& k0 = keyframes_[i1 > 0 ? i1 - 1 : i1];
    const Keyframe& k3 = keyframes_[i2 + 1 < keyframes_.size() ? i2 + 1 : i2];

    float span = k2.time - k1.time;
    float t = span > 0.0f ? (time - k1.time) / span : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);

    pose.position = catmullRom(k0.position, k1.position, k2.position, k3.position, t);
    pose.rotation = glm::slerp(k1.rotation, k2.rotation, t);
    pose.scale = catmullRom(k0.scale, k1.scale, k2.scale, k3.scale, t);
    return pose;
}

AnimationTrack& AnimationClip::trackFor(const std::string& targetName) {
    for (auto& track : tracks) {
        if (track.targetName() == targetName) return track;
    }
    tracks.emplace_back(targetName);
    return tracks.back();
}

void AnimationClip::removeTrack(const std::string& targetName) {
    tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                 [&](const AnimationTrack& t) { return t.targetName() == targetName; }),
                 tracks.end());
}

bool AnimationClip::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "ANIMCLIP 1\n";
    out << "NAME " << name << "\n";
    out << "DURATION " << duration << "\n";
    out << "LOOPING " << (looping ? 1 : 0) << "\n";
    for (const auto& track : tracks) {
        out << "TRACK " << track.targetName() << "\n";
        for (const auto& k : track.keyframes()) {
            out << "KEY " << k.time << ' ' << k.position.x << ' ' << k.position.y << ' ' << k.position.z << ' '
                << k.rotation.x << ' ' << k.rotation.y << ' ' << k.rotation.z << ' ' << k.rotation.w << ' '
                << k.scale.x << ' ' << k.scale.y << ' ' << k.scale.z << ' ' << easingToIndex(k.easing) << "\n";
        }
    }
    for (const auto& event : events) {
        out << "EVENT " << event.time << ' ' << event.name << "\n";
    }
    out << "END\n";
    return out.good();
}

bool AnimationClip::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("ANIMCLIP", 0) != 0) return false;

    AnimationClip loaded;
    AnimationTrack* currentTrack = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("NAME ", 0) == 0) {
            loaded.name = line.substr(5);
        } else if (line.rfind("DURATION ", 0) == 0) {
            loaded.duration = std::stof(line.substr(9));
        } else if (line.rfind("LOOPING ", 0) == 0) {
            loaded.looping = line.substr(8) != "0";
        } else if (line.rfind("TRACK ", 0) == 0) {
            loaded.tracks.emplace_back(line.substr(6));
            currentTrack = &loaded.tracks.back(); // refreshed here, never held across a later emplace_back -- see note below
        } else if (line.rfind("KEY ", 0) == 0 && currentTrack != nullptr) {
            std::istringstream iss(line.substr(4));
            Keyframe k;
            int easingIndex = 0;
            iss >> k.time >> k.position.x >> k.position.y >> k.position.z >> k.rotation.x >> k.rotation.y >>
                k.rotation.z >> k.rotation.w >> k.scale.x >> k.scale.y >> k.scale.z >> easingIndex;
            if (!iss.fail()) {
                k.easing = easingFromIndex(easingIndex);
                currentTrack->addKeyframe(k);
            }
        } else if (line.rfind("EVENT ", 0) == 0) {
            std::istringstream iss(line.substr(6));
            AnimationEvent event;
            iss >> event.time >> event.name;
            if (!iss.fail()) loaded.events.push_back(std::move(event));
        } else if (line == "END") {
            break;
        }
        // Any other/unrecognized line is skipped -- forward-compatible with
        // a future field addition rather than failing the whole load.
    }

    *this = std::move(loaded);
    return true;
}

std::vector<float> collectKeyframeTimes(const AnimationClip& clip) {
    std::vector<float> times;
    for (const auto& track : clip.tracks) {
        for (const auto& keyframe : track.keyframes()) times.push_back(keyframe.time);
    }
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end(),
                             [](float a, float b) { return std::fabs(a - b) < kKeyframeMergeEpsilon; }),
                times.end());
    return times;
}

bool validateAnimationClipAgainstSkeleton(const AnimationClip& clip, const Skeleton& skeleton, std::string& outError) {
    if (clip.tracks.empty()) {
        outError = "clip \"" + clip.name + "\" has no tracks -- nothing would actually animate (missing channels)";
        return false;
    }
    for (const auto& track : clip.tracks) {
        if (skeleton.findJointIndex(track.targetName()) < 0) {
            outError = "track targets joint \"" + track.targetName() + "\", which does not exist in skeleton \"" +
                        skeleton.name + "\" (joint mismatch)";
            return false;
        }
        if (track.keyframes().empty()) {
            outError = "track for joint \"" + track.targetName() + "\" has no keyframes (missing channels)";
            return false;
        }
    }
    return true;
}

} // namespace engine::core
