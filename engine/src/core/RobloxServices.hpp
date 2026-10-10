#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/ECS.hpp"
#include "core/InstanceTree.hpp"

namespace engine::core {
class ResourceManager;
struct SceneLighting;
}

// Roblox's common services: TweenService, Debris, Sound playback and
// Lighting. CollectionService tags live in core/InstanceTree.hpp. The Luau
// side is in core/ScriptInstanceApi.cpp.
namespace engine::core::services {

struct TweenSettings {
    double time = 1.0;
    std::string style = "Quad";
    std::string direction = "Out";
    int repeatCount = 0; // -1 repeats forever
    bool reverses = false;
    double delay = 0.0;
};

// Roblox's easing curves: alpha in [0,1] -> eased alpha.
[[nodiscard]] double ease(const std::string& style, const std::string& direction, double alpha);
// Value between a and b; false for types that can't be tweened.
bool lerp(const InstanceValue& a, const InstanceValue& b, double alpha, InstanceValue& out);
[[nodiscard]] bool canTween(PropertyType type);

InstanceRef createTween(ECS& ecs, InstanceRef target, const TweenSettings& settings,
                        std::vector<std::pair<const PropertyDef*, InstanceValue>> goals);
void playTween(ECS& ecs, InstanceRef tween);
void pauseTween(ECS& ecs, InstanceRef tween);
void cancelTween(ECS& ecs, InstanceRef tween);

// Debris:AddItem.
void addDebris(ECS& ecs, InstanceRef item, double lifetime);

// Sound:Play/Stop/Pause/Resume. Fire Played, Stopped, ...
void playSound(ECS& ecs, InstanceRef sound);
void stopSound(ECS& ecs, InstanceRef sound);
void pauseSound(ECS& ecs, InstanceRef sound);
void resumeSound(ECS& ecs, InstanceRef sound);

// Sound files load through this (set by the Player and Studio). Paths are
// the game's own files; rbxassetid:// ids can't be downloaded.
void setResources(ECS& ecs, ResourceManager* resources);

// Advances tweens, Debris and sounds. Runs at Heartbeat (core/InstanceSignals.cpp).
void tick(ECS& ecs, double dt);
// Stop in Studio: drops tweens, timers and sound state.
void reset(ECS& ecs);

// The Lighting service, read by the hosts each frame. Returns false when
// the game has no Lighting, so the host keeps its own clock and look.
bool lightingClock(ECS& ecs, float& hours);
void applyLighting(ECS& ecs, SceneLighting& lighting);

} // namespace engine::core::services
