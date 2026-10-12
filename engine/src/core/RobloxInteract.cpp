#include "core/RobloxInteract.hpp"

#include <functional>
#include <string>

#include "core/Camera.hpp"
#include "core/Components.hpp"
#include "core/InstanceSignals.hpp"
#include "core/InstanceTree.hpp"
#include "core/PartBodies.hpp"
#include "core/Physics.hpp"
#include "core/RobloxPlayers.hpp"
#include "core/RobloxRemoteNet.hpp"
#include "core/UnionGeometry.hpp"

namespace engine::core::interact {

namespace {

struct State {
    InstanceRef hovered = kNoInstance;
};

InstanceValue get(ECS& ecs, InstanceRef ref, const char* property) {
    InstanceValue value;
    const PropertyDef* def = instances::findProperty(instances::className(ecs, ref), property);
    if (def != nullptr) (void)instances::getProperty(ecs, ref, *def, value);
    return value;
}

// The ClickDetector on the part, or on the Model (or other ancestor) around it.
InstanceRef detectorFor(ECS& ecs, InstanceRef part) {
    for (InstanceRef at = part; at != kNoInstance && at != kWorkspaceInstance && at != kGameInstance;
         at = instances::parent(ecs, at)) {
        for (InstanceRef child : instances::children(ecs, at)) {
            if (instances::className(ecs, child) == "ClickDetector") return child;
        }
    }
    return kNoInstance;
}

void emit(ECS& ecs, InstanceRef detector, const char* event, InstanceRef player) {
    if (SignalHub* hub = signals::findHub(ecs)) {
        hub->fire(detector, event, {SignalArg::of(InstanceValue::ofInstance(player))});
    }
    remotenet::sendInteraction(ecs, detector, event);
}

} // namespace

void update(ECS& ecs, const Pointer& pointer) {
    State& state = ecs.raw().ctx().emplace<State>();
    const InstanceRef player = players::localPlayer(ecs);
    Physics* physics = partbodies::physicsOf(ecs);
    InstanceRef hit = kNoInstance;
    if (player != kNoInstance && physics != nullptr && pointer.mouse.x >= 0.0f && !pointer.overGui &&
        pointer.view.x > 0.0f && pointer.view.y > 0.0f) {
        const InstanceValue character = get(ecs, player, "Character");
        glm::vec3 rootPosition(0.0f);
        bool haveRoot = false;
        if (character.ref != kNoInstance && instances::isAlive(ecs, character.ref)) {
            for (InstanceRef child : instances::children(ecs, character.ref)) {
                if (instances::name(ecs, child) == "HumanoidRootPart") {
                    rootPosition = glm::vec3(get(ecs, child, "Position").vec);
                    haveRoot = true;
                }
            }
        }
        if (haveRoot) {
            const Camera& camera = instances::scriptCamera(ecs);
            glm::vec3 origin;
            glm::vec3 direction;
            camera.screenPointToRay(2.0f * pointer.mouse.x / pointer.view.x - 1.0f,
                                    2.0f * pointer.mouse.y / pointer.view.y - 1.0f, pointer.view.x / pointer.view.y,
                                    origin, direction);
            const std::function<bool(EntityId)> accept = [&](EntityId e) {
                e = unions::owner(ecs, e);
                if (!ecs.raw().valid(e) || ecs.tryGetComponent<PlayerAvatarPart>(e) != nullptr) return false;
                const InstanceRef ref = instances::refOf(ecs, e);
                return ref != kNoInstance && instances::isInWorld(ecs, e);
            };
            const Physics::RaycastHit result = physics->raycast(origin, direction, 1000.0f, accept);
            if (result.hit) {
                const InstanceRef part = instances::refOf(ecs, unions::owner(ecs, result.entity));
                const InstanceRef detector = part == kNoInstance ? kNoInstance : detectorFor(ecs, part);
                if (detector != kNoInstance &&
                    glm::distance(rootPosition, result.point) <= static_cast<float>(get(ecs, detector, "MaxActivationDistance").number)) {
                    hit = detector;
                }
            }
        }
    }

    if (state.hovered != hit) {
        if (state.hovered != kNoInstance && instances::isAlive(ecs, state.hovered)) emit(ecs, state.hovered, "MouseHoverLeave", player);
        if (hit != kNoInstance) emit(ecs, hit, "MouseHoverEnter", player);
        state.hovered = hit;
    }
    if (hit != kNoInstance && pointer.left) emit(ecs, hit, "MouseClick", player);
    if (hit != kNoInstance && pointer.right) emit(ecs, hit, "RightMouseClick", player);
}

} // namespace engine::core::interact
