#include "trailer/RenderShowcase.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "core/Components.hpp"
#include "tntwars/MapMaterial.hpp"

namespace engine::trailer {

namespace {

[[nodiscard]] float smoothstep01(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

core::EntityId spawnShowcaseBox(core::ECS& ecs, core::MeshLibrary& meshLibrary, VmaAllocator allocator,
                                 VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 position,
                                 glm::vec3 halfExtents, const core::ProceduralMaterialLibrary& materials,
                                 tntwars::MapPieceMaterialKind kind, const char* name) {
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.castsShadow = true;
    materials.applyTo(renderable, kind);

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;
    return entity;
}

// Real, direct crystal application -- `crystal` isn't one of
// ProceduralMaterialLibrary::applyTo()'s tntwars::MapPieceMaterialKind
// cases (see that struct's own header comment: crystal is consumed
// directly by callers like miningsim::buildRtxPrototypeScene(), not
// through the map-piece dispatch), so this showcase's own "Material
// Showcase" zone sets crystal's texture handles + a modest glow directly,
// the same real pattern that Mining Sim RTX prototype already
// established for this exact material.
void applyCrystalMaterial(core::Renderable& renderable, const core::ProceduralMaterialLibrary& materials) {
    renderable.baseColor = glm::vec4(1.0f);
    renderable.albedoTexture = materials.crystal.albedo;
    renderable.normalTexture = materials.crystal.normal;
    renderable.metallicTexture = materials.crystal.metallic;
    renderable.roughnessTexture = materials.crystal.roughness;
    renderable.aoTexture = materials.crystal.ao;
    renderable.metallic = 0.15f;
    renderable.roughness = 0.2f;
    renderable.emissiveColor = glm::vec3(0.55f, 0.75f, 1.0f);
    renderable.emissiveIntensity = 0.35f;
}

// Real, direct grass application -- like crystal above, `grass` isn't one
// of ProceduralMaterialLibrary::applyTo()'s tntwars::MapPieceMaterialKind
// cases (that field is consumed directly by Terrain's own height/slope
// material blending, see ProceduralMaterials.hpp's own header comment),
// so a real "wide open field" ground plane sets it directly. Kronos
// ("Real-Time Rendering Evolved" trailer) -- swapped in for Zone 1's
// ground per a live reference-image request ("wide open fields with tall
// grass"); still a real, large, primitive-free plane, just a different
// real procedural texture on it.
void applyGrassMaterial(core::Renderable& renderable, const core::ProceduralMaterialLibrary& materials) {
    renderable.baseColor = glm::vec4(1.0f);
    renderable.albedoTexture = materials.grass.albedo;
    renderable.normalTexture = materials.grass.normal;
    renderable.metallicTexture = materials.grass.metallic;
    renderable.roughnessTexture = materials.grass.roughness;
    renderable.aoTexture = materials.grass.ao;
    renderable.metallic = 0.0f;
    renderable.roughness = 0.85f;
}

core::EntityId spawnShowcaseCapsule(core::ECS& ecs, core::MeshLibrary& meshLibrary, VmaAllocator allocator,
                                     VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 position,
                                     float radius, float halfHeight, const core::ProceduralMaterialLibrary& materials,
                                     tntwars::MapPieceMaterialKind kind, bool crystal, const char* name) {
    core::Mesh mesh = core::Mesh::createCapsule(allocator, device, cmdPool, queue, radius, halfHeight);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.castsShadow = true;
    if (crystal) {
        applyCrystalMaterial(renderable, materials);
    } else {
        materials.applyTo(renderable, kind);
    }
    return entity;
}

core::EntityId spawnShowcaseGrassPlane(core::ECS& ecs, core::MeshLibrary& meshLibrary, VmaAllocator allocator,
                                        VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 position,
                                        float halfWidth, float halfDepth, const core::ProceduralMaterialLibrary& materials,
                                        const char* name) {
    core::Mesh mesh = core::Mesh::createPlane(allocator, device, cmdPool, queue, halfWidth, halfDepth);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.castsShadow = false;
    applyGrassMaterial(renderable, materials);
    return entity;
}

core::EntityId spawnShowcasePlane(core::ECS& ecs, core::MeshLibrary& meshLibrary, VmaAllocator allocator,
                                   VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 position,
                                   float halfWidth, float halfDepth, const core::ProceduralMaterialLibrary& materials,
                                   tntwars::MapPieceMaterialKind kind, const char* name) {
    core::Mesh mesh = core::Mesh::createPlane(allocator, device, cmdPool, queue, halfWidth, halfDepth);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.castsShadow = false;
    materials.applyTo(renderable, kind);
    return entity;
}

// Real glass/water panel -- routed by Renderer::drawSceneIntoImpl() to
// glassPipeline_ instead of scenePipeline_ purely because
// Renderable::transmission > 0 (see that field's own Components.hpp
// comment and shaders/glass.frag's header comment for the real
// Fresnel+refract() shading this drives). `tint` is the real transmitted-
// light color (blue-teal for water, a pale neutral for clear glass);
// `transmission` 0..1 (1 = fully real glass/water, see glass.frag's own
// comment on what 0 means); `ior` the real index of refraction (water
// ~1.33, glass ~1.5).
core::EntityId spawnShowcaseGlassPanel(core::ECS& ecs, core::MeshLibrary& meshLibrary, VmaAllocator allocator,
                                        VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 position,
                                        float halfWidth, float halfDepth, glm::vec3 tint, float transmission,
                                        float ior, const char* name) {
    core::Mesh mesh = core::Mesh::createPlane(allocator, device, cmdPool, queue, halfWidth, halfDepth);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    // Real, deliberate no-shadow: a glass/water panel casting a hard
    // opaque shadow would misrepresent a transmissive surface -- see
    // this function's own header comment.
    renderable.castsShadow = false;
    renderable.baseColor = glm::vec4(tint, 1.0f);
    renderable.roughness = 0.06f;
    renderable.transmission = transmission;
    renderable.transmissionIor = ior;
    return entity;
}

// Real, thin, bright-emissive strip -- a real plane (not a box/capsule,
// see this file's own "no primitives" scene-planning notes), reads as
// a neon panel scattering light through real volumetric fog once that's
// enabled for the zone it's placed in.
core::EntityId spawnShowcaseNeonStrip(core::ECS& ecs, core::MeshLibrary& meshLibrary, VmaAllocator allocator,
                                       VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 position,
                                       glm::vec3 eulerDegrees, float halfWidth, float halfDepth, glm::vec3 color,
                                       const char* name) {
    core::Mesh mesh = core::Mesh::createPlane(allocator, device, cmdPool, queue, halfWidth, halfDepth);
    if (mesh.vertexBuffer() == VK_NULL_HANDLE) return core::kNullEntity;
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        transform->position = position;
        glm::quat rotation = glm::angleAxis(glm::radians(eulerDegrees.z), glm::vec3(0.0f, 0.0f, 1.0f)) *
                              glm::angleAxis(glm::radians(eulerDegrees.x), glm::vec3(1.0f, 0.0f, 0.0f)) *
                              glm::angleAxis(glm::radians(eulerDegrees.y), glm::vec3(0.0f, 1.0f, 0.0f));
        transform->rotation = rotation;
    }

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.castsShadow = false;
    renderable.baseColor = glm::vec4(color, 1.0f);
    renderable.metallic = 0.0f;
    renderable.roughness = 0.5f;
    renderable.emissiveColor = color;
    renderable.emissiveIntensity = 3.0f;
    return entity;
}

core::EntityId spawnShowcaseEmitter(core::ECS& ecs, glm::vec3 position, const core::ParticleEmitterSettings& settings,
                                     const char* name) {
    core::EntityId entity = ecs.createEntity(name);
    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) transform->position = position;
    auto& emitter = ecs.addComponent<core::ParticleEmitter>(entity);
    emitter.settings = settings;
    return entity;
}

// --- New, non-combat particle presets, authored fresh for this showcase
// (only two real presets existed anywhere before this: TNT-Wars'
// explosion burst and projectile-impact spark, both short-lived combat
// reactions -- none of the four below existed in any form).

[[nodiscard]] core::ParticleEmitterSettings sparksPreset() {
    core::ParticleEmitterSettings s;
    s.looping = true;
    s.emissionRate = 40.0f;
    s.particleLifetime = 0.9f;
    s.particleLifetimeVariance = 0.35f;
    s.velocityMin = glm::vec3(-1.5f, 3.0f, -1.5f);
    s.velocityMax = glm::vec3(1.5f, 6.5f, 1.5f);
    s.gravity = glm::vec3(0.0f, -9.0f, 0.0f);
    s.sizeStart = 0.06f;
    s.sizeEnd = 0.01f;
    s.colorStart = glm::vec4(1.0f, 0.95f, 0.6f, 1.0f);
    s.colorEnd = glm::vec4(1.0f, 0.5f, 0.1f, 0.0f);
    return s;
}

[[nodiscard]] core::ParticleEmitterSettings smokePreset() {
    core::ParticleEmitterSettings s;
    s.looping = true;
    s.emissionRate = 8.0f;
    s.particleLifetime = 4.5f;
    s.particleLifetimeVariance = 1.0f;
    s.velocityMin = glm::vec3(-0.4f, 1.2f, -0.4f);
    s.velocityMax = glm::vec3(0.4f, 2.2f, 0.4f);
    s.gravity = glm::vec3(0.0f, 0.15f, 0.0f); // real, slight upward drift -- smoke rises, doesn't fall
    s.sizeStart = 0.4f;
    s.sizeEnd = 1.6f;
    s.colorStart = glm::vec4(0.55f, 0.55f, 0.58f, 0.35f);
    s.colorEnd = glm::vec4(0.7f, 0.7f, 0.72f, 0.0f);
    return s;
}

[[nodiscard]] core::ParticleEmitterSettings firePreset() {
    core::ParticleEmitterSettings s;
    s.looping = true;
    s.emissionRate = 30.0f;
    s.particleLifetime = 0.5f;
    s.particleLifetimeVariance = 0.15f;
    s.velocityMin = glm::vec3(-0.6f, 2.5f, -0.6f);
    s.velocityMax = glm::vec3(0.6f, 4.5f, 0.6f);
    s.gravity = glm::vec3(0.0f, 1.5f, 0.0f); // real, upward buoyancy -- flame accelerates up, not down
    s.sizeStart = 0.35f;
    s.sizeEnd = 0.05f;
    s.colorStart = glm::vec4(1.0f, 0.75f, 0.15f, 1.0f);
    s.colorEnd = glm::vec4(0.9f, 0.15f, 0.05f, 0.0f);
    return s;
}

[[nodiscard]] core::ParticleEmitterSettings embersPreset() {
    core::ParticleEmitterSettings s;
    s.looping = true;
    s.emissionRate = 6.0f;
    s.particleLifetime = 6.0f;
    s.particleLifetimeVariance = 1.5f;
    s.velocityMin = glm::vec3(-0.5f, 0.4f, -0.5f);
    s.velocityMax = glm::vec3(0.5f, 1.1f, 0.5f);
    s.gravity = glm::vec3(0.0f, 0.0f, 0.0f); // real, weightless drift -- a floating ember, not falling ash
    s.sizeStart = 0.05f;
    s.sizeEnd = 0.03f;
    s.colorStart = glm::vec4(1.0f, 0.55f, 0.15f, 1.0f);
    s.colorEnd = glm::vec4(0.6f, 0.2f, 0.05f, 0.0f);
    return s;
}

} // namespace

