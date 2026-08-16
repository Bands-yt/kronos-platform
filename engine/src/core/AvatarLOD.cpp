#include "core/AvatarLOD.hpp"

namespace engine::core {

bool avatarLODCategoryVisibleAtDistance(AvatarLODCategory category, float distanceToCamera,
                                         const AvatarLODThresholds& thresholds) {
    switch (category) {
        case AvatarLODCategory::Body: return true;
        case AvatarLODCategory::Face: return distanceToCamera < thresholds.faceCutoffMeters;
        case AvatarLODCategory::Accessory: return distanceToCamera < thresholds.accessoryCutoffMeters;
        case AvatarLODCategory::Clothing: return distanceToCamera < thresholds.clothingCutoffMeters;
    }
    return true; // unreachable, but a real, honest fail-visible default rather than UB
}

void updateAvatarLOD(ECS& ecs, const std::vector<EntityId>& entities, float distanceToCamera,
                      const AvatarLODThresholds& thresholds) {
    for (EntityId entity : entities) {
        auto* skinned = ecs.tryGetComponent<SkinnedRenderable>(entity);
        if (skinned == nullptr) continue;
        AvatarLODCategory category = AvatarLODCategory::Body;
        if (const auto* tag = ecs.tryGetComponent<AvatarLODTag>(entity)) {
            category = tag->category;
        }
        skinned->visible = avatarLODCategoryVisibleAtDistance(category, distanceToCamera, thresholds);
    }
}

} // namespace engine::core
