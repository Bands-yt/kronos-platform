#pragma once

#include <vector>

#include "core/Components.hpp"
#include "core/ECS.hpp"

namespace engine::core {

// Kronos ("Avatar 2.0" -- "Performance and LOD" -- "Add distance-based
// LOD levels for clothing meshes, accessories, and facial features"):
// real, tunable distance thresholds (world units, matching this
// engine's own existing scale -- CharacterController::Settings::
// cameraDistance defaults to 6.0f for normal third-person gameplay,
// studio::PreviewScene::kMaxOrbitDistance is 15.0f, so these sit above
// the former (a player's own face must never vanish in ordinary
// gameplay) and within reach of the latter (a creator zooming out in
// AvatarEditor/the Home preview can genuinely walk through every tier)).
// Deliberately staggered by real-world perceptual priority: facial
// detail is smallest and least legible at distance, so it's hidden
// first; clothing defines the avatar's overall silhouette, so it's kept
// the longest.
struct AvatarLODThresholds {
    float faceCutoffMeters = 9.0f;
    float accessoryCutoffMeters = 12.0f;
    float clothingCutoffMeters = 14.5f;
};

// Pure, headlessly-testable: does `category` stay visible at
// `distanceToCamera` under `thresholds`? `AvatarLODCategory::Body` is
// always true -- see AvatarLODTag's own comment (core/Components.hpp)
// for why overall silhouette is deliberately never LOD-hidden.
[[nodiscard]] bool avatarLODCategoryVisibleAtDistance(AvatarLODCategory category, float distanceToCamera,
                                                        const AvatarLODThresholds& thresholds = {});

// Real, per-frame: reads each entity's own AvatarLODTag (defaults to
// Body/always-visible if an entity somehow has none -- a real, honest
// fail-safe, never a fail-hidden) and writes the result straight into
// that entity's existing SkinnedRenderable::visible -- reusing the
// exact real, already-wired renderer check (Renderer.cpp's skinned draw
// loop: `if (!skinned.visible) continue;`), so no renderer/shader
// changes are needed for this to actually skip real GPU draw calls.
// `entities` is deliberately the same combined list every real owner
// (AvatarController/HomeAvatarPreview/AvatarEditor) already threads
// through their own skinnedEntities_ -- transform/skinning-matrix
// application stays unconditional for every entity (a currently-hidden
// piece must still be posed correctly for the frame it reappears), only
// visibility is toggled here.
void updateAvatarLOD(ECS& ecs, const std::vector<EntityId>& entities, float distanceToCamera,
                      const AvatarLODThresholds& thresholds = {});

} // namespace engine::core