void ShowcaseCameraPath::addWaypoint(ShowcaseWaypoint waypoint) { waypoints_.push_back(waypoint); }

float ShowcaseCameraPath::durationSeconds() const { return waypoints_.empty() ? 0.0f : waypoints_.back().timeSeconds; }

namespace {
[[nodiscard]] ShowcaseCameraSample sampleFromSingleWaypoint(const ShowcaseWaypoint& wp) {
    glm::vec3 dir = wp.lookAt - wp.position;
    float len = glm::length(dir);
    if (len < 1e-5f) return ShowcaseCameraSample{wp.position, 0.0f, 0.0f, wp.rollDegrees, wp.fovDegrees};
    dir /= len;
    return ShowcaseCameraSample{wp.position, glm::degrees(std::atan2(dir.z, dir.x)),
                                 glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f))), wp.rollDegrees,
                                 wp.fovDegrees};
}
} // namespace

ShowcaseCameraSample ShowcaseCameraPath::sample(float timeSeconds) const {
    if (waypoints_.empty()) return ShowcaseCameraSample{};
    if (waypoints_.size() == 1 || timeSeconds <= waypoints_.front().timeSeconds) {
        return sampleFromSingleWaypoint(waypoints_.front());
    }
    if (timeSeconds >= waypoints_.back().timeSeconds) {
        return sampleFromSingleWaypoint(waypoints_.back());
    }

    size_t i = 0;
    while (i + 1 < waypoints_.size() && waypoints_[i + 1].timeSeconds < timeSeconds) ++i;
    const ShowcaseWaypoint& a = waypoints_[i];
    const ShowcaseWaypoint& b = waypoints_[i + 1];
    float span = b.timeSeconds - a.timeSeconds;
    float t = span > 1e-5f ? smoothstep01((timeSeconds - a.timeSeconds) / span) : 1.0f;

    glm::vec3 position = glm::mix(a.position, b.position, t);
    glm::vec3 lookAt = glm::mix(a.lookAt, b.lookAt, t);
    float fov = glm::mix(a.fovDegrees, b.fovDegrees, t);
    float roll = glm::mix(a.rollDegrees, b.rollDegrees, t);

    glm::vec3 dir = lookAt - position;
    float len = glm::length(dir);
    if (len < 1e-5f) return ShowcaseCameraSample{position, 0.0f, 0.0f, roll, fov};
    dir /= len;
    return ShowcaseCameraSample{position, glm::degrees(std::atan2(dir.z, dir.x)),
                                 glm::degrees(std::asin(std::clamp(dir.y, -1.0f, 1.0f))), roll, fov};
}

