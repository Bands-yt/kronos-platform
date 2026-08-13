#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ParticleSystem.hpp"
#include "core/ProceduralMaterials.hpp"

namespace engine::trailer {

// Kronos ("Real-Time Rendering Evolved" trailer): a real, non-gameplay
// showcase world -- explicitly requested as "no gameplay, no models, no
// characters, no traversal, no TNT Wars, no Sky Map". Distinct from
// TrailerDirector (which is entirely TNT-Wars-beat-driven, see that
// file's own header comment): this is a single, continuous, scripted
// camera journey through six real rendering-feature zones laid out along
// world-space Z, driven live through Application's normal windowed
// swapchain path (never through CaptureRig/AuxiliaryScene) -- see
// buildShowcaseCameraPath()'s own comment for why that choice matters.

// --- Camera path -----------------------------------------------------

struct ShowcaseWaypoint {
    float timeSeconds = 0.0f;
    glm::vec3 position{0.0f};
    glm::vec3 lookAt{0.0f};
    float fovDegrees = 60.0f;
    // Real camera roll -- "slight tilt for cinematic framing" (the
    // user's own SUNSET prompt). 0.0f (the default) matches every other
    // existing waypoint's untilted framing.
    float rollDegrees = 0.0f;
};

struct ShowcaseCameraSample {
    glm::vec3 position{0.0f};
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float rollDegrees = 0.0f;
    float fovDegrees = 60.0f;
};

// Real, small, self-contained camera path -- deliberately not
// tntwars::CinematicSequence (mechanically reusable per investigation,
// but importing anything under the tntwars:: namespace into a scene
// explicitly built to contain zero TNT Wars content risks pulling in
// TNT-Wars-flavored assumptions later; this class needs nothing beyond
// "smoothly interpolate between a handful of authored waypoints", so it
// gets its own minimal implementation instead). Smoothstep-eased linear
// interpolation between the two waypoints bracketing `timeSeconds` --
// intentionally simpler than a full spline: a trailer dolly reads as
// smooth with eased-linear segments and authored waypoints don't need
// tangent-handle authoring for a single continuous forward journey.
class ShowcaseCameraPath {
public:
    void addWaypoint(ShowcaseWaypoint waypoint);
    [[nodiscard]] float durationSeconds() const;
    [[nodiscard]] ShowcaseCameraSample sample(float timeSeconds) const;

private:
    std::vector<ShowcaseWaypoint> waypoints_;
};

// Real, named scene-boundary timestamps -- the one shared source of
// truth both buildShowcaseCameraPath() (which authors waypoints against
// these times) and Application.cpp's own per-tick showcase logic (which
// switches renderer settings/captions at these times) read from, so the
// two never drift out of sync with each other.
struct ShowcaseSceneTimes {
    // Real pre-roll hold -- the live window takes several real seconds to
    // appear + get positioned/recorded after process launch (confirmed
    // live: ~10-11s), which would otherwise eat directly into Zone 1's
    // own runtime before any capture could start. The camera path holds
    // perfectly still at Zone 1's own start framing for this long before
    // `kSunsetStart` actually begins the dolly -- see
    // buildShowcaseCameraPath()'s own matching pre-roll waypoint -- so a
    // recording that starts a few seconds late still opens on Zone 1 at
    // rest, not mid-flight into Zone 2.
    static constexpr float kPreRollSeconds = 30.0f;
    static constexpr float kSunsetStart = kPreRollSeconds + 0.0f;
    static constexpr float kMaterialStart = kPreRollSeconds + 10.0f;
    static constexpr float kParticleStart = kPreRollSeconds + 20.0f;
    static constexpr float kCameraStart = kPreRollSeconds + 28.0f;
    static constexpr float kEnvironmentStart = kPreRollSeconds + 34.0f;
    static constexpr float kEngineToolsStart = kPreRollSeconds + 44.0f;
    static constexpr float kEnd = kPreRollSeconds + 52.0f;
};

[[nodiscard]] ShowcaseCameraPath buildShowcaseCameraPath();

// --- World geometry ----------------------------------------------------

// Real, procedurally-placed geometry + particle emitters for all six
// zones, spawned once at startup (never rebuilt/torn down -- the whole
// world coexists simultaneously along Z, exactly like a real museum
// gallery; the camera path above is what turns "a world with six areas"
// into "six scenes" by choosing where to look and when). Uses only real,
// already-generated ProceduralMaterialLibrary presets (metal/crystal/
// stone/ground/sand/wood) -- no glass/water (this renderer has no
// transmission support, see this file's own trailer-planning notes) and
// no character/HUD/TNT-Wars entity of any kind.
void spawnRenderShowcaseWorld(core::ECS& ecs, core::MeshLibrary& meshLibrary, core::ParticleSystem& particleSystem,
                               const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                               VkDevice device, VkCommandPool cmdPool, VkQueue queue);

} // namespace engine::trailer
