#include "core/RobloxInput.hpp"

#include <algorithm>
#include <set>
#include <string>

#include "core/InstanceSignals.hpp"
#include "core/InstanceTree.hpp"
#include "core/RobloxDatatypes.hpp"

namespace engine::core::input {

namespace {

struct State {
    std::set<std::string> keys;
    std::set<int> buttons;
    glm::vec2 mouse{0.0f};
    glm::vec2 delta{0.0f};
    bool typing = false;
    // InputObjects outlive the frame that fired them: handlers run deferred.
    std::vector<InstanceRef> fresh;
    std::vector<InstanceRef> stale;
};

State& stateOf(ECS& ecs) { return ecs.raw().ctx().emplace<State>(); }

int enumValue(const std::string& type, const std::string& item) {
    for (const auto& [name, value] : robloxEnumItems(type)) {
        if (name == item) return value;
    }
    return 0;
}

const char* buttonName(int button) {
    switch (button) {
        case 2: return "MouseButton2";
        case 3: return "MouseButton3";
        default: return "MouseButton1";
    }
}

struct Object {
    std::string key;       // KeyCode item
    std::string type;      // UserInputType item
    std::string state;     // UserInputState item
    glm::vec3 position{0.0f};
    glm::vec3 delta{0.0f};
};

void set(ECS& ecs, InstanceRef ref, const char* name, const InstanceValue& value) {
    if (const PropertyDef* def = instances::findProperty("InputObject", name)) instances::setProperty(ecs, ref, *def, value);
}

void fire(ECS& ecs, State& s, InstanceRef service, const char* event, const Object& object, bool processed) {
    SignalHub* hub = signals::findHub(ecs);
    if (hub == nullptr || !hub->hasConnections(service, event)) return;
    const InstanceRef ref = instances::createUnchecked(ecs, "InputObject");
    if (ref == kNoInstance) return;
    set(ecs, ref, "KeyCode", InstanceValue::ofEnum("KeyCode", object.key, enumValue("KeyCode", object.key)));
    set(ecs, ref, "UserInputType", InstanceValue::ofEnum("UserInputType", object.type, enumValue("UserInputType", object.type)));
    set(ecs, ref, "UserInputState", InstanceValue::ofEnum("UserInputState", object.state, enumValue("UserInputState", object.state)));
    set(ecs, ref, "Position", InstanceValue::ofVector3(object.position));
    set(ecs, ref, "Delta", InstanceValue::ofVector3(object.delta));
    s.fresh.push_back(ref);
    hub->fire(service, event, {SignalArg::of(InstanceValue::ofInstance(ref)), SignalArg::of(InstanceValue::ofBool(processed))});
}

} // namespace

void feed(ECS& ecs, const Frame& frame) {
    State& s = stateOf(ecs);
    // Objects from two frames ago have had their handlers run.
    for (InstanceRef ref : s.stale) {
        if (instances::isAlive(ecs, ref)) instances::destroy(ecs, ref);
    }
    s.stale = std::move(s.fresh);
    s.fresh.clear();

    std::string error;
    const InstanceRef service = instances::getService(ecs, "UserInputService", error);
    const glm::vec3 where(frame.mouse, 0.0f);
    const bool moved = frame.delta.x != 0.0f || frame.delta.y != 0.0f;
    s.mouse = frame.mouse;
    s.delta = frame.delta;
    s.typing = frame.typing;

    for (const KeyEvent& key : frame.keys) {
        const bool had = s.keys.count(key.key) != 0;
        if (key.down == had) continue;
        if (key.down) s.keys.insert(key.key); else s.keys.erase(key.key);
        if (service == kNoInstance) continue;
        Object o{key.key, "Keyboard", key.down ? "Begin" : "End", where, glm::vec3(0.0f)};
        fire(ecs, s, service, key.down ? "InputBegan" : "InputEnded", o, frame.typing);
    }
    for (const ButtonEvent& button : frame.buttons) {
        const bool had = s.buttons.count(button.button) != 0;
        if (button.down == had) continue;
        if (button.down) s.buttons.insert(button.button); else s.buttons.erase(button.button);
        if (service == kNoInstance) continue;
        Object o{"Unknown", buttonName(button.button), button.down ? "Begin" : "End", where, glm::vec3(0.0f)};
        fire(ecs, s, service, button.down ? "InputBegan" : "InputEnded", o, frame.overGui);
    }
    if (service != kNoInstance && moved) {
        Object o{"Unknown", "MouseMovement", "Change", where, glm::vec3(frame.delta, 0.0f)};
        fire(ecs, s, service, "InputChanged", o, frame.overGui);
    }
    if (service != kNoInstance && frame.wheel != 0.0f) {
        Object o{"Unknown", "MouseWheel", "Change", glm::vec3(0.0f, 0.0f, frame.wheel), glm::vec3(0.0f)};
        o.position = glm::vec3(0.0f, 0.0f, frame.wheel);
        fire(ecs, s, service, "InputChanged", o, frame.overGui);
    }
}

bool isKeyDown(ECS& ecs, const std::string& key) { return stateOf(ecs).keys.count(key) != 0; }
bool isMouseButtonDown(ECS& ecs, int button) { return stateOf(ecs).buttons.count(button) != 0; }
glm::vec2 mouseLocation(ECS& ecs) { return stateOf(ecs).mouse; }
glm::vec2 mouseDelta(ECS& ecs) { return stateOf(ecs).delta; }
bool typing(ECS& ecs) { return stateOf(ecs).typing; }
std::vector<std::string> keysDown(ECS& ecs) {
    const State& s = stateOf(ecs);
    return {s.keys.begin(), s.keys.end()};
}

} // namespace engine::core::input