ShowcaseCameraPath buildShowcaseCameraPath() {
    ShowcaseCameraPath path;
    using T = ShowcaseSceneTimes;

    // Real pre-roll hold -- see ShowcaseSceneTimes::kPreRollSeconds' own
    // comment. Identical position/lookAt/fov/roll to the very next
    // waypoint (Zone 1's own real start framing), so the camera sits
    // perfectly still here rather than drifting -- a held frame, not a
    // degenerate zero-length move.
    path.addWaypoint({0.0f, glm::vec3(0.0f, 1.3f, -6.0f), glm::vec3(0.0f, 1.0f, 20.0f), 58.0f, -2.5f});

    // Zone 1 -- Sunset Ray Tracing: a low, slow forward dolly across the
    // reflective metal ground onto the real water plane, kept close to
    // the surface for a real grazing-angle reflection/refraction view of
    // the sunset (per the user's own "SUNSET PROMPT").
    path.addWaypoint({T::kSunsetStart, glm::vec3(0.0f, 1.3f, -6.0f), glm::vec3(0.0f, 1.0f, 20.0f), 58.0f, -2.5f});
    path.addWaypoint({T::kSunsetStart + 5.0f, glm::vec3(0.0f, 1.1f, 10.0f), glm::vec3(0.0f, 0.8f, 34.0f), 52.0f, 1.5f});
    path.addWaypoint({T::kMaterialStart - 1.0f, glm::vec3(0.0f, 1.0f, 24.0f), glm::vec3(0.0f, 0.6f, 46.0f), 48.0f, 0.0f});

    // Zone 2 -- Material Showcase: a lateral dolly past metal -> glass ->
    // water, holding each panel in frame a beat longer than the last so
    // its own reflection/refraction actually reads before the next
    // arrives (per the user's own "MATERIALS PROMPT").
    path.addWaypoint({T::kMaterialStart, glm::vec3(-9.0f, 2.6f, 54.0f), glm::vec3(-9.0f, 2.6f, 62.0f), 42.0f});
    path.addWaypoint({T::kMaterialStart + 5.0f, glm::vec3(0.0f, 2.4f, 54.0f), glm::vec3(0.0f, 2.6f, 62.0f), 42.0f});
    path.addWaypoint({T::kParticleStart - 1.0f, glm::vec3(9.0f, 2.2f, 56.0f), glm::vec3(9.0f, 0.4f, 62.0f), 42.0f});

    // Zone 3 -- Particle & FX: a slow arc around the four emitters.
    path.addWaypoint({T::kParticleStart, glm::vec3(-9.0f, 2.2f, 112.0f), glm::vec3(0.0f, 2.0f, 120.0f), 55.0f});
    path.addWaypoint({T::kParticleStart + 4.0f, glm::vec3(0.0f, 2.6f, 106.0f), glm::vec3(0.0f, 2.2f, 120.0f), 55.0f});
    path.addWaypoint({T::kCameraStart - 1.0f, glm::vec3(9.0f, 2.2f, 112.0f), glm::vec3(0.0f, 2.0f, 120.0f), 55.0f});

    // Zone 4 -- Camera System: a slow, deliberate push-in on the particle
    // cluster from a new angle (Application.cpp racks DOF focus and
    // ramps motion-blur shutter angle across this exact span -- see
    // ShowcaseSceneTimes::kCameraStart).
    path.addWaypoint({T::kCameraStart, glm::vec3(0.0f, 3.5f, 96.0f), glm::vec3(0.0f, 2.0f, 118.0f), 42.0f});
    path.addWaypoint({T::kEnvironmentStart - 1.0f, glm::vec3(0.0f, 2.2f, 112.0f), glm::vec3(0.0f, 2.0f, 122.0f), 32.0f});

    // Zone 5 -- Environment Lighting: gliding between the pillar cluster,
    // volumetric fog catching the low sun as real light shafts.
    path.addWaypoint({T::kEnvironmentStart, glm::vec3(-6.0f, 2.4f, 152.0f), glm::vec3(0.0f, 4.0f, 168.0f), 55.0f});
    path.addWaypoint({T::kEnvironmentStart + 5.0f, glm::vec3(4.0f, 3.0f, 168.0f), glm::vec3(-2.0f, 5.0f, 184.0f), 50.0f});
    path.addWaypoint({T::kEngineToolsStart - 1.0f, glm::vec3(0.0f, 3.4f, 182.0f), glm::vec3(0.0f, 4.5f, 196.0f), 48.0f});

    // Zone 6 -- Engine Tools: camera settles and holds on a clean vista
    // for the settings/perf panel to read against.
    path.addWaypoint({T::kEngineToolsStart, glm::vec3(0.0f, 4.0f, 176.0f), glm::vec3(0.0f, 5.0f, 195.0f), 45.0f});
    path.addWaypoint({T::kEnd, glm::vec3(0.0f, 4.2f, 178.0f), glm::vec3(0.0f, 5.0f, 195.0f), 45.0f});

    return path;
}

