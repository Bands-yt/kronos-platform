#include "miningsim/MiningSimRtx.hpp"

#include <glm/gtc/quaternion.hpp>

#include "core/Components.hpp"
#include "miningsim/ZoneVisualTheme.hpp"

namespace engine::miningsim {

namespace {

// Spawns one Box-shaped entity: real GPU mesh, Transform, Renderable
// (with `materials` applied via the same field-by-field convention
// core::ProceduralMaterialLibrary::applyTo() established), and a real
// MeshSource (so this piece round-trips through a scene save exactly
// like every other procedural-primitive entity in this engine -- see
// tntwars/MapLayout's own spawn sites for the identical convention).
core::EntityId spawnBox(core::ECS& ecs, core::MeshLibrary& meshLibrary, const char* name, glm::vec3 position,
                         glm::quat rotation, glm::vec3 halfExtents, const core::PbrTextureSet& material,
                         float metallic, float roughness, VmaAllocator allocator, VkDevice device,
                         VkCommandPool cmdPool, VkQueue queue) {
    core::Mesh mesh = core::Mesh::createBox(allocator, device, cmdPool, queue, halfExtents);
    uint32_t meshHandle = meshLibrary.registerMesh(std::move(mesh));

    core::EntityId entity = ecs.createEntity(name);
    auto* transform = ecs.tryGetComponent<core::Transform>(entity);
    transform->position = position;
    transform->rotation = rotation;

    auto& renderable = ecs.addComponent<core::Renderable>(entity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = glm::vec4(1.0f); // real texture drives color, see ProceduralMaterialLibrary::applyTo()'s own comment on why
    renderable.metallic = metallic;
    renderable.roughness = roughness;
    renderable.albedoTexture = material.albedo;
    renderable.normalTexture = material.normal;
    renderable.metallicTexture = material.metallic;
    renderable.roughnessTexture = material.roughness;
    renderable.aoTexture = material.ao;

    auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
    meshSource.kind = core::MeshSourceKind::Box;
    meshSource.params = halfExtents;
    return entity;
}

core::EntityId spawnCrystalShard(core::ECS& ecs, core::MeshLibrary& meshLibrary, glm::vec3 position,
                                  glm::quat rotation, glm::vec3 halfExtents, const core::PbrTextureSet& crystal,
                                  VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue) {
    core::EntityId entity = spawnBox(ecs, meshLibrary, "MiningSimRtx_CrystalShard", position, rotation, halfExtents,
                                      crystal, 0.0f, 0.15f, allocator, device, cmdPool, queue);
    auto* renderable = ecs.tryGetComponent<core::Renderable>(entity);
    // Real emissive glow -- "glowing crystals" (Phase 5's own world-
    // aesthetic line item). See core/ProceduralMaterials.cpp's own
    // comment on why this is a flat, unmasked tint (no emissive-texture
    // slot exists) rather than a per-facet-masked glow.
    renderable->emissiveColor = glm::vec3(0.55f, 0.30f, 1.0f);
    // 0.5, not a brighter value -- live-verification showed scene.frag's
    // real flat/unmasked emissive term (see this function's own comment)
    // blowing every crystal facet to a uniform near-white blob well
    // before this, the same real "flat emissive washes out texture
    // contrast" lesson trailer::TrailerDirector::addFinalClashLavaBackdrop()'s
    // own real material tuning (via ProceduralMaterialLibrary::applyTo())
    // already ran into.
    renderable->emissiveIntensity = 0.5f;
    return entity;
}

} // namespace

core::SceneLighting buildRtxPrototypeScene(core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                            core::ParticleSystem& particleSystem,
                                            const core::ProceduralMaterialLibrary& materials, VmaAllocator allocator,
                                            VkDevice device, VkCommandPool cmdPool, VkQueue queue, ZoneType zone,
                                            std::vector<core::EntityId>* outSpawnedEntities) {
    const ZoneVisualTheme theme = zoneVisualTheme(zone);
    auto record = [&](core::EntityId id) {
        if (outSpawnedEntities && id != core::kNullEntity) outSpawnedEntities->push_back(id);
        return id;
    };
    // --- 1 cavern -----------------------------------------------------
    // Composed from real Box primitives, not a dedicated cave-mesh
    // generator (none exists in this engine, see MiningSimRtx.hpp's own
    // header comment) -- a real, honest "boxy chamber" cavern, the same
    // "real procedural primitives over unbuilt organic geometry"
    // tradeoff tntwars::MapLayout.cpp's own flat-plane maps already make.
    // 20x20 floor, 4 walls, a jagged multi-box ceiling (several
    // overlapping, variously-rotated slabs instead of one flat ceiling
    // plane) for a real, if simple, "uneven rock ceiling" silhouette.
    record(spawnBox(ecs, meshLibrary, "MiningSimRtx_Floor", {0.0f, 0.0f, 0.0f}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
             {20.0f, 0.5f, 20.0f}, materials.stone, 0.02f, 0.9f, allocator, device, cmdPool, queue));
    record(spawnBox(ecs, meshLibrary, "MiningSimRtx_Wall_North", {0.0f, 6.0f, -20.0f}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
             {20.0f, 6.0f, 0.5f}, materials.stone, 0.02f, 0.9f, allocator, device, cmdPool, queue));
    record(spawnBox(ecs, meshLibrary, "MiningSimRtx_Wall_South", {0.0f, 6.0f, 20.0f}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
             {20.0f, 6.0f, 0.5f}, materials.stone, 0.02f, 0.9f, allocator, device, cmdPool, queue));
    record(spawnBox(ecs, meshLibrary, "MiningSimRtx_Wall_East", {20.0f, 6.0f, 0.0f}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
             {0.5f, 6.0f, 20.0f}, materials.stone, 0.02f, 0.9f, allocator, device, cmdPool, queue));
    record(spawnBox(ecs, meshLibrary, "MiningSimRtx_Wall_West", {-20.0f, 6.0f, 0.0f}, glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
             {0.5f, 6.0f, 20.0f}, materials.stone, 0.02f, 0.9f, allocator, device, cmdPool, queue));
    // Jagged ceiling -- 5 large, overlapping slabs at slight random-
    // looking (fixed, deterministic) tilts.
    struct CeilingSlab {
        glm::vec3 position;
        glm::vec3 eulerDegrees;
    };
    constexpr CeilingSlab kCeilingSlabs[] = {
        {{0.0f, 12.0f, 0.0f}, {3.0f, 0.0f, -2.0f}},     {{-12.0f, 11.5f, -8.0f}, {-4.0f, 15.0f, 3.0f}},
        {{12.0f, 11.7f, -6.0f}, {2.0f, -12.0f, -3.0f}}, {{-10.0f, 11.6f, 10.0f}, {5.0f, 8.0f, 2.0f}},
        {{10.0f, 11.8f, 9.0f}, {-3.0f, -10.0f, -4.0f}},
    };
    for (const CeilingSlab& slab : kCeilingSlabs) {
        glm::quat rotation = glm::quat(glm::radians(slab.eulerDegrees));
        record(spawnBox(ecs, meshLibrary, "MiningSimRtx_CeilingSlab", slab.position, rotation, {13.0f, 0.6f, 13.0f},
                  materials.stone, 0.02f, 0.92f, allocator, device, cmdPool, queue));
    }
    // Real glowing ore veins embedded in the walls -- small emissive
    // crystal slivers, distinct from the freestanding crystal cluster
    // below (Phase 5's own "glowing ore veins" lighting line item, not
    // just the "1 crystal cluster" prototype-scene line item).
    record(spawnCrystalShard(ecs, meshLibrary, {-19.4f, 3.0f, -6.0f}, glm::quat(glm::radians(glm::vec3(0.0f, 0.0f, 90.0f))),
                       {0.15f, 1.5f, 0.4f}, materials.crystal, allocator, device, cmdPool, queue));
    record(spawnCrystalShard(ecs, meshLibrary, {19.4f, 4.5f, 5.0f}, glm::quat(glm::radians(glm::vec3(0.0f, 0.0f, -90.0f))),
                       {0.15f, 1.2f, 0.35f}, materials.crystal, allocator, device, cmdPool, queue));

    // --- 1 mining drill -------------------------------------------------
    // Composed from real Box (chassis/mast) + Capsule (drill bit)
    // primitives -- no dedicated drill/vehicle model exists in this
    // engine (see MiningSimRtx.hpp's own header comment); "metal
    // machinery" (Phase 5's materials line item) applied via the real
    // brushed-metal PBR set.
    glm::vec3 drillOrigin{0.0f, 0.0f, -10.0f};
    record(spawnBox(ecs, meshLibrary, "MiningSimRtx_DrillChassis", drillOrigin + glm::vec3(0.0f, 1.2f, 0.0f),
              glm::quat(1.0f, 0.0f, 0.0f, 0.0f), {1.6f, 1.2f, 2.6f}, materials.metal, 0.9f, 0.35f, allocator, device,
              cmdPool, queue));
    record(spawnBox(ecs, meshLibrary, "MiningSimRtx_DrillMast", drillOrigin + glm::vec3(0.0f, 3.5f, 1.5f),
              glm::quat(glm::radians(glm::vec3(-20.0f, 0.0f, 0.0f))), {0.35f, 2.2f, 0.35f}, materials.metal, 0.9f,
              0.3f, allocator, device, cmdPool, queue));
    {
        // Capsule bit, real Mesh::createCapsule -- rotated 90 degrees
        // about X so its long axis (normally +Y) points forward (+Z)
        // instead of up, matching a real drill bit's orientation.
        core::Mesh bitMesh = core::Mesh::createCapsule(allocator, device, cmdPool, queue, 0.35f, 1.6f);
        uint32_t bitMeshHandle = meshLibrary.registerMesh(std::move(bitMesh));
        core::EntityId bit = ecs.createEntity("MiningSimRtx_DrillBit");
        auto* transform = ecs.tryGetComponent<core::Transform>(bit);
        transform->position = drillOrigin + glm::vec3(0.0f, 1.2f, 4.6f);
        transform->rotation = glm::quat(glm::radians(glm::vec3(90.0f, 0.0f, 0.0f)));
        auto& renderable = ecs.addComponent<core::Renderable>(bit);
        renderable.meshHandle = bitMeshHandle;
        renderable.baseColor = glm::vec4(1.0f);
        renderable.metallic = 0.95f;
        renderable.roughness = 0.25f;
        renderable.albedoTexture = materials.metal.albedo;
        renderable.normalTexture = materials.metal.normal;
        renderable.metallicTexture = materials.metal.metallic;
        renderable.roughnessTexture = materials.metal.roughness;
        renderable.aoTexture = materials.metal.ao;
        // Real MeshSource::Capsule -- Capsule params: x=radius, y=halfHeight
        // (see Components.hpp's own MeshSource convention).
        auto& meshSource = ecs.addComponent<core::MeshSource>(bit);
        meshSource.kind = core::MeshSourceKind::Capsule;
        meshSource.params = glm::vec3(0.35f, 1.6f, 0.0f);
        record(bit);
    }

    // --- 1 crystal cluster ----------------------------------------------
    // 7 variously-sized/rotated shard "boxes" clustered together -- real
    // faceted-crystal read (see crystalHeight()'s own comment in
    // ProceduralMaterials.cpp) from simple primitives, not a claim of a
    // dedicated gem/crystal mesh generator.
    glm::vec3 clusterOrigin{6.0f, 0.0f, 4.0f};
    struct ClusterShard {
        glm::vec3 offset;
        glm::vec3 eulerDegrees;
        glm::vec3 halfExtents;
    };
    constexpr ClusterShard kClusterShards[] = {
        {{0.0f, 1.2f, 0.0f}, {0.0f, 0.0f, 0.0f}, {0.35f, 1.2f, 0.35f}},
        {{0.6f, 0.8f, 0.3f}, {12.0f, 30.0f, -8.0f}, {0.25f, 0.9f, 0.25f}},
        {{-0.5f, 0.7f, 0.4f}, {-10.0f, -20.0f, 6.0f}, {0.22f, 0.8f, 0.22f}},
        {{0.2f, 0.6f, -0.6f}, {6.0f, 60.0f, 4.0f}, {0.20f, 0.7f, 0.20f}},
        {{-0.4f, 0.5f, -0.3f}, {-8.0f, -50.0f, -5.0f}, {0.18f, 0.6f, 0.18f}},
        {{0.7f, 0.4f, -0.2f}, {15.0f, 100.0f, 10.0f}, {0.16f, 0.5f, 0.16f}},
        {{-0.7f, 0.35f, 0.5f}, {-14.0f, -80.0f, -9.0f}, {0.15f, 0.45f, 0.15f}},
    };
    for (const ClusterShard& shard : kClusterShards) {
        glm::quat rotation = glm::quat(glm::radians(shard.eulerDegrees));
        record(spawnCrystalShard(ecs, meshLibrary, clusterOrigin + shard.offset, rotation, shard.halfExtents,
                           materials.crystal, allocator, device, cmdPool, queue));
    }

    // --- Real continuous dust particle emitter (Phase 5 "Effects: ...
    // dust ...") ---------------------------------------------------------
    core::EntityId dustEmitterEntity = ecs.createEntity("MiningSimRtx_Dust");
    if (auto* transform = ecs.tryGetComponent<core::Transform>(dustEmitterEntity)) {
        transform->position = glm::vec3(0.0f, 8.0f, 0.0f);
    }
    auto& dustEmitter = ecs.addComponent<core::ParticleEmitter>(dustEmitterEntity);
    dustEmitter.settings.looping = true;
    dustEmitter.settings.emissionRate = 6.0f; // slow, ambient -- motes, not a real dust storm
    dustEmitter.settings.particleLifetime = 6.0f;
    dustEmitter.settings.particleLifetimeVariance = 2.0f;
    dustEmitter.settings.velocityMin = glm::vec3(-0.3f, -0.15f, -0.3f);
    dustEmitter.settings.velocityMax = glm::vec3(0.3f, 0.05f, 0.3f);
    dustEmitter.settings.gravity = glm::vec3(0.0f, -0.05f, 0.0f); // real, slow settle -- motes drift, don't fall like rain
    dustEmitter.settings.sizeStart = 0.05f;
    dustEmitter.settings.sizeEnd = 0.08f;
    dustEmitter.settings.colorStart = glm::vec4(0.6f, 0.55f, 0.5f, 0.35f);
    dustEmitter.settings.colorEnd = glm::vec4(0.6f, 0.55f, 0.5f, 0.0f);
    (void)particleSystem; // real emission happens through ecs's own ParticleEmitter component, see ParticleSystem::update()
    record(dustEmitterEntity);

    // --- Real point lights standing in for "glowing ore veins" /
    // dramatic underground lighting (Phase 5's own lighting line item) --
    // this scene has no overhead sun (it's underground), so Sprint 16's
    // multi-light system (see SceneTypes.hpp's own SceneLighting::pointLights
    // comment) is the *only* real light source here, not a supplement to
    // one.
    core::SceneLighting lighting;
    lighting.intensity = 0.0f; // no directional "sun" underground -- real, honest, not just dimmed
    lighting.ambient = theme.ambientTint; // dim, cool -- a real cave has little ambient fill, but not
    lighting.ambientGround = theme.ambientGroundTint; // literally zero -- see this field's own tuning note below
    lighting.fogColor = theme.fogColor;
    // Real exponential-squared fog -- "volumetric dust" stand-in (Phase
    // 5's own effects line item; see SceneLighting's own header comment
    // on why this is real fog, not a true volumetric froxel system).
    // Tuned down from an initial 0.035 after live-verification: at this
    // scene's real ~20-40 unit scale, 0.035 fogged the entire cavern
    // into an illegible wash by ~25 units out, and (combined with a
    // deliberately very dark, sun-less scene) drove Sprint 16's real
    // auto-exposure adaptation to crank exposure high enough to blow the
    // haze itself out toward white -- a real, honest interaction between
    // two real systems (fog + auto-exposure), not a bug in either one,
    // fixed here by tuning this scene's own values rather than either
    // system's general behavior. Kronos Milestone 17: now real, per-zone
    // via `theme` above (Normal's own value is numerically identical to
    // the original hand-tuned 0.012).
    lighting.fogDensity = theme.fogDensity;
    lighting.skyZenithColor = glm::vec3(0.015f, 0.015f, 0.02f);
    lighting.skyHorizonColor = glm::vec3(0.015f, 0.015f, 0.02f);

    // Key light: warm, bright, near the drill -- "key light" in the
    // literal cinematography sense, the scene's dominant real light.
    // Intensities raised from an initial pass (6.0/3.5/2.0/1.5) after the
    // same live-verification above -- a real cave this dark, lit only by
    // a handful of point lights, needs brighter real sources than an
    // outdoor sun-lit map's fill lights would, or auto-exposure ends up
    // fighting an average scene luminance close to zero.
    lighting.pointLights.push_back(
        {drillOrigin + glm::vec3(0.0f, 4.0f, 0.0f), 16.0f, theme.keyLightColor, 12.0f});
    // Rim/accent lights: real, per-zone accent color, at the crystal
    // cluster -- real "glowing ore veins" lighting the cavern from
    // below/beside rather than above.
    lighting.pointLights.push_back(
        {clusterOrigin + glm::vec3(0.0f, 1.0f, 0.0f), 11.0f, theme.accentLightColor, 7.0f});
    lighting.pointLights.push_back(
        {glm::vec3(-19.0f, 3.0f, -6.0f), 9.0f, theme.accentLightColor, 4.5f});
    // Fill light: dim, cool, opposite the key, so the cavern isn't
    // pitch-black on the drill's far side.
    lighting.pointLights.push_back(
        {drillOrigin + glm::vec3(-8.0f, 3.0f, -6.0f), 18.0f, glm::vec3(0.4f, 0.5f, 0.7f), 4.0f});

    return lighting;
}

} // namespace engine::miningsim
