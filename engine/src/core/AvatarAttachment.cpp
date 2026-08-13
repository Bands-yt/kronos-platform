#include "core/AvatarAttachment.hpp"

#include "core/Components.hpp"

namespace engine::core {

void updateAttachments(ECS& ecs) {
    for (auto entity : ecs.view<AttachedTo>()) {
        const AttachedTo* attachment = ecs.tryGetComponent<AttachedTo>(entity);
        if (attachment == nullptr || attachment->parent == kNullEntity) continue;
        if (!ecs.raw().valid(attachment->parent)) continue;

        const Transform* parentTransform = ecs.tryGetComponent<Transform>(attachment->parent);
        Transform* childTransform = ecs.tryGetComponent<Transform>(entity);
        if (parentTransform == nullptr || childTransform == nullptr) continue;

        // localOffset rotates with the parent (a hat follows the head's
        // facing direction, not just its position) -- not scaled by
        // parentTransform->scale, see header comment.
        childTransform->position = parentTransform->position + parentTransform->rotation * attachment->localOffset;
        childTransform->rotation = parentTransform->rotation * attachment->localRotation;
    }
}

} // namespace engine::core
