#include "core/RobloxServices.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtc/quaternion.hpp>

#include "core/Components.hpp"
#include "core/InstanceSignals.hpp"
#include "core/Logger.hpp"
#include "core/ResourceManager.hpp"
#include "core/SceneTypes.hpp"

namespace engine::core::services {
namespace {

constexpr double kPi = 3.14159265358979323846;

struct TweenGoal {
    const PropertyDef* property = nullptr;
    InstanceValue start;
    InstanceValue goal;
};

enum class TweenState { Begin = 0, Delayed = 1, Playing = 2, Paused = 3, Completed = 4, Cancelled = 5 };

const char* stateName(TweenState state) {
    switch (state) {
        case TweenState::Begin: return "Begin";
        case TweenState::Delayed: return "Delayed";
        case TweenState::Playing: return "Playing";
        case TweenState::Paused: return "Paused";
        case TweenState::Completed: return "Completed";
        case TweenState::Cancelled: return "Cancelled";
    }
    return "Begin";
}

struct Tween {
    InstanceRef target = kNoInstance;
    TweenSettings settings;
    std::vector<TweenGoal> goals;
    TweenState state = TweenState::Begin;
    double elapsed = 0.0;
    double delayLeft = 0.0;
};

struct SoundState {
    std::string loadedPath;
    bool wasPlaying = false;
};

struct ServicesState {
    double clock = 0.0;
    std::unordered_map<InstanceRef, Tween> tweens;
    std::vector<std::pair<double, InstanceRef>> debris;
    std::unordered_map<EntityId, SoundState> sounds;
    std::unordered_set<std::string> warnedSoundIds;
    ResourceManager* resources = nullptr;
};

ServicesState& stateOf(ECS& ecs) { return ecs.raw().ctx().emplace<ServicesState>(); }

double bounceOut(double t) {
    constexpr double n = 7.5625;
    constexpr double d = 2.75;
    if (t < 1.0 / d) return n * t * t;
    if (t < 2.0 / d) {
        t -= 1.5 / d;
        return n * t * t + 0.75;
    }
    if (t < 2.5 / d) {
        t -= 2.25 / d;
        return n * t * t + 0.9375;
    }
    t -= 2.625 / d;
    return n * t * t + 0.984375;
}

// The "In" shape of each style.
double easeIn(const std::string& style, double t) {
    if (style == "Linear") return t;
    if (style == "Sine") return 1.0 - std::cos(t * kPi / 2.0);
    if (style == "Quad") return t * t;
    if (style == "Cubic") return t * t * t;
    if (style == "Quart") return t * t * t * t;
    if (style == "Quint") return t * t * t * t * t;
    if (style == "Exponential") return t <= 0.0 ? 0.0 : std::pow(2.0, 10.0 * t - 10.0);
    if (style == "Circular") return 1.0 - std::sqrt(std::max(0.0, 1.0 - t * t));
    if (style == "Back") {
        constexpr double c1 = 1.70158;
        return (c1 + 1.0) * t * t * t - c1 * t * t;
    }
    if (style == "Elastic") {
        if (t <= 0.0 || t >= 1.0) return t;
        return -std::pow(2.0, 10.0 * t - 10.0) * std::sin((10.0 * t - 10.75) * (2.0 * kPi / 3.0));
    }
    if (style == "Bounce") return 1.0 - bounceOut(1.0 - t);
    return t * t;
}

const PropertyDef& playbackStateProperty() {
    static const PropertyDef* def = instances::findProperty("Tween", "PlaybackState");
    return *def;
}

void setState(ECS& ecs, InstanceRef self, Tween& tween, TweenState state) {
    tween.state = state;
    if (!instances::isAlive(ecs, self)) return;
    instances::setProperty(ecs, self, playbackStateProperty(),
                           InstanceValue::ofEnum("PlaybackState", stateName(state), static_cast<int>(state)));
}

void finish(ECS& ecs, InstanceRef self, Tween& tween, TweenState state) {
    setState(ecs, self, tween, state);
    if (SignalHub* hub = signals::findHub(ecs)) {
        hub->fire(self, "Completed",
                  {SignalArg::of(InstanceValue::ofEnum("PlaybackState", stateName(state), static_cast<int>(state)))});
    }
}

void apply(ECS& ecs, Tween& tween, double alpha) {
    const double eased = ease(tween.settings.style, tween.settings.direction, alpha);
    for (const TweenGoal& goal : tween.goals) {
        InstanceValue value;
        if (lerp(goal.start, goal.goal, eased, value)) instances::setProperty(ecs, tween.target, *goal.property, value);
    }
}

void tickTween(ECS& ecs, InstanceRef self, Tween& tween, double dt) {
    if (!instances::isAlive(ecs, tween.target)) {
        setState(ecs, self, tween, TweenState::Cancelled);
        return;
    }
    if (tween.state == TweenState::Delayed) {
        tween.delayLeft -= dt;
        if (tween.delayLeft > 0.0) return;
        dt = -tween.delayLeft;
        setState(ecs, self, tween, TweenState::Playing);
    }
    tween.elapsed += dt;
    const double time = std::max(tween.settings.time, 0.0);
    const double cycle = time * (tween.settings.reverses ? 2.0 : 1.0);
    const double cycles = tween.settings.repeatCount < 0 ? std::numeric_limits<double>::infinity()
                                                         : tween.settings.repeatCount + 1.0;
    if (cycle <= 0.0 || tween.elapsed >= cycle * cycles) {
        apply(ecs, tween, tween.settings.reverses ? 0.0 : 1.0);
        finish(ecs, self, tween, TweenState::Completed);
        return;
    }
    const double local = tween.elapsed - std::floor(tween.elapsed / cycle) * cycle;
    apply(ecs, tween, local < time ? local / time : 2.0 - local / time);
}

void tickSounds(ECS& ecs, ServicesState& s) {
    std::vector<EntityId> alive;
    for (auto [e, info, source] : ecs.raw().view<InstanceInfo, AudioSource>().each()) {
        if (info.className != "Sound") continue;
        alive.push_back(e);
        SoundState& sound = s.sounds[e];
        if (sound.loadedPath != source.path) {
            sound.loadedPath = source.path;
            if (auto* refs = ecs.tryGetComponent<ResourceRefs>(e)) {
                std::erase_if(refs->handles, [&](const ResourceHandle& handle) {
                    return handle.kind() == ResourceKind::Audio && handle.get() == source.soundHandle;
                });
            }
            source.soundHandle = AudioSource::kInvalidHandle;
            if (source.path.rfind("rbxasset", 0) == 0) {
                if (s.warnedSoundIds.insert(source.path).second) {
                    logWarn("Sound", "%s can't be downloaded from Roblox; put the sound file in your game folder and "
                                     "use its path as the SoundId", source.path.c_str());
                }
            } else if (!source.path.empty() && s.resources != nullptr && s.resources->hasLoader(ResourceKind::Audio)) {
                ResourceHandle handle = s.resources->acquire(ResourceKind::Audio, source.path);
                source.soundHandle = handle.get();
                ecs.raw().get_or_emplace<ResourceRefs>(e).handles.push_back(std::move(handle));
            }
        }
        const InstanceRef ref = instances::refOf(ecs, e);
        source.spatial = instances::classIsA(instances::className(ecs, instances::parent(ecs, ref)), "BasePart");
        // Audio::mix turns `playing` off when a sound reaches its end.
        if (sound.wasPlaying && !source.playing) {
            if (SignalHub* hub = signals::findHub(ecs)) {
                hub->fire(ref, "Ended", {SignalArg::of(InstanceValue::ofString(source.path))});
            }
        }
        sound.wasPlaying = source.playing;
    }
    if (alive.size() != s.sounds.size()) {
        std::erase_if(s.sounds, [&](const auto& entry) {
            return std::find(alive.begin(), alive.end(), entry.first) == alive.end();
        });
    }
}

AudioSource* soundOf(ECS& ecs, InstanceRef ref) { return ecs.tryGetComponent<AudioSource>(instances::entityOf(ecs, ref)); }

void fireSound(ECS& ecs, InstanceRef ref, const char* event) {
    if (SignalHub* hub = signals::findHub(ecs)) {
        const AudioSource* source = soundOf(ecs, ref);
        hub->fire(ref, event, {SignalArg::of(InstanceValue::ofString(source != nullptr ? source->path : ""))});
    }
}

// Script-made stops don't count as reaching the end.
void setPlaying(ECS& ecs, InstanceRef ref, bool playing, bool restart) {
    AudioSource* source = soundOf(ecs, ref);
    if (source == nullptr) return;
    source->playing = playing;
    if (restart) source->restart = true;
    stateOf(ecs).sounds[instances::entityOf(ecs, ref)].wasPlaying = playing;
}

InstanceValue lightingValue(ECS& ecs, InstanceRef lighting, const char* name) {
    InstanceValue value;
    if (const PropertyDef* def = instances::findProperty("Lighting", name)) {
        (void)instances::getProperty(ecs, lighting, *def, value);
    }
    return value;
}

} // namespace

double ease(const std::string& style, const std::string& direction, double alpha) {
    const double t = std::clamp(alpha, 0.0, 1.0);
    if (direction == "In") return easeIn(style, t);
    if (direction == "InOut") return t < 0.5 ? easeIn(style, t * 2.0) / 2.0 : 1.0 - easeIn(style, 2.0 - t * 2.0) / 2.0;
    return 1.0 - easeIn(style, 1.0 - t);
}

bool canTween(PropertyType type) {
    return type == PropertyType::Number || type == PropertyType::Vector3 || type == PropertyType::CFrame ||
           type == PropertyType::Color3 || type == PropertyType::Bool || type == PropertyType::Vector2 ||
           type == PropertyType::UDim || type == PropertyType::UDim2;
}

bool lerp(const InstanceValue& a, const InstanceValue& b, double alpha, InstanceValue& out) {
    if (a.type != b.type) return false;
    const float t = static_cast<float>(alpha);
    switch (a.type) {
        case InstanceValue::Type::Number: out = InstanceValue::ofNumber(a.number + (b.number - a.number) * alpha); return true;
        case InstanceValue::Type::Vector3: out = InstanceValue::ofVector3(glm::mix(a.vec, b.vec, t)); return true;
        case InstanceValue::Type::Color3: out = InstanceValue::ofColor3(glm::mix(a.vec, b.vec, t)); return true;
        case InstanceValue::Type::CFrame:
            out = InstanceValue::ofCFrame(glm::mix(a.vec, b.vec, t), glm::slerp(a.rot, b.rot, t));
            return true;
        case InstanceValue::Type::Bool: out = alpha >= 1.0 ? b : a; return true;
        case InstanceValue::Type::Vector2:
        case InstanceValue::Type::UDim:
        case InstanceValue::Type::UDim2:
            out = a;
            out.vec = glm::mix(a.vec, b.vec, t);
            out.number = a.number + (b.number - a.number) * alpha;
            return true;
        default: return false;
    }
}

InstanceRef createTween(ECS& ecs, InstanceRef target, const TweenSettings& settings,
                        std::vector<std::pair<const PropertyDef*, InstanceValue>> goals) {
    const InstanceRef self = instances::createUnchecked(ecs, "Tween");
    instances::setName(ecs, self, "Tween");
    instances::setProperty(ecs, self, *instances::findProperty("Tween", "Instance"), InstanceValue::ofInstance(target));
    Tween tween;
    tween.target = target;
    tween.settings = settings;
    for (auto& [property, goal] : goals) tween.goals.push_back(TweenGoal{property, InstanceValue{}, std::move(goal)});
    stateOf(ecs).tweens[self] = std::move(tween);
    return self;
}

void playTween(ECS& ecs, InstanceRef self) {
    auto& tweens = stateOf(ecs).tweens;
    const auto found = tweens.find(self);
    if (found == tweens.end()) return;
    Tween& tween = found->second;
    if (tween.state == TweenState::Playing || tween.state == TweenState::Delayed) return;
    if (tween.state != TweenState::Paused) {
        // A newer tween takes over the properties it shares with a playing one.
        for (auto& [otherRef, other] : tweens) {
            if (otherRef == self || other.target != tween.target ||
                (other.state != TweenState::Playing && other.state != TweenState::Delayed)) {
                continue;
            }
            const bool overlaps = std::any_of(other.goals.begin(), other.goals.end(), [&](const TweenGoal& a) {
                return std::any_of(tween.goals.begin(), tween.goals.end(),
                                   [&](const TweenGoal& b) { return a.property == b.property; });
            });
            if (overlaps) finish(ecs, otherRef, other, TweenState::Cancelled);
        }
        for (TweenGoal& goal : tween.goals) (void)instances::getProperty(ecs, tween.target, *goal.property, goal.start);
        tween.elapsed = 0.0;
        tween.delayLeft = tween.settings.delay;
    }
    setState(ecs, self, tween, tween.delayLeft > 0.0 ? TweenState::Delayed : TweenState::Playing);
}

void pauseTween(ECS& ecs, InstanceRef self) {
    auto& tweens = stateOf(ecs).tweens;
    const auto found = tweens.find(self);
    if (found == tweens.end()) return;
    if (found->second.state == TweenState::Playing || found->second.state == TweenState::Delayed) {
        setState(ecs, self, found->second, TweenState::Paused);
    }
}

void cancelTween(ECS& ecs, InstanceRef self) {
    auto& tweens = stateOf(ecs).tweens;
    const auto found = tweens.find(self);
    if (found == tweens.end()) return;
    Tween& tween = found->second;
    if (tween.state == TweenState::Completed || tween.state == TweenState::Cancelled ||
        tween.state == TweenState::Begin) {
        return;
    }
    tween.elapsed = 0.0;
    finish(ecs, self, tween, TweenState::Cancelled);
}

void addDebris(ECS& ecs, InstanceRef item, double lifetime) {
    ServicesState& s = stateOf(ecs);
    s.debris.emplace_back(s.clock + std::max(lifetime, 0.0), item);
}

void playSound(ECS& ecs, InstanceRef sound) {
    setPlaying(ecs, sound, true, true);
    fireSound(ecs, sound, "Played");
}

void stopSound(ECS& ecs, InstanceRef sound) {
    setPlaying(ecs, sound, false, true);
    fireSound(ecs, sound, "Stopped");
}

void pauseSound(ECS& ecs, InstanceRef sound) {
    const AudioSource* source = soundOf(ecs, sound);
    if (source == nullptr || !source->playing) return;
    setPlaying(ecs, sound, false, false);
    fireSound(ecs, sound, "Paused");
}

void resumeSound(ECS& ecs, InstanceRef sound) {
    const AudioSource* source = soundOf(ecs, sound);
    if (source == nullptr || source->playing) return;
    setPlaying(ecs, sound, true, false);
    fireSound(ecs, sound, "Resumed");
}

void setResources(ECS& ecs, ResourceManager* resources) { stateOf(ecs).resources = resources; }

void tick(ECS& ecs, double dt) {
    auto* s = ecs.raw().ctx().find<ServicesState>();
    if (s == nullptr) return;
    s->clock += dt;

    std::vector<InstanceRef> active;
    for (auto& [ref, tween] : s->tweens) {
        if (tween.state == TweenState::Playing || tween.state == TweenState::Delayed) active.push_back(ref);
    }
    std::sort(active.begin(), active.end());
    for (InstanceRef ref : active) {
        const auto found = s->tweens.find(ref);
        if (found != s->tweens.end() &&
            (found->second.state == TweenState::Playing || found->second.state == TweenState::Delayed)) {
            tickTween(ecs, ref, found->second, dt);
        }
    }
    std::erase_if(s->tweens, [&](const auto& entry) { return !instances::isAlive(ecs, entry.first); });

    std::vector<InstanceRef> expired;
    std::erase_if(s->debris, [&](const auto& item) {
        if (item.first > s->clock) return false;
        expired.push_back(item.second);
        return true;
    });
    for (InstanceRef ref : expired) {
        if (instances::isAlive(ecs, ref)) instances::destroy(ecs, ref);
    }

    tickSounds(ecs, *s);
}

void reset(ECS& ecs) {
    auto* s = ecs.raw().ctx().find<ServicesState>();
    if (s == nullptr) return;
    ResourceManager* resources = s->resources;
    *s = ServicesState{};
    s->resources = resources;
}

bool lightingClock(ECS& ecs, float& hours) {
    const InstanceRef lighting = instances::findService(ecs, "Lighting");
    if (lighting == kNoInstance) return false;
    hours = static_cast<float>(lightingValue(ecs, lighting, "ClockTime").number);
    return true;
}

void applyLighting(ECS& ecs, SceneLighting& lighting) {
    const InstanceRef ref = instances::findService(ecs, "Lighting");
    if (ref == kNoInstance) return;
    lighting.intensity *= static_cast<float>(std::max(0.0, lightingValue(ecs, ref, "Brightness").number) / 2.0);
    const glm::vec3 outdoor = lightingValue(ecs, ref, "OutdoorAmbient").vec / (128.0f / 255.0f);
    const glm::vec3 ambient = lightingValue(ecs, ref, "Ambient").vec;
    lighting.ambient = lighting.ambient * outdoor + ambient * 0.5f;
    lighting.ambientGround = lighting.ambientGround * outdoor + ambient * 0.5f;
    const double fogEnd = lightingValue(ecs, ref, "FogEnd").number;
    if (fogEnd > 0.0 && fogEnd < 100000.0) {
        // Squared-exponential fog that is 98% thick at FogEnd.
        lighting.fogDensity = std::max(lighting.fogDensity, static_cast<float>(1.978 / fogEnd));
        lighting.fogColor = lightingValue(ecs, ref, "FogColor").vec;
    }
}

} // namespace engine::core::services
