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
    enum class Type : uint8_t { Nil, Bool, Number, String, Vector3, CFrame, Color3, BrickColor, Enum, Instance,
                                Vector2, UDim, UDim2 };
    Type type = Type::Nil;
    bool boolean = false;
    double number = 0.0; // Number; Enum value
    std::string text;    // String; Enum item name
    std::string enumType;
    // Vector3, Color3, BrickColor, CFrame position; Vector2 (x, y); UDim (scale, offset);
    // UDim2 (X.Scale, X.Offset, Y.Scale) with Y.Offset in `number`.
    glm::vec3 vec{0.0f};
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
    static InstanceValue ofVector2(float x, float y);
    static InstanceValue ofUDim(float scale, float offset);
    static InstanceValue ofUDim2(float xScale, float xOffset, float yScale, float yOffset);

    [[nodiscard]] static const char* typeName(Type type);
};

// Identifies an instance: an entity id, or one of the two that have no entity.
using InstanceRef = uint32_t;
inline constexpr InstanceRef kNoInstance = ~0u;
inline constexpr InstanceRef kGameInstance = ~0u - 1;
inline constexpr InstanceRef kWorkspaceInstance = ~0u - 2;
inline constexpr InstanceRef kRunServiceInstance = ~0u - 3;

// Roblox-side data for an entity. Entities without it get a class from their
// components (see className()).
struct InstanceInfo {
    std::string className;
    bool detached = false; // Parent is nil
    std::map<std::string, InstanceValue> properties; // stored properties (Anchored, Value, ...)
    std::map<std::string, InstanceValue> attributes;
    std::vector<std::string> tags; // CollectionService tags, in the order added

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

enum class PropertyType : uint8_t { Bool, Number, String, Vector3, CFrame, Color3, BrickColor, Enum, Instance,
                                    Vector2, UDim, UDim2 };

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
    std::vector<std::string> methods;
    std::vector<std::string> events;
};

namespace instances {

[[nodiscard]] const ClassDef* findClass(const std::string& name);
[[nodiscard]] const std::vector<ClassDef>& allClasses();
[[nodiscard]] bool classIsA(const std::string& className, const std::string& base);
// Searches the class and its superclasses.
[[nodiscard]] const PropertyDef* findProperty(const std::string& className, const std::string& property);
[[nodiscard]] bool classHasMethod(const std::string& className, const std::string& method);
[[nodiscard]] bool classHasEvent(const std::string& className, const std::string& event);
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
// The same for engine code, including classes scripts can't make (Player).
InstanceRef createUnchecked(ECS& ecs, const std::string& className);
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

// CollectionService tags. add/remove return false when nothing changed.
bool addTag(ECS& ecs, InstanceRef ref, const std::string& tag);
bool removeTag(ECS& ecs, InstanceRef ref, const std::string& tag);
[[nodiscard]] bool hasTag(ECS& ecs, InstanceRef ref, const std::string& tag);
[[nodiscard]] std::vector<std::string> tags(ECS& ecs, InstanceRef ref);
// Tagged instances inside the game (not detached), in creation order.
[[nodiscard]] std::vector<InstanceRef> tagged(ECS& ecs, const std::string& tag);

// Parts outside the workspace (in ReplicatedStorage, or with Parent nil)
// don't draw. Re-checks the entity and its descendants.
void updateWorldPresence(ECS& ecs, EntityId entity);
// True when the entity or an ancestor has Parent = nil. Such instances are
// left out of Studio's lists and scene files, like in Roblox.
[[nodiscard]] bool isDetached(ECS& ecs, EntityId entity);
// False for parts kept outside the workspace; those have no physics body.
[[nodiscard]] bool isInWorld(ECS& ecs, EntityId entity);

// Roblox parts are solid: a collider matching Size and Shape, Static when
// Anchored, else Dynamic. Play and scene loading build the body from it.
void fitPartCollider(ECS& ecs, EntityId entity);
// CanCollide = false parts become sensors: nothing bumps into them, but
// Touched still fires.
[[nodiscard]] bool canCollide(ECS& ecs, EntityId entity);

// World-space pose of an entity, composed along its parents.
struct Pose {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};
[[nodiscard]] Pose worldPose(ECS& ecs, EntityId entity);
void setWorldPose(ECS& ecs, EntityId entity, glm::vec3 position, glm::quat rotation);

// Asks Physics::step to move the live body (and those of its descendants)
// to where the Transform now says, like setting CFrame in Roblox.
struct PhysicsPoseWrite {
    bool resetVelocity = false;
};
void markBodyMoved(ECS& ecs, EntityId entity, bool resetVelocity = false);
// Moves a part and its body, e.g. a respawning character.
void teleport(ECS& ecs, EntityId entity, glm::vec3 position, glm::quat rotation, bool resetVelocity);

// The two parts an enabled WeldConstraint in the workspace joins; false if it joins nothing.
bool weldParts(ECS& ecs, EntityId weld, EntityId& part0, EntityId& part1);
// Every part joined to `part` through welds, `part` first.
std::vector<EntityId> weldedAssembly(ECS& ecs, EntityId part);
// Parts moved alone (Position/Orientation) since the last weld rebuild; their welds keep the new offset.
struct WeldOffsetsChanged {
    std::vector<EntityId> parts;
};

} // namespace instances
} // namespace engine::core
