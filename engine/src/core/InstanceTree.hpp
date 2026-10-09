#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/ECS.hpp"

// Roblox's Instance tree (game, workspace, Parent, children, properties) as a
// view over the ECS. Scripts reach it through core/ScriptInstanceApi.hpp.
namespace engine::core {

// A property or attribute value. BrickColor is kept as its colour.
struct InstanceValue {
    enum class Type : uint8_t { Nil, Bool, Number, String, Vector3, CFrame, Color3, BrickColor, Enum, Instance };
    Type type = Type::Nil;
    bool boolean = false;
    double number = 0.0; // Number; Enum value
    std::string text;    // String; Enum item name
    std::string enumType;
    glm::vec3 vec{0.0f}; // Vector3, Color3, BrickColor, CFrame position
    glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
    uint32_t ref = ~0u; // Instance

    static InstanceValue ofBool(bool v);
    static InstanceValue ofNumber(double v);
    static InstanceValue ofString(std::string v);
    static InstanceValue ofVector3(glm::vec3 v);
    static InstanceValue ofCFrame(glm::vec3 position, glm::quat rotation);
    static InstanceValue ofColor3(glm::vec3 rgb);
    static InstanceValue ofBrickColor(glm::vec3 rgb);
    static InstanceValue ofEnum(std::string enumType, std::string item, int value);
    static InstanceValue ofInstance(uint32_t ref);

    [[nodiscard]] static const char* typeName(Type type);
};

// Identifies an instance: an entity id, or one of the two that have no entity.
using InstanceRef = uint32_t;
inline constexpr InstanceRef kNoInstance = ~0u;
inline constexpr InstanceRef kGameInstance = ~0u - 1;
inline constexpr InstanceRef kWorkspaceInstance = ~0u - 2;

// Roblox-side data for an entity. Entities without it get a class from their
// components (see className()).
struct InstanceInfo {
    std::string className;
    bool detached = false; // Parent is nil
    std::map<std::string, InstanceValue> properties; // stored properties (Anchored, Value, ...)
    std::map<std::string, InstanceValue> attributes;

    // One-line text form for scene files.
    [[nodiscard]] std::string serialize() const;
    static bool deserialize(const std::string& text, InstanceInfo& out);
};

// 1-unit meshes for parts made by Instance.new, kept in the registry context.
struct InstanceMeshes {
    uint32_t box = ~0u;
    uint32_t ball = ~0u;
    uint32_t cylinder = ~0u;
};

enum class PropertyType : uint8_t { Bool, Number, String, Vector3, CFrame, Color3, BrickColor, Enum, Instance };

// Which side of a game may see or change a property.
enum class PropertyContext : uint8_t { Any, ServerOnly, ClientOnly };

struct PropertyDef {
    std::string name;
    PropertyType type = PropertyType::Number;
    std::string enumType;
    bool readOnly = false;
    bool replicated = true;    // sent from server to clients
    bool serialized = true;    // written to scene files (false when derived from another property)
    bool studioVisible = true; // shown in Studio's Inspector
    PropertyContext context = PropertyContext::Any;
    InstanceValue defaultValue; // for stored properties
    // Null for stored properties (kept in InstanceInfo::properties).
    InstanceValue (*get)(ECS&, EntityId) = nullptr;
    void (*set)(ECS&, EntityId, const InstanceValue&) = nullptr;
};

struct ClassDef {
    std::string name;
    std::string superclass;
    bool creatable = false; // Instance.new may make it
    bool service = false;   // lives directly under game
    std::vector<PropertyDef> properties;
};

namespace instances {

[[nodiscard]] const ClassDef* findClass(const std::string& name);
[[nodiscard]] const std::vector<ClassDef>& allClasses();
[[nodiscard]] bool classIsA(const std::string& className, const std::string& base);
// Searches the class and its superclasses.
[[nodiscard]] const PropertyDef* findProperty(const std::string& className, const std::string& property);
// Behaviour-only services (RunService, TweenService, ...) the bridge adds later.
[[nodiscard]] bool isPlannedService(const std::string& name);

[[nodiscard]] InstanceRef refOf(ECS& ecs, EntityId entity);
[[nodiscard]] EntityId entityOf(ECS& ecs, InstanceRef ref);
[[nodiscard]] bool isAlive(ECS& ecs, InstanceRef ref);
[[nodiscard]] std::string className(ECS& ecs, InstanceRef ref);
[[nodiscard]] std::string name(ECS& ecs, InstanceRef ref);
void setName(ECS& ecs, InstanceRef ref, const std::string& name);
[[nodiscard]] std::string fullName(ECS& ecs, InstanceRef ref);
[[nodiscard]] InstanceRef parent(ECS& ecs, InstanceRef ref);
[[nodiscard]] std::vector<InstanceRef> children(ECS& ecs, InstanceRef ref);
[[nodiscard]] std::vector<InstanceRef> descendants(ECS& ecs, InstanceRef ref);
[[nodiscard]] bool isDescendantOf(ECS& ecs, InstanceRef ref, InstanceRef ancestor);
// parent may be kNoInstance (Parent = nil). Fails with a Roblox-style message.
bool setParent(ECS& ecs, InstanceRef child, InstanceRef parent, std::string& error);

// Creates a detached instance (Parent nil). kNoInstance if not creatable.
InstanceRef create(ECS& ecs, const std::string& className, std::string& error);
// Copies the instance and its descendants; the copy is detached.
InstanceRef clone(ECS& ecs, InstanceRef ref);
void destroy(ECS& ecs, InstanceRef ref);
// Finds or makes the service; kNoInstance and an error if it isn't one.
InstanceRef getService(ECS& ecs, const std::string& name, std::string& error);
// Only finds it.
[[nodiscard]] InstanceRef findService(ECS& ecs, const std::string& name);

bool getProperty(ECS& ecs, InstanceRef ref, const PropertyDef& property, InstanceValue& out);
void setProperty(ECS& ecs, InstanceRef ref, const PropertyDef& property, const InstanceValue& value);

[[nodiscard]] const InstanceValue* attribute(ECS& ecs, InstanceRef ref, const std::string& name);
void setAttribute(ECS& ecs, InstanceRef ref, const std::string& name, const InstanceValue& value);
[[nodiscard]] std::map<std::string, InstanceValue> attributes(ECS& ecs, InstanceRef ref);

// Parts outside the workspace (in ReplicatedStorage, or with Parent nil)
// don't draw. Re-checks the entity and its descendants.
void updateWorldPresence(ECS& ecs, EntityId entity);
// True when the entity or an ancestor has Parent = nil. Such instances are
// left out of Studio's lists and scene files, like in Roblox.
[[nodiscard]] bool isDetached(ECS& ecs, EntityId entity);

// World-space pose of an entity, composed along its parents.
struct Pose {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};
[[nodiscard]] Pose worldPose(ECS& ecs, EntityId entity);
void setWorldPose(ECS& ecs, EntityId entity, glm::vec3 position, glm::quat rotation);

} // namespace instances
} // namespace engine::core