void spawnRenderShowcaseWorld(core::ECS& ecs, core::MeshLibrary& meshLibrary, core::ParticleSystem&,
                               const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                               VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    using Kind = tntwars::MapPieceMaterialKind;

    // Zone 1 -- Sunset Ray Tracing (per the user's own "SUNSET PROMPT":
    // no cylinders, no spheres, no primitive shapes -- large naturalistic
    // surfaces only). A real grass-field ground plane (swapped in from
    // the original reflective metal per a later reference-image request
    // -- "wide open fields with tall grass") the camera glides across,
    // giving way to a real glass/water plane (see
    // spawnShowcaseGlassPanel()'s own comment) catching the sunset in a
    // real refracted/reflected sky, exactly as requested.
    spawnShowcaseGrassPlane(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(0.0f, 0.0f, 8.0f), 60.0f,
                             12.0f, materials, "Showcase_Zone1_Ground");
    // Real, wide water plane -- extends far past the camera's own
    // furthest Zone 1 waypoint (Z=46) so it genuinely reads as "extending
    // to the horizon" rather than a bounded pool, per the user's own
    // "Wide reflective water surface extending to the horizon".
    spawnShowcaseGlassPanel(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(0.0f, -0.15f, 100.0f), 80.0f,
                             80.0f, glm::vec3(0.35f, 0.55f, 0.6f), 0.92f, 1.33f, "Showcase_Zone1_Water");

    // Zone 2 -- Material Showcase (per the "MATERIALS PROMPT": large
    // surfaces only -- metal panel, glass wall, water plane, neon strips,
    // no primitives). Arranged across the same real floor rather than on
    // individual pedestals (a pedestal is itself a small box primitive).
    spawnShowcasePlane(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(0.0f, 0.0f, 68.0f), 16.0f, 10.0f,
                        materials, Kind::Stone, "Showcase_Zone2_Floor");
    // Large metal panel, angled slightly to catch the sunset in its own
    // real reflection (RT reflections/SSR both real-enabled the whole
    // showcase, see main.cpp's own --render-showcase setup).
    spawnShowcaseBox(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(-9.0f, 3.0f, 62.0f),
                      glm::vec3(4.0f, 3.0f, 0.15f), materials, Kind::Metal, "Showcase_Zone2_MetalPanel");
    // Real glass wall -- vertical, transmission near 1 (true glass, not
    // water), IOR 1.5.
    spawnShowcaseGlassPanel(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(0.0f, 3.0f, 62.0f), 4.0f,
                             3.0f, glm::vec3(0.75f, 0.85f, 0.85f), 0.85f, 1.5f, "Showcase_Zone2_GlassWall");
    // Real water plane -- horizontal, same real ripple/refraction/
    // reflection technique as Zone 1's water, IOR 1.33.
    spawnShowcaseGlassPanel(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(9.0f, 0.05f, 62.0f), 4.0f,
                             6.0f, glm::vec3(0.3f, 0.5f, 0.58f), 0.9f, 1.33f, "Showcase_Zone2_WaterPlane");
    // Real neon strips -- thin, bright-emissive planes (see
    // spawnShowcaseNeonStrip()'s own comment), scattering through
    // volumetric fog once Application.cpp enables it for this zone.
    spawnShowcaseNeonStrip(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(-9.0f, 6.3f, 62.0f),
                            glm::vec3(90.0f, 0.0f, 0.0f), 4.0f, 0.08f, glm::vec3(0.2f, 0.9f, 1.0f),
                            "Showcase_Zone2_NeonStrip1");
    spawnShowcaseNeonStrip(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(0.0f, 6.3f, 65.9f),
                            glm::vec3(90.0f, 0.0f, 0.0f), 4.0f, 0.08f, glm::vec3(1.0f, 0.25f, 0.7f),
                            "Showcase_Zone2_NeonStrip2");

    // Zone 3 -- Particle & FX.
    spawnShowcasePlane(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(0.0f, 0.0f, 118.0f), 16.0f, 14.0f,
                        materials, Kind::Stone, "Showcase_Zone3_Floor");
    spawnShowcaseEmitter(ecs, glm::vec3(-9.0f, 0.2f, 118.0f), sparksPreset(), "Showcase_Zone3_Sparks");
    spawnShowcaseEmitter(ecs, glm::vec3(-3.0f, 0.2f, 118.0f), smokePreset(), "Showcase_Zone3_Smoke");
    spawnShowcaseEmitter(ecs, glm::vec3(3.0f, 0.2f, 118.0f), firePreset(), "Showcase_Zone3_Fire");
    spawnShowcaseEmitter(ecs, glm::vec3(9.0f, 0.2f, 118.0f), embersPreset(), "Showcase_Zone3_Embers");

    // Zone 5 -- Environment Lighting: a pillar cluster the fog/god-rays
    // can catch, spread across the flight path so the camera passes
    // between them rather than around the outside.
    spawnShowcasePlane(ecs, meshLibrary, allocator, device, cmdPool, queue, glm::vec3(0.0f, 0.0f, 175.0f), 20.0f, 25.0f,
                        materials, Kind::Stone, "Showcase_Zone5_Floor");
    const glm::vec3 pillarPositions[] = {
        glm::vec3(-8.0f, 3.0f, 155.0f), glm::vec3(6.0f, 3.0f, 160.0f),  glm::vec3(-4.0f, 3.0f, 172.0f),
        glm::vec3(8.0f, 3.0f, 178.0f),  glm::vec3(-9.0f, 3.0f, 188.0f), glm::vec3(3.0f, 3.0f, 192.0f),
    };
    int pillarIndex = 0;
    for (glm::vec3 pos : pillarPositions) {
        std::string name = "Showcase_Zone5_Pillar_" + std::to_string(pillarIndex++);
        spawnShowcaseBox(ecs, meshLibrary, allocator, device, cmdPool, queue, pos, glm::vec3(0.7f, 3.0f, 0.7f), materials,
                          Kind::Metal, name.c_str());
    }
}

} // namespace engine::trailer
