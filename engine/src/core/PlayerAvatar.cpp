#include "core/PlayerAvatar.hpp"

#include <cstdio>

#include "core/AvatarAccessories.hpp"
#include "core/AvatarFace.hpp"
#include "core/AvatarHair.hpp"
#include "core/Components.hpp"
#include "core/Hierarchy.hpp"
#include "core/Renderer.hpp"
#include "core/ResourcePaths.hpp"

namespace engine::core {

std::unique_ptr<AvatarController> spawnPlayerAvatarRig(ECS& ecs, Renderer& renderer, RiggedMeshLibrary& riggedMeshLibrary,
                                                       const PlayerAvatarLook& look, const CatalogueIndex& catalogueIndex,
                                                       std::vector<EntityId>& outEntities) {
    const Skeleton skeleton = applyBodyProportionsToSkeleton(buildHumanoidSkeleton(), look.bodyProportions);
    auto allocator = renderer.allocator();
    auto device = renderer.device();
    auto pool = renderer.commandPool();
    auto queue = renderer.graphicsQueue();

    std::string error;
    std::vector<EntityId> body;
    if (!spawnRiggedAvatar(ecs, skeleton, look.loadout, catalogueIndex, riggedMeshLibrary, allocator, device, pool, queue,
                           body, error, look.skinTone, look.headShape, look.bodyProportions)) {
        std::fprintf(stderr, "spawnPlayerAvatarRig: body failed: %s\n", error.c_str());
        return nullptr;
    }
    outEntities.insert(outEntities.end(), body.begin(), body.end());

    auto addParts = [&](const char* what, bool ok, std::vector<EntityId>& parts) {
        if (ok) {
            outEntities.insert(outEntities.end(), parts.begin(), parts.end());
        } else {
            std::fprintf(stderr, "spawnPlayerAvatarRig: %s failed: %s\n", what, error.c_str());
        }
        error.clear();
    };
    std::vector<EntityId> face, clothing, accessories, hair;
    addParts("face",
             spawnAvatarFace(ecs, skeleton, look.skinTone, riggedMeshLibrary, allocator, device, pool, queue, face, error),
             face);
    addParts("clothing",
             spawnAvatarClothing(ecs, skeleton, look.loadout, catalogueIndex, look.bodyProportions, look.clothingFit,
                                 riggedMeshLibrary, allocator, device, pool, queue, clothing, error),
             clothing);
    addParts("accessories",
             spawnAvatarAccessories(ecs, skeleton, look.loadout, catalogueIndex, riggedMeshLibrary, allocator, device,
                                    pool, queue, accessories, error),
             accessories);
    addParts("hair",
             spawnAvatarDefaultHair(ecs, skeleton, look.loadout, kDefaultHairColor, riggedMeshLibrary, allocator, device,
                                    pool, queue, hair, error),
             hair);

    auto controller = std::make_unique<AvatarController>(skeleton);
    const std::string animDir = resolveResourceDir(executableDirectory(), "assets", ENGINE_ASSET_DIR) + "/animations";
    auto loadClip = [&](const char* name, void (AvatarController::*setter)(AnimationClip), const std::string& overridePath) {
        AnimationClip clip;
        if (!overridePath.empty()) {
            if (clip.loadFromFile(overridePath)) {
                (controller.get()->*setter)(std::move(clip));
                return;
            }
            std::fprintf(stderr, "spawnPlayerAvatarRig: override clip \"%s\" failed, using the default.\n",
                         overridePath.c_str());
        }
        const std::string shipped = animDir + "/" + name + ".anim";
        if (clip.loadFromFile(shipped)) {
            (controller.get()->*setter)(std::move(clip));
        } else {
            std::fprintf(stderr, "spawnPlayerAvatarRig: could not load \"%s\".\n", shipped.c_str());
        }
    };
    const AnimationOverrides& overrides = look.animationOverrides;
    loadClip("idle", &AvatarController::setIdleClip, overrides.idleClipPath);
    loadClip("walk", &AvatarController::setWalkClip, overrides.walkClipPath);
    loadClip("run", &AvatarController::setRunClip, overrides.runClipPath);
    loadClip("jump_start", &AvatarController::setJumpClip, overrides.jumpStartClipPath);
    loadClip("jump_air", &AvatarController::setJumpAirClip, overrides.jumpAirClipPath);
    loadClip("jump_land", &AvatarController::setJumpLandClip, overrides.jumpLandClipPath);
    return controller;
}

glm::vec3 findPlayerSpawnPosition(ECS& ecs, glm::vec3 fallback) {
    for (auto [entity, name] : ecs.raw().view<Name>().each()) {
        if (name.value != "SpawnLocation" && name.value != "SpawnPoint") continue;
        if (ecs.tryGetComponent<Transform>(entity) == nullptr) continue;
        const glm::mat4 world = hierarchy::computeWorldMatrix(ecs, entity);
        float halfHeight = 0.5f;
        if (const auto* source = ecs.tryGetComponent<MeshSource>(entity); source && source->kind == MeshSourceKind::Box) {
            halfHeight = source->params.y;
        }
        const glm::vec3 top = glm::vec3(world * glm::vec4(0.0f, halfHeight, 0.0f, 1.0f));
        return top + glm::vec3(0.0f, 1.5f, 0.0f);
    }
    return fallback;
}

} // namespace engine::core
