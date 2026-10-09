#pragma once

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/AvatarController.hpp"
#include "core/AvatarLoadout.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/ECS.hpp"
#include "core/RiggedAvatar.hpp"
#include "core/RiggedMesh.hpp"

namespace engine::core {

class AnimationDatabase;
class Renderer;
struct LocalProfile;

struct PlayerAvatarLook {
    glm::vec4 skinTone{0.85f, 0.75f, 0.65f, 1.0f};
    HeadShape headShape = HeadShape::Oval;
    BodyProportions bodyProportions{};
    AvatarLoadout loadout{};
    AnimationOverrides animationOverrides{};
    ClothingFit clothingFit = ClothingFit::Tight;
};

// The look saved in the player's profile (Avatar page), with equipped
// items from `loadout` and animation overrides resolved through `animations`.
[[nodiscard]] PlayerAvatarLook playerAvatarLookFromProfile(const LocalProfile& profile, const AvatarLoadout& loadout,
                                                           const AnimationDatabase& animations);

// Builds the player's body, face, clothing, accessories and hair, appends
// every entity to `outEntities`, and returns the controller that animates
// them (with the shipped walk/run/jump clips loaded). Null if the body
// itself couldn't be built; a missing face, hat or clip is only logged.
[[nodiscard]] std::unique_ptr<AvatarController> spawnPlayerAvatarRig(ECS& ecs, Renderer& renderer,
                                                                      RiggedMeshLibrary& riggedMeshLibrary,
                                                                      const PlayerAvatarLook& look,
                                                                      const CatalogueIndex& catalogueIndex,
                                                                      std::vector<EntityId>& outEntities);

// Where a player should appear: above a part named "SpawnLocation" (or
// "SpawnPoint") if the scene has one, like Roblox, otherwise `fallback`.
[[nodiscard]] glm::vec3 findPlayerSpawnPosition(ECS& ecs, glm::vec3 fallback);

// Camera yaw a player should start with: the SpawnLocation's front (-Z),
// or with none, looking toward the middle of the world.
[[nodiscard]] float findPlayerSpawnYawDegrees(ECS& ecs, glm::vec3 spawnPosition);

} // namespace engine::core
