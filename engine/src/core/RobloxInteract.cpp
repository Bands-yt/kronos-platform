#include "core/RobloxInteract.hpp"

#include <functional>
#include <algorithm>
#include <string>
#include <vector>

#include "core/Camera.hpp"
#include "core/Components.hpp"
#include "core/InstanceSignals.hpp"
#include "core/InstanceTree.hpp"
#include "core/PartBodies.hpp"
#include "core/Physics.hpp"
#include "core/RobloxInput.hpp"
#include "core/RobloxPlayers.hpp"
#include "core/RobloxRemoteNet.hpp"
#include "core/UnionGeometry.hpp"

namespace engine::core::interact {

namespace {

struct State {
    InstanceRef hovered = kNoInstance;
    // ProximityPrompts: found by a slow scan, then checked every frame.
    std::vector<InstanceRef> prompts;
    float scanIn = 0.0f;
    InstanceRef shown = kNoInstance;
    bool keyWasDown = false;
    bool holding = false;
    bool triggered = false;
    float held = 0.0f;
    ShownPrompt info;
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


InstanceRef holderPart(ECS& ecs, InstanceRef ref) {
    InstanceRef at = instances::parent(ecs, ref);
    while (at != kNoInstance && !instances::classIsA(instances::className(ecs, at), "BasePart")) {
        at = instances::parent(ecs, at);
    }
    return at;
}

void endHold(ECS& ecs, State& st, InstanceRef player) {
    if (!st.holding || st.shown == kNoInstance || !instances::isAlive(ecs, st.shown)) {
        st.holding = false;
        return;
    }
    if (st.triggered) emit(ecs, st.shown, "TriggerEnded", player);
    emit(ecs, st.shown, "PromptButtonHoldEnded", player);
    st.holding = false;
    st.triggered = false;
    st.held = 0.0f;
}

// Picks the nearest usable prompt, shows it and turns the key into Triggered.
void updatePrompts(ECS& ecs, State& st, const Pointer& pointer, InstanceRef player, bool haveRoot, glm::vec3 root,
                   Physics* physics) {
    st.scanIn -= pointer.dt;
    if (st.scanIn <= 0.0f) {
        st.scanIn = 0.25f;
        st.prompts.clear();
        for (InstanceRef ref : instances::descendants(ecs, kWorkspaceInstance)) {
            if (instances::className(ecs, ref) == "ProximityPrompt") st.prompts.push_back(ref);
        }
    }
    InstanceRef best = kNoInstance;
    glm::vec3 bestPosition(0.0f);
    float bestDistance = 1e30f;
    if (haveRoot && player != kNoInstance) {
        const Camera& camera = instances::scriptCamera(ecs);
        for (InstanceRef prompt : st.prompts) {
            if (!instances::isAlive(ecs, prompt) || !get(ecs, prompt, "Enabled").boolean) continue;
            const InstanceRef part = holderPart(ecs, prompt);
            if (part == kNoInstance || !instances::isInWorld(ecs, instances::entityOf(ecs, part))) continue;
            const glm::vec3 position(get(ecs, part, "Position").vec);
            const float distance = glm::distance(root, position);
            if (distance > static_cast<float>(get(ecs, prompt, "MaxActivationDistance").number) || distance >= bestDistance) {
                continue;
            }
            if (physics != nullptr && get(ecs, prompt, "RequiresLineOfSight").boolean) {
                const glm::vec3 toPart = position - camera.position;
                const float length = glm::length(toPart);
                if (length > 0.01f) {
                    const EntityId own = instances::entityOf(ecs, part);
                    const std::function<bool(EntityId)> accept = [&](EntityId e) {
                        e = unions::owner(ecs, e);
                        if (e == own || !ecs.raw().valid(e) || ecs.tryGetComponent<PlayerAvatarPart>(e) != nullptr) return false;
                        return instances::isInWorld(ecs, e);
                    };
                    const Physics::RaycastHit blocked = physics->raycast(camera.position, toPart / length, length, accept);
                    if (blocked.hit) continue;
                }
            }
            best = prompt;
            bestPosition = position;
            bestDistance = distance;
        }
    }
    if (best != st.shown) {
        endHold(ecs, st, player);
        if (st.shown != kNoInstance && instances::isAlive(ecs, st.shown)) {
            if (SignalHub* hub = signals::findHub(ecs)) {
                hub->fire(st.shown, "PromptHidden", {SignalArg::of(InstanceValue::ofInstance(player))});
            }
        }
        st.shown = best;
        if (best != kNoInstance) {
            if (SignalHub* hub = signals::findHub(ecs)) {
                hub->fire(best, "PromptShown", {SignalArg::of(InstanceValue::ofInstance(player))});
            }
        }
    }
    if (st.shown == kNoInstance) {
        st.info = ShownPrompt{};
        st.keyWasDown = input::isKeyDown(ecs, "E");
        return;
    }
    const std::string key = get(ecs, st.shown, "KeyboardKeyCode").text;
    const double duration = get(ecs, st.shown, "HoldDuration").number;
    const bool down = input::isKeyDown(ecs, key) && !input::typing(ecs);
    if (down && !st.keyWasDown) {
        st.holding = true;
        st.triggered = false;
        st.held = 0.0f;
        emit(ecs, st.shown, "PromptButtonHoldBegan", player);
        if (duration <= 0.0) {
            st.triggered = true;
            emit(ecs, st.shown, "Triggered", player);
        }
    } else if (down && st.holding && !st.triggered) {
        st.held += pointer.dt;
        if (st.held >= duration) {
            st.triggered = true;
            emit(ecs, st.shown, "Triggered", player);
        }
    } else if (!down && st.keyWasDown) {
        endHold(ecs, st, player);
    }
    st.keyWasDown = down;
    st.info.ref = st.shown;
    st.info.position = bestPosition != glm::vec3(0.0f) ? bestPosition : glm::vec3(get(ecs, holderPart(ecs, st.shown), "Position").vec);
    st.info.key = key;
    st.info.action = get(ecs, st.shown, "ActionText").text;
    st.info.object = get(ecs, st.shown, "ObjectText").text;
    st.info.hold = duration > 0.0 && st.holding ? std::min(1.0f, st.held / static_cast<float>(duration)) : 0.0f;
}

} // namespace

void update(ECS& ecs, const Pointer& pointer) {
    State& state = ecs.raw().ctx().emplace<State>();
    const InstanceRef player = players::localPlayer(ecs);
    Physics* physics = partbodies::physicsOf(ecs);
    glm::vec3 rootPosition(0.0f);
    bool haveRoot = false;
    if (player != kNoInstance) {
        const InstanceValue character = get(ecs, player, "Character");
        if (character.ref != kNoInstance && instances::isAlive(ecs, character.ref)) {
            for (InstanceRef child : instances::children(ecs, character.ref)) {
                if (instances::name(ecs, child) == "HumanoidRootPart") {
                    rootPosition = glm::vec3(get(ecs, child, "Position").vec);
                    haveRoot = true;
                }
            }
        }
    }
    updatePrompts(ecs, state, pointer, player, haveRoot, rootPosition, physics);

    InstanceRef hit = kNoInstance;
    if (haveRoot && physics != nullptr && pointer.mouse.x >= 0.0f && !pointer.overGui && pointer.view.x > 0.0f &&
        pointer.view.y > 0.0f) {
        const Camera& camera = instances::scriptCamera(ecs);
        glm::vec3 origin;
        glm::vec3 direction;
        camera.screenPointToRay(2.0f * pointer.mouse.x / pointer.view.x - 1.0f,
                                2.0f * pointer.mouse.y / pointer.view.y - 1.0f, pointer.view.x / pointer.view.y, origin,
                                direction);
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

    if (state.hovered != hit) {
        if (state.hovered != kNoInstance && instances::isAlive(ecs, state.hovered)) emit(ecs, state.hovered, "MouseHoverLeave", player);
        if (hit != kNoInstance) emit(ecs, hit, "MouseHoverEnter", player);
        state.hovered = hit;
    }
    if (hit != kNoInstance && pointer.left) emit(ecs, hit, "MouseClick", player);
    if (hit != kNoInstance && pointer.right) emit(ecs, hit, "RightMouseClick", player);
}

const ShownPrompt* shownPrompt(ECS& ecs) {
    const State& state = ecs.raw().ctx().emplace<State>();
    return state.shown != kNoInstance && state.info.ref == state.shown ? &state.info : nullptr;
}

} // namespace engine::core::interact
