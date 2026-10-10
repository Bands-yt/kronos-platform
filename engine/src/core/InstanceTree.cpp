#include "core/InstanceTree.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <unordered_map>

#include "core/Components.hpp"
#include "core/Hierarchy.hpp"
#include "core/InstanceSignals.hpp"
#include "core/PhysicsMaterial.hpp"
#include "core/RobloxPlayers.hpp"

namespace engine::core {

InstanceValue InstanceValue::ofBool(bool v) {
    InstanceValue out;
    out.type = Type::Bool;
    out.boolean = v;
    return out;
}
InstanceValue InstanceValue::ofNumber(double v) {
    InstanceValue out;
    out.type = Type::Number;
    out.number = v;
    return out;
}
InstanceValue InstanceValue::ofString(std::string v) {
    InstanceValue out;
    out.type = Type::String;
    out.text = std::move(v);
    return out;
}
InstanceValue InstanceValue::ofVector3(glm::vec3 v) {
    InstanceValue out;
    out.type = Type::Vector3;
    out.vec = v;
    return out;
}
InstanceValue InstanceValue::ofCFrame(glm::vec3 position, glm::quat rotation) {
    InstanceValue out;
    out.type = Type::CFrame;
    out.vec = position;
    out.rot = rotation;
    return out;
}
InstanceValue InstanceValue::ofColor3(glm::vec3 rgb) {
    InstanceValue out;
    out.type = Type::Color3;
    out.vec = rgb;
    return out;
}
InstanceValue InstanceValue::ofBrickColor(glm::vec3 rgb) {
    InstanceValue out = ofColor3(rgb);
    out.type = Type::BrickColor;
    return out;
}
InstanceValue InstanceValue::ofEnum(std::string enumType, std::string item, int value) {
    InstanceValue out;
    out.type = Type::Enum;
    out.enumType = std::move(enumType);
    out.text = std::move(item);
    out.number = value;
    return out;
}
InstanceValue InstanceValue::ofInstance(uint32_t ref) {
    InstanceValue out;
    out.type = ref == kNoInstance ? Type::Nil : Type::Instance;
    out.ref = ref;
    return out;
}

InstanceValue InstanceValue::ofVector2(float x, float y) {
    InstanceValue out;
    out.type = Type::Vector2;
    out.vec = glm::vec3(x, y, 0.0f);
    return out;
}

InstanceValue InstanceValue::ofUDim(float scale, float offset) {
    InstanceValue out;
    out.type = Type::UDim;
    out.vec = glm::vec3(scale, offset, 0.0f);
    return out;
}

InstanceValue InstanceValue::ofUDim2(float xScale, float xOffset, float yScale, float yOffset) {
    InstanceValue out;
    out.type = Type::UDim2;
    out.vec = glm::vec3(xScale, xOffset, yScale);
    out.number = yOffset;
    return out;
}

const char* InstanceValue::typeName(Type type) {
    switch (type) {
        case Type::Nil: return "nil";
        case Type::Bool: return "boolean";
        case Type::Number: return "number";
        case Type::String: return "string";
        case Type::Vector3: return "Vector3";
        case Type::CFrame: return "CFrame";
        case Type::Color3: return "Color3";
        case Type::BrickColor: return "BrickColor";
        case Type::Enum: return "EnumItem";
        case Type::Instance: return "Instance";
        case Type::Vector2: return "Vector2";
        case Type::UDim: return "UDim";
        case Type::UDim2: return "UDim2";
    }
    return "nil";
}

// --- scene-file text form ---------------------------------------------------
// className|detached|count|kind name type fields...  Strings are %-escaped so
// the whole thing stays on one line with no spaces inside fields.
namespace {

std::string escape(const std::string& s) {
    std::string out;
    for (const char c : s) {
        if (c == '%' || c == ' ' || c == '|' || c == '\n' || c == '\r' || c == '\t') {
            char buffer[4];
            std::snprintf(buffer, sizeof(buffer), "%%%02X", static_cast<unsigned char>(c));
            out += buffer;
        } else {
            out += c;
        }
    }
    return out.empty() ? "%00" : out;
}

std::string unescape(const std::string& s) {
    if (s == "%00") return {};
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            out += static_cast<char>(std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

void writeValue(std::ostringstream& out, const InstanceValue& v) {
    out << static_cast<int>(v.type);
    switch (v.type) {
        case InstanceValue::Type::Nil: break;
        case InstanceValue::Type::Bool: out << ' ' << (v.boolean ? 1 : 0); break;
        case InstanceValue::Type::Number: out << ' ' << v.number; break;
        case InstanceValue::Type::String: out << ' ' << escape(v.text); break;
        case InstanceValue::Type::Vector3:
        case InstanceValue::Type::Color3:
        case InstanceValue::Type::BrickColor: out << ' ' << v.vec.x << ' ' << v.vec.y << ' ' << v.vec.z; break;
        case InstanceValue::Type::CFrame:
            out << ' ' << v.vec.x << ' ' << v.vec.y << ' ' << v.vec.z << ' ' << v.rot.w << ' ' << v.rot.x << ' '
                << v.rot.y << ' ' << v.rot.z;
            break;
        case InstanceValue::Type::Enum: out << ' ' << escape(v.enumType) << ' ' << escape(v.text) << ' ' << v.number; break;
        // Instance links don't survive a reload (ids are per session).
        case InstanceValue::Type::Instance: break;
        case InstanceValue::Type::Vector2:
        case InstanceValue::Type::UDim: out << ' ' << v.vec.x << ' ' << v.vec.y; break;
        case InstanceValue::Type::UDim2: out << ' ' << v.vec.x << ' ' << v.vec.y << ' ' << v.vec.z << ' ' << v.number; break;
    }
}

bool readValue(std::istringstream& in, InstanceValue& v) {
    int type = 0;
    if (!(in >> type) || type < 0 || type > static_cast<int>(InstanceValue::Type::UDim2)) return false;
    v = InstanceValue{};
    v.type = static_cast<InstanceValue::Type>(type);
    std::string a, b;
    switch (v.type) {
        case InstanceValue::Type::Nil: return true;
        case InstanceValue::Type::Bool: {
            int flag = 0;
            in >> flag;
            v.boolean = flag != 0;
            break;
        }
        case InstanceValue::Type::Number: in >> v.number; break;
        case InstanceValue::Type::String:
            in >> a;
            v.text = unescape(a);
            break;
        case InstanceValue::Type::Vector3:
        case InstanceValue::Type::Color3:
        case InstanceValue::Type::BrickColor: in >> v.vec.x >> v.vec.y >> v.vec.z; break;
        case InstanceValue::Type::CFrame:
            in >> v.vec.x >> v.vec.y >> v.vec.z >> v.rot.w >> v.rot.x >> v.rot.y >> v.rot.z;
            break;
        case InstanceValue::Type::Enum:
            in >> a >> b >> v.number;
            v.enumType = unescape(a);
            v.text = unescape(b);
            break;
        case InstanceValue::Type::Instance: v.type = InstanceValue::Type::Nil; return true;
        case InstanceValue::Type::Vector2:
        case InstanceValue::Type::UDim: in >> v.vec.x >> v.vec.y; break;
        case InstanceValue::Type::UDim2: in >> v.vec.x >> v.vec.y >> v.vec.z >> v.number; break;
    }
    return !in.fail();
}

} // namespace

std::string InstanceInfo::serialize() const {
    std::ostringstream out;
    out << escape(className) << ' ' << (detached ? 1 : 0) << ' ' << properties.size();
    for (const auto& [key, value] : properties) {
        out << ' ' << escape(key) << ' ';
        writeValue(out, value);
    }
    out << ' ' << attributes.size();
    for (const auto& [key, value] : attributes) {
        out << ' ' << escape(key) << ' ';
        writeValue(out, value);
    }
    if (!tags.empty()) {
        out << ' ' << tags.size();
        for (const std::string& tag : tags) out << ' ' << escape(tag);
    }
    return out.str();
}

bool InstanceInfo::deserialize(const std::string& text, InstanceInfo& out) {
    std::istringstream in(text);
    InstanceInfo info;
    std::string word;
    int detached = 0;
    size_t count = 0;
    if (!(in >> word >> detached >> count)) return false;
    info.className = unescape(word);
    info.detached = detached != 0;
    for (size_t i = 0; i < count; ++i) {
        InstanceValue value;
        if (!(in >> word) || !readValue(in, value)) return false;
        info.properties[unescape(word)] = value;
    }
    if (!(in >> count)) return false;
    for (size_t i = 0; i < count; ++i) {
        InstanceValue value;
        if (!(in >> word) || !readValue(in, value)) return false;
        info.attributes[unescape(word)] = value;
    }
    // Tags came later; older lines end here.
    if (in >> count) {
        for (size_t i = 0; i < count && in >> word; ++i) info.tags.push_back(unescape(word));
    }
    out = std::move(info);
    return true;
}

namespace instances {
namespace {

// Game and (when no Workspace entity exists) workspace keep their data here.
struct VirtualInstances {
    InstanceInfo game{"DataModel"};
    InstanceInfo workspace{"Workspace"};
    InstanceInfo runService{"RunService"};
};

constexpr glm::vec3 kDefaultPartColor{163.0f / 255.0f, 162.0f / 255.0f, 165.0f / 255.0f};

bool valid(ECS& ecs, EntityId e) { return e != kNullEntity && ecs.raw().valid(e); }

bool sameValue(const InstanceValue& a, const InstanceValue& b) {
    if (a.type != b.type) return false;
    switch (a.type) {
        case InstanceValue::Type::Nil: return true;
        case InstanceValue::Type::Bool: return a.boolean == b.boolean;
        case InstanceValue::Type::Number: return a.number == b.number;
        case InstanceValue::Type::String: return a.text == b.text;
        case InstanceValue::Type::Enum: return a.enumType == b.enumType && a.text == b.text;
        case InstanceValue::Type::Instance: return a.ref == b.ref;
        case InstanceValue::Type::CFrame: return a.vec == b.vec && a.rot == b.rot;
        case InstanceValue::Type::UDim2: return a.vec == b.vec && a.number == b.number;
        default: return a.vec == b.vec;
    }
}

EntityId parentEntity(ECS& ecs, EntityId e) {
    const auto* h = ecs.tryGetComponent<Hierarchy>(e);
    return h != nullptr && valid(ecs, h->parent) ? h->parent : kNullEntity;
}

bool hidden(ECS& ecs, EntityId e) { return ecs.tryGetComponent<PlayerAvatarPart>(e) != nullptr; }

std::string inferClassName(ECS& ecs, EntityId e) {
    if (ecs.hasComponent<Script>(e)) return "Script";
    if (const auto* light = ecs.tryGetComponent<Light>(e)) return light->type == LightType::Spot ? "SpotLight" : "PointLight";
    if (ecs.hasComponent<AudioSource>(e)) return "Sound";
    if (ecs.hasComponent<Renderable>(e)) {
        const auto* mesh = ecs.tryGetComponent<MeshSource>(e);
        if (mesh != nullptr && (mesh->kind == MeshSourceKind::Obj || mesh->kind == MeshSourceKind::Gltf ||
                                mesh->kind == MeshSourceKind::Fbx)) {
            return "MeshPart";
        }
        return "Part";
    }
    if (parentEntity(ecs, e) == kNullEntity) {
        if (const auto* n = ecs.tryGetComponent<Name>(e)) {
            const ClassDef* def = findClass(n->value);
            if (def != nullptr && def->service) return def->name;
        }
    }
    return "Model";
}

std::string entityClass(ECS& ecs, EntityId e) {
    if (const auto* info = ecs.tryGetComponent<InstanceInfo>(e)) {
        if (!info->className.empty()) return info->className;
    }
    return inferClassName(ecs, e);
}

InstanceInfo& ensureInfo(ECS& ecs, EntityId e) {
    if (auto* info = ecs.tryGetComponent<InstanceInfo>(e)) {
        if (info->className.empty()) info->className = inferClassName(ecs, e);
        return *info;
    }
    InstanceInfo info;
    info.className = inferClassName(ecs, e);
    return ecs.addComponent<InstanceInfo>(e, std::move(info));
}

bool isServiceClass(const std::string& className) {
    const ClassDef* def = findClass(className);
    return def != nullptr && def->service;
}

std::vector<EntityId> rootEntities(ECS& ecs) {
    std::vector<EntityId> roots;
    for (EntityId e : ecs.view<Transform>()) {
        if (parentEntity(ecs, e) == kNullEntity && !hidden(ecs, e)) roots.push_back(e);
    }
    // Creation order, near enough: entity index.
    std::sort(roots.begin(), roots.end(), [](EntityId a, EntityId b) {
        return entt::to_entity(a) < entt::to_entity(b);
    });
    return roots;
}

EntityId workspaceEntity(ECS& ecs) {
    for (EntityId e : rootEntities(ecs)) {
        if (entityClass(ecs, e) == "Workspace") return e;
    }
    return kNullEntity;
}

InstanceInfo& infoFor(ECS& ecs, InstanceRef ref) {
    if (ref == kGameInstance) return ecs.raw().ctx().emplace<VirtualInstances>().game;
    if (ref == kRunServiceInstance) return ecs.raw().ctx().emplace<VirtualInstances>().runService;
    const EntityId e = entityOf(ecs, ref);
    if (valid(ecs, e)) return ensureInfo(ecs, e);
    return ecs.raw().ctx().emplace<VirtualInstances>().workspace;
}

const InstanceInfo* findInfo(ECS& ecs, InstanceRef ref) {
    if (ref == kGameInstance || ref == kRunServiceInstance ||
        (ref == kWorkspaceInstance && !valid(ecs, workspaceEntity(ecs)))) {
        auto* virtuals = ecs.raw().ctx().find<VirtualInstances>();
        if (virtuals == nullptr) return nullptr;
        if (ref == kRunServiceInstance) return &virtuals->runService;
        return ref == kGameInstance ? &virtuals->game : &virtuals->workspace;
    }
    return ecs.tryGetComponent<InstanceInfo>(entityOf(ecs, ref));
}

// Raw link that keeps the child's local transform.
void linkRaw(ECS& ecs, EntityId child, EntityId parent) {
    auto& parentHierarchy = ecs.raw().get_or_emplace<Hierarchy>(parent);
    parentHierarchy.children.push_back(child);
    ecs.raw().get_or_emplace<Hierarchy>(child).parent = parent;
}

// --- pose helpers -----------------------------------------------------------

glm::vec3 sizeFactor(ECS& ecs, EntityId e) {
    const auto* mesh = ecs.tryGetComponent<MeshSource>(e);
    if (mesh == nullptr) return glm::vec3(1.0f);
    switch (mesh->kind) {
        case MeshSourceKind::Box: return mesh->params * 2.0f;
        case MeshSourceKind::Plane: return {mesh->params.x * 2.0f, 1.0f, mesh->params.z * 2.0f};
        case MeshSourceKind::Capsule:
            if (mesh->params.y <= 0.0f) return glm::vec3(mesh->params.x * 2.0f);
            return {mesh->params.x * 2.0f, (mesh->params.y + mesh->params.x) * 2.0f, mesh->params.x * 2.0f};
        default: return glm::vec3(1.0f);
    }
}

glm::vec3 orientationOf(const glm::quat& q) {
    // Roblox Orientation: degrees, applied Y then X then Z.
    const glm::mat3 m = glm::mat3_cast(q);
    const float r12 = m[2][1], r02 = m[2][0], r22 = m[2][2], r10 = m[0][1], r11 = m[1][1];
    const float x = std::asin(std::clamp(-r12, -1.0f, 1.0f));
    const float y = std::atan2(r02, r22);
    const float z = std::atan2(r10, r11);
    return glm::degrees(glm::vec3(x, y, z));
}

glm::quat fromOrientation(glm::vec3 degrees) {
    const glm::vec3 r = glm::radians(degrees);
    return glm::angleAxis(r.y, glm::vec3(0, 1, 0)) * glm::angleAxis(r.x, glm::vec3(1, 0, 0)) *
           glm::angleAxis(r.z, glm::vec3(0, 0, 1));
}

glm::vec3 rotationXYZOf(const glm::quat& q) {
    // Roblox Rotation: degrees, R = Rx * Ry * Rz.
    const glm::mat3 m = glm::mat3_cast(q);
    const float r02 = m[2][0], r12 = m[2][1], r22 = m[2][2], r01 = m[1][0], r00 = m[0][0];
    const float y = std::asin(std::clamp(r02, -1.0f, 1.0f));
    const float x = std::atan2(-r12, r22);
    const float z = std::atan2(-r01, r00);
    return glm::degrees(glm::vec3(x, y, z));
}

glm::quat fromRotationXYZ(glm::vec3 degrees) {
    const glm::vec3 r = glm::radians(degrees);
    return glm::angleAxis(r.x, glm::vec3(1, 0, 0)) * glm::angleAxis(r.y, glm::vec3(0, 1, 0)) *
           glm::angleAxis(r.z, glm::vec3(0, 0, 1));
}

// --- property accessors -----------------------------------------------------

Renderable* renderable(ECS& ecs, EntityId e) { return ecs.tryGetComponent<Renderable>(e); }

InstanceValue getPosition(ECS& ecs, EntityId e) { return InstanceValue::ofVector3(worldPose(ecs, e).position); }
void weldOffsetChanged(ECS& ecs, EntityId e) { ecs.raw().ctx().emplace<WeldOffsetsChanged>().parts.push_back(e); }

bool isDescendantOfAny(ECS& ecs, EntityId e, const std::vector<EntityId>& roots) {
    for (EntityId at = parentEntity(ecs, e); at != kNullEntity; at = parentEntity(ecs, at)) {
        if (std::find(roots.begin(), roots.end(), at) != roots.end()) return true;
    }
    return false;
}

void setPosition(ECS& ecs, EntityId e, const InstanceValue& v) {
    setWorldPose(ecs, e, v.vec, worldPose(ecs, e).rotation);
    markBodyMoved(ecs, e);
    weldOffsetChanged(ecs, e);
}
InstanceValue getCFrame(ECS& ecs, EntityId e) {
    const Pose pose = worldPose(ecs, e);
    return InstanceValue::ofCFrame(pose.position, pose.rotation);
}
// Setting CFrame moves every part welded to this one, like in Roblox.
void setCFrame(ECS& ecs, EntityId e, const InstanceValue& v) {
    const Pose before = worldPose(ecs, e);
    const glm::quat rotation = glm::normalize(v.rot);
    const glm::quat turn = rotation * glm::inverse(before.rotation);
    const std::vector<EntityId> assembly = weldedAssembly(ecs, e);
    setWorldPose(ecs, e, v.vec, rotation);
    markBodyMoved(ecs, e);
    std::vector<EntityId> moved{e};
    for (size_t i = 1; i < assembly.size(); ++i) {
        const EntityId other = assembly[i];
        if (isDescendantOfAny(ecs, other, moved)) continue;
        const Pose pose = worldPose(ecs, other);
        setWorldPose(ecs, other, v.vec + turn * (pose.position - before.position), glm::normalize(turn * pose.rotation));
        markBodyMoved(ecs, other);
        moved.push_back(other);
    }
}
InstanceValue getOrientation(ECS& ecs, EntityId e) {
    return InstanceValue::ofVector3(orientationOf(worldPose(ecs, e).rotation));
}
void setOrientation(ECS& ecs, EntityId e, const InstanceValue& v) {
    setWorldPose(ecs, e, worldPose(ecs, e).position, fromOrientation(v.vec));
    markBodyMoved(ecs, e);
    weldOffsetChanged(ecs, e);
}
InstanceValue getRotation(ECS& ecs, EntityId e) {
    return InstanceValue::ofVector3(rotationXYZOf(worldPose(ecs, e).rotation));
}
void setRotation(ECS& ecs, EntityId e, const InstanceValue& v) {
    setWorldPose(ecs, e, worldPose(ecs, e).position, fromRotationXYZ(v.vec));
    markBodyMoved(ecs, e);
    weldOffsetChanged(ecs, e);
}
InstanceValue getSize(ECS& ecs, EntityId e) {
    return InstanceValue::ofVector3(glm::abs(worldPose(ecs, e).scale * sizeFactor(ecs, e)));
}
void setSize(ECS& ecs, EntityId e, const InstanceValue& v) {
    auto* transform = ecs.tryGetComponent<Transform>(e);
    if (transform == nullptr) return;
    // In Roblox a part's Size never moves or resizes what's inside it.
    std::vector<std::pair<EntityId, Pose>> kids;
    if (const auto* h = ecs.tryGetComponent<Hierarchy>(e)) {
        for (EntityId child : h->children) {
            if (valid(ecs, child)) kids.emplace_back(child, worldPose(ecs, child));
        }
    }
    const glm::vec3 size = glm::max(v.vec, glm::vec3(0.001f));
    const EntityId p = parentEntity(ecs, e);
    const glm::vec3 parentScale = p != kNullEntity ? worldPose(ecs, p).scale : glm::vec3(1.0f);
    transform->scale = size / (sizeFactor(ecs, e) * glm::max(glm::abs(parentScale), glm::vec3(1e-6f)));
    const glm::vec3 newScale = glm::max(glm::abs(worldPose(ecs, e).scale), glm::vec3(1e-6f));
    for (const auto& [child, pose] : kids) {
        if (auto* t = ecs.tryGetComponent<Transform>(child)) t->scale = pose.scale / newScale;
        setWorldPose(ecs, child, pose.position, pose.rotation);
    }
    if (ecs.tryGetComponent<ColliderShape>(e) != nullptr) fitPartCollider(ecs, e);
}
InstanceValue getColor(ECS& ecs, EntityId e) {
    const auto* r = renderable(ecs, e);
    return InstanceValue::ofColor3(r != nullptr ? glm::vec3(r->baseColor) : kDefaultPartColor);
}
void setColor(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* r = renderable(ecs, e)) {
        r->baseColor = glm::vec4(glm::clamp(v.vec, glm::vec3(0.0f), glm::vec3(1.0f)), r->baseColor.a);
        if (r->emissiveIntensity > 0.0f && ecs.tryGetComponent<InstanceInfo>(e) != nullptr) {
            const auto& props = ecs.tryGetComponent<InstanceInfo>(e)->properties;
            const auto material = props.find("Material");
            if (material != props.end() && material->second.text == "Neon") r->emissiveColor = glm::vec3(r->baseColor);
        }
    }
}
InstanceValue getBrickColor(ECS& ecs, EntityId e) {
    InstanceValue v = getColor(ecs, e);
    v.type = InstanceValue::Type::BrickColor;
    return v;
}
InstanceValue getTransparency(ECS& ecs, EntityId e) {
    const auto* r = renderable(ecs, e);
    return InstanceValue::ofNumber(r != nullptr ? 1.0 - r->baseColor.a : 0.0);
}
void setTransparency(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* r = renderable(ecs, e)) {
        r->baseColor.a = 1.0f - std::clamp(static_cast<float>(v.number), 0.0f, 1.0f);
        updateWorldPresence(ecs, e);
    }
}
void setReflectance(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* r = renderable(ecs, e)) {
        const float reflectance = std::clamp(static_cast<float>(v.number), 0.0f, 1.0f);
        r->roughness = std::clamp(0.85f - reflectance * 0.7f, 0.05f, 1.0f);
        r->metallic = std::clamp(reflectance * 0.6f, 0.0f, 1.0f);
    }
}
InstanceValue getCastShadow(ECS& ecs, EntityId e) {
    const auto* r = renderable(ecs, e);
    return InstanceValue::ofBool(r == nullptr || r->castsShadow);
}
void setCastShadow(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* r = renderable(ecs, e)) r->castsShadow = v.boolean;
}
InstanceValue getAnchored(ECS& ecs, EntityId e) {
    if (const auto* info = ecs.tryGetComponent<InstanceInfo>(e)) {
        const auto it = info->properties.find("Anchored");
        if (it != info->properties.end()) return it->second;
    }
    const auto* body = ecs.tryGetComponent<RigidBody>(e);
    return InstanceValue::ofBool(body == nullptr || body->motionType != RigidBodyMotionType::Dynamic);
}
void setAnchored(ECS& ecs, EntityId e, const InstanceValue& v) {
    ensureInfo(ecs, e).properties["Anchored"] = v;
    // Takes effect when Play builds the bodies; a live body keeps its type.
    if (auto* body = ecs.tryGetComponent<RigidBody>(e)) {
        if (body->joltBodyId == RigidBody::kInvalidBodyId) {
            body->motionType = v.boolean ? RigidBodyMotionType::Static : RigidBodyMotionType::Dynamic;
        }
    }
}
void setMaterial(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* r = renderable(ecs, e)) {
        if (v.text == "Neon") {
            r->emissiveColor = glm::vec3(r->baseColor);
            r->emissiveIntensity = 1.5f;
        } else {
            r->emissiveIntensity = 0.0f;
        }
    }
}
void setShape(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (ecs.tryGetComponent<ColliderShape>(e) != nullptr) {
        ensureInfo(ecs, e).properties["Shape"] = v;
        fitPartCollider(ecs, e);
    }
    const auto* meshes = ecs.raw().ctx().find<InstanceMeshes>();
    auto* r = renderable(ecs, e);
    if (meshes == nullptr || r == nullptr) return;
    const uint32_t handle = v.text == "Ball" ? meshes->ball : v.text == "Cylinder" ? meshes->cylinder : meshes->box;
    if (handle != ~0u) r->meshHandle = handle;
}

Light* light(ECS& ecs, EntityId e) { return ecs.tryGetComponent<Light>(e); }
InstanceValue getBrightness(ECS& ecs, EntityId e) {
    const auto* l = light(ecs, e);
    return InstanceValue::ofNumber(l != nullptr ? l->intensity : 1.0);
}
void setBrightness(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* l = light(ecs, e)) l->intensity = std::max(0.0f, static_cast<float>(v.number));
}
InstanceValue getLightColor(ECS& ecs, EntityId e) {
    const auto* l = light(ecs, e);
    return InstanceValue::ofColor3(l != nullptr ? l->color : glm::vec3(1.0f));
}
void setLightColor(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* l = light(ecs, e)) l->color = v.vec;
}
InstanceValue getRange(ECS& ecs, EntityId e) {
    const auto* l = light(ecs, e);
    return InstanceValue::ofNumber(l != nullptr ? l->radius : 8.0);
}
void setRange(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* l = light(ecs, e)) l->radius = std::max(0.01f, static_cast<float>(v.number));
}
InstanceValue getLightEnabled(ECS& ecs, EntityId e) {
    const auto* l = light(ecs, e);
    return InstanceValue::ofBool(l == nullptr || l->enabled);
}
void setLightEnabled(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* l = light(ecs, e)) l->enabled = v.boolean;
}
// Script.Enabled is the newer name for `not Disabled`.
InstanceValue getScriptEnabled(ECS& ecs, EntityId e) {
    const auto* info = ecs.tryGetComponent<InstanceInfo>(e);
    if (info == nullptr) return InstanceValue::ofBool(true);
    const auto it = info->properties.find("Disabled");
    return InstanceValue::ofBool(it == info->properties.end() || !it->second.boolean);
}
void setScriptEnabled(ECS& ecs, EntityId e, const InstanceValue& v) {
    ecs.raw().get_or_emplace<InstanceInfo>(e).properties["Disabled"] = InstanceValue::ofBool(!v.boolean);
}
InstanceValue getShadows(ECS& ecs, EntityId e) {
    const auto* l = light(ecs, e);
    return InstanceValue::ofBool(l != nullptr && l->castsShadow);
}
void setShadows(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* l = light(ecs, e)) l->castsShadow = v.boolean;
}
InstanceValue getAngle(ECS& ecs, EntityId e) {
    const auto* l = light(ecs, e);
    return InstanceValue::ofNumber(l != nullptr ? l->outerConeDegrees * 2.0 : 90.0);
}
void setAngle(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* l = light(ecs, e)) {
        l->outerConeDegrees = std::clamp(static_cast<float>(v.number) * 0.5f, 0.0f, 90.0f);
        l->innerConeDegrees = l->outerConeDegrees * 0.8f;
    }
}
void roundIntValue(ECS& ecs, EntityId e, const InstanceValue& v) {
    ensureInfo(ecs, e).properties["Value"] = InstanceValue::ofNumber(std::round(v.number));
}

AudioSource* sound(ECS& ecs, EntityId e) { return ecs.tryGetComponent<AudioSource>(e); }
InstanceValue getSoundId(ECS& ecs, EntityId e) {
    const auto* s = sound(ecs, e);
    return InstanceValue::ofString(s != nullptr ? s->path : "");
}
void setSoundId(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* s = sound(ecs, e)) s->path = v.text;
}
InstanceValue getVolume(ECS& ecs, EntityId e) {
    const auto* s = sound(ecs, e);
    return InstanceValue::ofNumber(s != nullptr ? s->volume : 0.5);
}
void setVolume(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* s = sound(ecs, e)) s->volume = std::clamp(static_cast<float>(v.number), 0.0f, 10.0f);
}
InstanceValue getPlaybackSpeed(ECS& ecs, EntityId e) {
    const auto* s = sound(ecs, e);
    return InstanceValue::ofNumber(s != nullptr ? s->pitch : 1.0);
}
void setPlaybackSpeed(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* s = sound(ecs, e)) s->pitch = std::max(0.0f, static_cast<float>(v.number));
}
InstanceValue getLooped(ECS& ecs, EntityId e) {
    const auto* s = sound(ecs, e);
    return InstanceValue::ofBool(s != nullptr && s->looping);
}
void setLooped(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* s = sound(ecs, e)) s->looping = v.boolean;
}
InstanceValue getPlaying(ECS& ecs, EntityId e) {
    const auto* s = sound(ecs, e);
    return InstanceValue::ofBool(s != nullptr && s->playing);
}
void setPlaying(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* s = sound(ecs, e)) s->playing = v.boolean;
}
InstanceValue getRollOffMin(ECS& ecs, EntityId e) {
    const auto* s = sound(ecs, e);
    return InstanceValue::ofNumber(s != nullptr ? s->minDistance : 10.0);
}
void setRollOffMin(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* s = sound(ecs, e)) s->minDistance = std::max(0.0f, static_cast<float>(v.number));
}
InstanceValue getRollOffMax(ECS& ecs, EntityId e) {
    const auto* s = sound(ecs, e);
    return InstanceValue::ofNumber(s != nullptr ? s->maxDistance : 10000.0);
}
void setRollOffMax(ECS& ecs, EntityId e, const InstanceValue& v) {
    if (auto* s = sound(ecs, e)) s->maxDistance = std::max(0.0f, static_cast<float>(v.number));
}

// Lighting.TimeOfDay is ClockTime as "hh:mm:ss".
InstanceValue getTimeOfDay(ECS& ecs, EntityId e) {
    double hours = 14.0;
    if (const auto* info = ecs.tryGetComponent<InstanceInfo>(e)) {
        if (const auto it = info->properties.find("ClockTime"); it != info->properties.end()) hours = it->second.number;
    }
    const int seconds = static_cast<int>(std::lround(hours * 3600.0)) % 86400;
    char text[16];
    std::snprintf(text, sizeof text, "%02d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
    return InstanceValue::ofString(text);
}
void setTimeOfDay(ECS& ecs, EntityId e, const InstanceValue& v) {
    int h = 0;
    int m = 0;
    int sec = 0;
    if (std::sscanf(v.text.c_str(), "%d:%d:%d", &h, &m, &sec) < 1) return;
    const PropertyDef* clock = findProperty("Lighting", "ClockTime");
    setProperty(ecs, refOf(ecs, e), *clock, InstanceValue::ofNumber(h + m / 60.0 + sec / 3600.0));
}
void wrapClockTime(ECS& ecs, EntityId e, const InstanceValue& v) {
    const double wrapped = std::fmod(std::fmod(v.number, 24.0) + 24.0, 24.0);
    if (wrapped == v.number) return;
    if (auto* info = ecs.tryGetComponent<InstanceInfo>(e)) info->properties["ClockTime"] = InstanceValue::ofNumber(wrapped);
}

PropertyDef prop(std::string name, PropertyType type, InstanceValue (*get)(ECS&, EntityId),
                 void (*set)(ECS&, EntityId, const InstanceValue&)) {
    PropertyDef def;
    def.name = std::move(name);
    def.type = type;
    def.get = get;
    def.set = set;
    return def;
}

PropertyDef stored(std::string name, PropertyType type, InstanceValue defaultValue,
                   void (*onSet)(ECS&, EntityId, const InstanceValue&) = nullptr) {
    PropertyDef def;
    def.name = std::move(name);
    def.type = type;
    def.defaultValue = std::move(defaultValue);
    def.set = onSet;
    return def;
}

PropertyDef storedEnum(std::string name, const std::string& enumType, const std::string& item, int value,
                       void (*onSet)(ECS&, EntityId, const InstanceValue&) = nullptr) {
    PropertyDef def = stored(std::move(name), PropertyType::Enum, InstanceValue::ofEnum(enumType, item, value), onSet);
    def.enumType = enumType;
    return def;
}

// Computed from another property (Position from CFrame), so not saved twice.
PropertyDef derived(PropertyDef def) {
    def.serialized = false;
    return def;
}

PropertyDef readOnlyStored(std::string name, PropertyType type, InstanceValue defaultValue) {
    PropertyDef def = stored(std::move(name), type, std::move(defaultValue));
    def.readOnly = true;
    return def;
}

PropertyDef readOnly(std::string name, PropertyType type) {
    PropertyDef def;
    def.name = std::move(name);
    def.type = type;
    def.readOnly = true;
    return def;
}

std::vector<ClassDef> buildClasses() {
    std::vector<ClassDef> classes;
    auto add = [&](const char* name, const char* superclass, bool creatable, bool service = false) -> ClassDef& {
        ClassDef def;
        def.name = name;
        def.superclass = superclass;
        def.creatable = creatable;
        def.service = service;
        classes.push_back(std::move(def));
        return classes.back();
    };

    // Name, ClassName and Parent are handled by getProperty/setProperty
    // directly, since game and workspace have them too.
    auto& instance = add("Instance", "", false);
    instance.properties = {
        prop("Name", PropertyType::String, nullptr, nullptr),
        derived(readOnly("ClassName", PropertyType::String)),
        prop("Parent", PropertyType::Instance, nullptr, nullptr),
        stored("Archivable", PropertyType::Bool, InstanceValue::ofBool(true)),
    };
    instance.properties.back().replicated = false;
    instance.methods = {"GetChildren", "GetDescendants", "FindFirstChild", "FindFirstChildOfClass",
                        "FindFirstChildWhichIsA", "FindFirstAncestor", "FindFirstAncestorOfClass",
                        "FindFirstAncestorWhichIsA", "WaitForChild", "IsA", "IsDescendantOf", "IsAncestorOf",
                        "Clone", "Destroy", "ClearAllChildren", "GetFullName", "GetAttribute", "SetAttribute",
                        "GetAttributes", "GetPropertyChangedSignal", "GetAttributeChangedSignal", "AddTag",
                        "RemoveTag", "HasTag", "GetTags"};
    instance.events = {"Changed", "ChildAdded", "ChildRemoved", "DescendantAdded", "DescendantRemoving",
                       "AncestryChanged", "AttributeChanged", "Destroying"};

    add("PVInstance", "Instance", false);
    auto& basePart = add("BasePart", "PVInstance", false);
    basePart.properties = {
        derived(prop("Position", PropertyType::Vector3, &getPosition, &setPosition)),
        prop("CFrame", PropertyType::CFrame, &getCFrame, &setCFrame),
        derived(prop("Orientation", PropertyType::Vector3, &getOrientation, &setOrientation)),
        derived(prop("Rotation", PropertyType::Vector3, &getRotation, &setRotation)),
        prop("Size", PropertyType::Vector3, &getSize, &setSize),
        prop("Color", PropertyType::Color3, &getColor, &setColor),
        derived(prop("BrickColor", PropertyType::BrickColor, &getBrickColor, &setColor)),
        prop("Transparency", PropertyType::Number, &getTransparency, &setTransparency),
        stored("Reflectance", PropertyType::Number, InstanceValue::ofNumber(0.0), &setReflectance),
        prop("CastShadow", PropertyType::Bool, &getCastShadow, &setCastShadow),
        prop("Anchored", PropertyType::Bool, &getAnchored, &setAnchored),
        stored("CanCollide", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("CanTouch", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("CanQuery", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("Locked", PropertyType::Bool, InstanceValue::ofBool(false)),
        storedEnum("Material", "Material", "Plastic", 256, &setMaterial),
    };
    basePart.events = {"Touched", "TouchEnded"};
    add("Part", "BasePart", true).properties = {storedEnum("Shape", "PartType", "Block", 1, &setShape)};
    add("WedgePart", "BasePart", true);
    add("MeshPart", "BasePart", true);
    add("PartOperation", "BasePart", false);
    add("UnionOperation", "PartOperation", true);
    add("SpawnLocation", "Part", true).properties = {
        stored("Enabled", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("Neutral", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("Duration", PropertyType::Number, InstanceValue::ofNumber(10.0)),
    };
    add("Seat", "Part", true).properties = {stored("Disabled", PropertyType::Bool, InstanceValue::ofBool(false))};
    add("VehicleSeat", "BasePart", true);

    auto& model = add("Model", "PVInstance", true);
    model.properties = {stored("PrimaryPart", PropertyType::Instance, InstanceValue{})};
    add("WorldRoot", "Model", false).methods = {"Raycast"};
    add("Workspace", "WorldRoot", false, true).properties = {
        stored("Gravity", PropertyType::Number, InstanceValue::ofNumber(196.2)),
        stored("FallenPartsDestroyHeight", PropertyType::Number, InstanceValue::ofNumber(-500.0)),
    };

    add("Folder", "Instance", true);
    add("Configuration", "Instance", true);

    PropertyDef weldActive = readOnlyStored("Active", PropertyType::Bool, InstanceValue::ofBool(false));
    weldActive.serialized = false;
    weldActive.replicated = false;
    add("WeldConstraint", "Instance", true).properties = {
        stored("Part0", PropertyType::Instance, InstanceValue{}),
        stored("Part1", PropertyType::Instance, InstanceValue{}),
        stored("Enabled", PropertyType::Bool, InstanceValue::ofBool(true)),
        weldActive,
    };

    add("LuaSourceContainer", "Instance", false);
    add("BaseScript", "LuaSourceContainer", false).properties = {
        stored("Disabled", PropertyType::Bool, InstanceValue::ofBool(false)),
        prop("Enabled", PropertyType::Bool, &getScriptEnabled, &setScriptEnabled),
    };
    add("Script", "BaseScript", true);
    add("LocalScript", "Script", true);
    add("ModuleScript", "LuaSourceContainer", true);

    auto& lightClass = add("Light", "Instance", false);
    lightClass.properties = {
        prop("Brightness", PropertyType::Number, &getBrightness, &setBrightness),
        prop("Color", PropertyType::Color3, &getLightColor, &setLightColor),
        prop("Enabled", PropertyType::Bool, &getLightEnabled, &setLightEnabled),
        prop("Shadows", PropertyType::Bool, &getShadows, &setShadows),
    };
    add("PointLight", "Light", true).properties = {prop("Range", PropertyType::Number, &getRange, &setRange)};
    add("SpotLight", "Light", true).properties = {
        prop("Range", PropertyType::Number, &getRange, &setRange),
        prop("Angle", PropertyType::Number, &getAngle, &setAngle),
    };
    add("SurfaceLight", "Light", true).properties = {
        prop("Range", PropertyType::Number, &getRange, &setRange),
        prop("Angle", PropertyType::Number, &getAngle, &setAngle),
    };

    add("ValueBase", "Instance", false);
    add("IntValue", "ValueBase", true).properties = {
        stored("Value", PropertyType::Number, InstanceValue::ofNumber(0.0), &roundIntValue)};
    add("NumberValue", "ValueBase", true).properties = {
        stored("Value", PropertyType::Number, InstanceValue::ofNumber(0.0))};
    add("StringValue", "ValueBase", true).properties = {
        stored("Value", PropertyType::String, InstanceValue::ofString(""))};
    add("BoolValue", "ValueBase", true).properties = {
        stored("Value", PropertyType::Bool, InstanceValue::ofBool(false))};
    add("ObjectValue", "ValueBase", true).properties = {stored("Value", PropertyType::Instance, InstanceValue{})};
    add("Vector3Value", "ValueBase", true).properties = {
        stored("Value", PropertyType::Vector3, InstanceValue::ofVector3(glm::vec3(0.0f)))};
    add("Color3Value", "ValueBase", true).properties = {
        stored("Value", PropertyType::Color3, InstanceValue::ofColor3(glm::vec3(0.0f)))};
    add("CFrameValue", "ValueBase", true).properties = {
        stored("Value", PropertyType::CFrame, InstanceValue::ofCFrame(glm::vec3(0.0f), glm::quat(1, 0, 0, 0)))};
    add("BrickColorValue", "ValueBase", true).properties = {
        stored("Value", PropertyType::BrickColor, InstanceValue::ofBrickColor(kDefaultPartColor))};

    auto& baseRemoteEvent = add("BaseRemoteEvent", "Instance", false);
    baseRemoteEvent.methods = {"FireServer", "FireClient", "FireAllClients"};
    baseRemoteEvent.events = {"OnServerEvent", "OnClientEvent"};
    add("RemoteEvent", "BaseRemoteEvent", true);
    add("UnreliableRemoteEvent", "BaseRemoteEvent", true);
    add("RemoteFunction", "Instance", true).methods = {"InvokeServer", "InvokeClient"};
    auto& bindableEvent = add("BindableEvent", "Instance", true);
    bindableEvent.methods = {"Fire"};
    bindableEvent.events = {"Event"};
    add("BindableFunction", "Instance", true).methods = {"Invoke"};
    auto& soundClass = add("Sound", "Instance", true);
    soundClass.properties = {
        prop("SoundId", PropertyType::String, &getSoundId, &setSoundId),
        prop("Volume", PropertyType::Number, &getVolume, &setVolume),
        prop("PlaybackSpeed", PropertyType::Number, &getPlaybackSpeed, &setPlaybackSpeed),
        prop("Looped", PropertyType::Bool, &getLooped, &setLooped),
        prop("Playing", PropertyType::Bool, &getPlaying, &setPlaying),
        derived(prop("IsPlaying", PropertyType::Bool, &getPlaying, nullptr)),
        prop("RollOffMinDistance", PropertyType::Number, &getRollOffMin, &setRollOffMin),
        prop("RollOffMaxDistance", PropertyType::Number, &getRollOffMax, &setRollOffMax),
        stored("PlayOnRemove", PropertyType::Bool, InstanceValue::ofBool(false)),
    };
    soundClass.properties[5].readOnly = true;
    soundClass.methods = {"Play", "Stop", "Pause", "Resume"};
    soundClass.events = {"Played", "Ended", "Stopped", "Paused", "Resumed", "Loaded"};
    add("SoundGroup", "Instance", true).properties = {stored("Volume", PropertyType::Number, InstanceValue::ofNumber(0.5))};
    // Present as objects; what they do arrives in later bridge steps.
    add("Tool", "Instance", true);
    add("Accessory", "Instance", true);
    add("StarterPlayerScripts", "Instance", false);
    add("StarterCharacterScripts", "Instance", false);
    add("PlayerScripts", "Instance", false);
    // Layout writes AbsolutePosition/AbsoluteSize (core/RobloxGui.cpp).
    auto layoutOutput = [](const char* name) {
        PropertyDef def = derived(readOnlyStored(name, PropertyType::Vector2, InstanceValue::ofVector2(0.0f, 0.0f)));
        def.replicated = false;
        def.studioVisible = false;
        return def;
    };
    add("GuiBase2d", "Instance", false).properties = {layoutOutput("AbsolutePosition"), layoutOutput("AbsoluteSize")};
    add("LayerCollector", "GuiBase2d", false).properties = {
        stored("Enabled", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("ResetOnSpawn", PropertyType::Bool, InstanceValue::ofBool(true)),
    };
    add("ScreenGui", "LayerCollector", true).properties = {
        stored("DisplayOrder", PropertyType::Number, InstanceValue::ofNumber(0.0)),
        stored("IgnoreGuiInset", PropertyType::Bool, InstanceValue::ofBool(false)),
    };
    auto rgb = [](float r, float g, float b) { return InstanceValue::ofColor3(glm::vec3(r, g, b) / 255.0f); };
    auto& guiObject = add("GuiObject", "GuiBase2d", false);
    guiObject.properties = {
        stored("Size", PropertyType::UDim2, InstanceValue::ofUDim2(0, 100, 0, 100)),
        stored("Position", PropertyType::UDim2, InstanceValue::ofUDim2(0, 0, 0, 0)),
        stored("AnchorPoint", PropertyType::Vector2, InstanceValue::ofVector2(0, 0)),
        stored("BackgroundColor3", PropertyType::Color3, rgb(255, 255, 255)),
        stored("BackgroundTransparency", PropertyType::Number, InstanceValue::ofNumber(0.0)),
        stored("BorderColor3", PropertyType::Color3, rgb(27, 42, 53)),
        stored("BorderSizePixel", PropertyType::Number, InstanceValue::ofNumber(1.0)),
        stored("Visible", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("ZIndex", PropertyType::Number, InstanceValue::ofNumber(1.0)),
        stored("LayoutOrder", PropertyType::Number, InstanceValue::ofNumber(0.0)),
        stored("ClipsDescendants", PropertyType::Bool, InstanceValue::ofBool(false)),
        stored("Rotation", PropertyType::Number, InstanceValue::ofNumber(0.0)),
        stored("Active", PropertyType::Bool, InstanceValue::ofBool(false)),
    };
    guiObject.events = {"MouseEnter", "MouseLeave"};
    add("Frame", "GuiObject", true);
    const std::vector<PropertyDef> textProperties = {
        stored("Text", PropertyType::String, InstanceValue::ofString("Label")),
        stored("TextColor3", PropertyType::Color3, rgb(27, 42, 53)),
        stored("TextSize", PropertyType::Number, InstanceValue::ofNumber(14.0)),
        stored("TextScaled", PropertyType::Bool, InstanceValue::ofBool(false)),
        stored("TextWrapped", PropertyType::Bool, InstanceValue::ofBool(false)),
        stored("TextTransparency", PropertyType::Number, InstanceValue::ofNumber(0.0)),
        storedEnum("TextXAlignment", "TextXAlignment", "Center", 2),
        storedEnum("TextYAlignment", "TextYAlignment", "Center", 1),
        storedEnum("Font", "Font", "SourceSans", 3),
    };
    const std::vector<PropertyDef> imageProperties = {
        stored("Image", PropertyType::String, InstanceValue::ofString("")),
        stored("ImageColor3", PropertyType::Color3, rgb(255, 255, 255)),
        stored("ImageTransparency", PropertyType::Number, InstanceValue::ofNumber(0.0)),
    };
    add("GuiLabel", "GuiObject", false);
    add("TextLabel", "GuiLabel", true).properties = textProperties;
    add("ImageLabel", "GuiLabel", true).properties = imageProperties;
    auto& guiButton = add("GuiButton", "GuiObject", false);
    guiButton.properties = {stored("AutoButtonColor", PropertyType::Bool, InstanceValue::ofBool(true))};
    guiButton.events = {"MouseButton1Click", "MouseButton1Down", "MouseButton1Up", "Activated"};
    add("TextButton", "GuiButton", true).properties = textProperties;
    add("ImageButton", "GuiButton", true).properties = imageProperties;
    add("UIComponent", "Instance", false);
    add("UIBase", "UIComponent", false);
    add("UICorner", "UIComponent", true).properties = {
        stored("CornerRadius", PropertyType::UDim, InstanceValue::ofUDim(0, 8))};
    add("UIPadding", "UIComponent", true).properties = {
        stored("PaddingLeft", PropertyType::UDim, InstanceValue::ofUDim(0, 0)),
        stored("PaddingRight", PropertyType::UDim, InstanceValue::ofUDim(0, 0)),
        stored("PaddingTop", PropertyType::UDim, InstanceValue::ofUDim(0, 0)),
        stored("PaddingBottom", PropertyType::UDim, InstanceValue::ofUDim(0, 0)),
    };
    add("UILayout", "UIComponent", false);
    add("UIGridStyleLayout", "UILayout", false).properties = {
        storedEnum("FillDirection", "FillDirection", "Vertical", 1),
        storedEnum("SortOrder", "SortOrder", "LayoutOrder", 2),
        storedEnum("HorizontalAlignment", "HorizontalAlignment", "Left", 1),
        storedEnum("VerticalAlignment", "VerticalAlignment", "Top", 1),
    };
    add("UIListLayout", "UIGridStyleLayout", true).properties = {
        stored("Padding", PropertyType::UDim, InstanceValue::ofUDim(0, 0))};

    add("DataModel", "Instance", false).methods = {"GetService", "FindService"};
    // Has no entity: game:GetService("RunService") is always the same object.
    auto& runService = add("RunService", "Instance", false, true);
    runService.methods = {"IsServer", "IsClient", "IsStudio", "IsRunning", "IsRunMode", "IsEdit"};
    runService.events = {"Stepped", "PreSimulation", "PostSimulation", "Heartbeat", "RenderStepped", "PreRender"};
    auto& playersService = add("Players", "Instance", false, true);
    playersService.properties = {
        readOnlyStored("LocalPlayer", PropertyType::Instance, InstanceValue{}),
        stored("RespawnTime", PropertyType::Number, InstanceValue::ofNumber(5.0)),
        stored("CharacterAutoLoads", PropertyType::Bool, InstanceValue::ofBool(true)),
        readOnlyStored("MaxPlayers", PropertyType::Number, InstanceValue::ofNumber(1.0)),
    };
    playersService.methods = {"GetPlayers", "GetPlayerFromCharacter", "GetPlayerByUserId"};
    playersService.events = {"PlayerAdded", "PlayerRemoving"};

    auto& player = add("Player", "Instance", false);
    player.properties = {
        readOnlyStored("UserId", PropertyType::Number, InstanceValue::ofNumber(0.0)),
        stored("DisplayName", PropertyType::String, InstanceValue::ofString("")),
        stored("Character", PropertyType::Instance, InstanceValue{}),
        stored("RespawnLocation", PropertyType::Instance, InstanceValue{}),
        readOnlyStored("AccountAge", PropertyType::Number, InstanceValue::ofNumber(0.0)),
        stored("CanLoadCharacterAppearance", PropertyType::Bool, InstanceValue::ofBool(true)),
    };
    player.methods = {"LoadCharacter"};
    player.events = {"CharacterAdded", "CharacterRemoving"};
    add("Backpack", "Instance", false);
    add("PlayerGui", "Instance", false);

    auto& humanoid = add("Humanoid", "Instance", true);
    humanoid.properties = {
        stored("Health", PropertyType::Number, InstanceValue::ofNumber(100.0), &players::onHealthSet),
        stored("MaxHealth", PropertyType::Number, InstanceValue::ofNumber(100.0), &players::onMaxHealthSet),
        stored("WalkSpeed", PropertyType::Number, InstanceValue::ofNumber(players::kDefaultWalkSpeed)),
        stored("JumpPower", PropertyType::Number, InstanceValue::ofNumber(players::kDefaultJumpPower)),
        stored("JumpHeight", PropertyType::Number, InstanceValue::ofNumber(players::kDefaultJumpHeight)),
        stored("UseJumpPower", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("AutoRotate", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("HipHeight", PropertyType::Number, InstanceValue::ofNumber(2.0)),
        stored("Jump", PropertyType::Bool, InstanceValue::ofBool(false)),
        stored("Sit", PropertyType::Bool, InstanceValue::ofBool(false)),
        stored("PlatformStand", PropertyType::Bool, InstanceValue::ofBool(false)),
        readOnlyStored("MoveDirection", PropertyType::Vector3, InstanceValue::ofVector3(glm::vec3(0.0f))),
        stored("WalkToPoint", PropertyType::Vector3, InstanceValue::ofVector3(glm::vec3(0.0f))),
        readOnlyStored("RootPart", PropertyType::Instance, InstanceValue{}),
        stored("DisplayName", PropertyType::String, InstanceValue::ofString("")),
        storedEnum("RigType", "HumanoidRigType", "R15", 1),
        stored("BreakJointsOnDeath", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("RequiresNeck", PropertyType::Bool, InstanceValue::ofBool(true)),
    };
    humanoid.methods = {"TakeDamage", "MoveTo", "Move", "GetState", "ChangeState"};
    humanoid.events = {"Died", "HealthChanged", "MoveToFinished", "Running", "Jumping", "FreeFalling", "StateChanged"};

    add("DataStoreService", "Instance", false, true).methods = {"GetDataStore", "GetGlobalDataStore"};
    add("GlobalDataStore", "Instance", false).methods = {"GetAsync", "SetAsync", "UpdateAsync", "RemoveAsync",
                                                         "IncrementAsync"};
    add("DataStore", "GlobalDataStore", false);

    auto& tweenBase = add("TweenBase", "Instance", false);
    tweenBase.properties = {readOnlyStored("PlaybackState", PropertyType::Enum, InstanceValue::ofEnum("PlaybackState", "Begin", 0))};
    tweenBase.properties[0].enumType = "PlaybackState";
    tweenBase.methods = {"Play", "Pause", "Cancel"};
    tweenBase.events = {"Completed"};
    add("Tween", "TweenBase", false).properties = {readOnlyStored("Instance", PropertyType::Instance, InstanceValue{})};
    add("TweenService", "Instance", false, true).methods = {"Create", "GetValue"};
    auto& debris = add("Debris", "Instance", false, true);
    debris.properties = {stored("MaxItems", PropertyType::Number, InstanceValue::ofNumber(1000.0))};
    debris.methods = {"AddItem"};
    auto& collection = add("CollectionService", "Instance", false, true);
    collection.methods = {"AddTag", "RemoveTag", "HasTag", "GetTags", "GetTagged", "GetAllTags",
                          "GetInstanceAddedSignal", "GetInstanceRemovedSignal"};

    auto& lighting = add("Lighting", "Instance", false, true);
    lighting.properties = {
        stored("ClockTime", PropertyType::Number, InstanceValue::ofNumber(14.0), &wrapClockTime),
        derived(prop("TimeOfDay", PropertyType::String, &getTimeOfDay, &setTimeOfDay)),
        stored("Brightness", PropertyType::Number, InstanceValue::ofNumber(2.0)),
        stored("Ambient", PropertyType::Color3, InstanceValue::ofColor3(glm::vec3(0.0f))),
        stored("OutdoorAmbient", PropertyType::Color3, InstanceValue::ofColor3(glm::vec3(128.0f / 255.0f))),
        stored("FogColor", PropertyType::Color3, InstanceValue::ofColor3(glm::vec3(192.0f / 255.0f))),
        stored("FogStart", PropertyType::Number, InstanceValue::ofNumber(0.0)),
        stored("FogEnd", PropertyType::Number, InstanceValue::ofNumber(100000.0)),
        stored("GlobalShadows", PropertyType::Bool, InstanceValue::ofBool(true)),
        stored("ExposureCompensation", PropertyType::Number, InstanceValue::ofNumber(0.0)),
        stored("GeographicLatitude", PropertyType::Number, InstanceValue::ofNumber(0.0)),
    };
    lighting.methods = {"GetMinutesAfterMidnight", "SetMinutesAfterMidnight"};

    for (const char* service : {"ReplicatedStorage", "ReplicatedFirst", "ServerScriptService",
                                "ServerStorage", "StarterGui", "StarterPack", "StarterPlayer", "SoundService", "Teams",
                                "Chat"}) {
        add(service, "Instance", false, true);
    }
    return classes;
}

const std::unordered_map<std::string, const ClassDef*>& classIndex() {
    static const std::unordered_map<std::string, const ClassDef*> index = [] {
        std::unordered_map<std::string, const ClassDef*> map;
        for (const ClassDef& def : allClasses()) map[def.name] = &def;
        return map;
    }();
    return index;
}

} // namespace

const std::vector<ClassDef>& allClasses() {
    static const std::vector<ClassDef> classes = buildClasses();
    return classes;
}

const ClassDef* findClass(const std::string& name) {
    const auto& index = classIndex();
    const auto it = index.find(name);
    return it == index.end() ? nullptr : it->second;
}

bool classIsA(const std::string& className, const std::string& base) {
    for (const ClassDef* def = findClass(className); def != nullptr; def = findClass(def->superclass)) {
        if (def->name == base) return true;
    }
    return false;
}

const PropertyDef* findProperty(const std::string& className, const std::string& property) {
    for (const ClassDef* def = findClass(className); def != nullptr; def = findClass(def->superclass)) {
        for (const PropertyDef& p : def->properties) {
            if (p.name == property) return &p;
        }
    }
    return nullptr;
}

bool classHasMethod(const std::string& className, const std::string& method) {
    for (const ClassDef* def = findClass(className); def != nullptr; def = findClass(def->superclass)) {
        if (std::find(def->methods.begin(), def->methods.end(), method) != def->methods.end()) return true;
    }
    return false;
}

bool classHasEvent(const std::string& className, const std::string& event) {
    for (const ClassDef* def = findClass(className); def != nullptr; def = findClass(def->superclass)) {
        if (std::find(def->events.begin(), def->events.end(), event) != def->events.end()) return true;
    }
    return false;
}

bool isPlannedService(const std::string& name) {
    static const char* const kPlanned[] = {
        "UserInputService", "ContextActionService", "HttpService", "MarketplaceService", "PhysicsService", "TextService",
        "GuiService", "PathfindingService", "BadgeService", "MessagingService", "TeleportService",
        "ProximityPromptService", "TextChatService", "LocalizationService", "GroupService", "SocialService",
        "VRService", "HapticService", "AnalyticsService", "MemoryStoreService", "AssetService", "InsertService",
        "ContentProvider", "StarterPlayerScripts"};
    for (const char* planned : kPlanned) {
        if (name == planned) return true;
    }
    return false;
}

InstanceRef refOf(ECS& ecs, EntityId entity) {
    if (!valid(ecs, entity)) return kNoInstance;
    if (parentEntity(ecs, entity) == kNullEntity && entityClass(ecs, entity) == "Workspace") return kWorkspaceInstance;
    return static_cast<InstanceRef>(entt::to_integral(entity));
}

EntityId entityOf(ECS& ecs, InstanceRef ref) {
    if (ref == kGameInstance || ref == kRunServiceInstance || ref == kNoInstance) return kNullEntity;
    if (ref == kWorkspaceInstance) return workspaceEntity(ecs);
    const auto entity = static_cast<EntityId>(ref);
    return valid(ecs, entity) ? entity : kNullEntity;
}

bool isAlive(ECS& ecs, InstanceRef ref) {
    if (ref == kGameInstance || ref == kWorkspaceInstance || ref == kRunServiceInstance) return true;
    if (ref == kNoInstance) return false;
    return valid(ecs, static_cast<EntityId>(ref));
}

std::string className(ECS& ecs, InstanceRef ref) {
    if (ref == kGameInstance) return "DataModel";
    if (ref == kWorkspaceInstance) return "Workspace";
    if (ref == kRunServiceInstance) return "RunService";
    const EntityId e = entityOf(ecs, ref);
    return valid(ecs, e) ? entityClass(ecs, e) : std::string();
}

std::string name(ECS& ecs, InstanceRef ref) {
    if (ref == kGameInstance) return "Game";
    if (ref == kRunServiceInstance) return "Run Service";
    const EntityId e = entityOf(ecs, ref);
    if (!valid(ecs, e)) return ref == kWorkspaceInstance ? "Workspace" : std::string();
    if (const auto* n = ecs.tryGetComponent<Name>(e)) return n->value;
    return entityClass(ecs, e);
}

void setName(ECS& ecs, InstanceRef ref, const std::string& value) {
    const EntityId e = entityOf(ecs, ref);
    if (!valid(ecs, e)) return;
    std::string& stored = ecs.raw().get_or_emplace<Name>(e).value;
    if (stored == value) return;
    stored = value;
    signals::propertyChanged(ecs, ref, "Name", InstanceValue::ofString(value));
}

std::string fullName(ECS& ecs, InstanceRef ref) {
    std::vector<std::string> parts;
    for (InstanceRef at = ref; at != kNoInstance && at != kGameInstance && parts.size() < 256; at = parent(ecs, at)) {
        parts.push_back(name(ecs, at));
    }
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!out.empty()) out += '.';
        out += *it;
    }
    return ref == kGameInstance ? name(ecs, ref) : out;
}

InstanceRef parent(ECS& ecs, InstanceRef ref) {
    if (ref == kGameInstance || ref == kNoInstance) return kNoInstance;
    if (ref == kWorkspaceInstance || ref == kRunServiceInstance) return kGameInstance;
    const EntityId e = entityOf(ecs, ref);
    if (!valid(ecs, e)) return kNoInstance;
    const EntityId p = parentEntity(ecs, e);
    if (p != kNullEntity) return refOf(ecs, p);
    if (const auto* info = ecs.tryGetComponent<InstanceInfo>(e); info != nullptr && info->detached) return kNoInstance;
    return isServiceClass(entityClass(ecs, e)) ? kGameInstance : kWorkspaceInstance;
}

std::vector<InstanceRef> children(ECS& ecs, InstanceRef ref) {
    std::vector<InstanceRef> out;
    auto addChildrenOf = [&](EntityId e) {
        if (const auto* h = ecs.tryGetComponent<Hierarchy>(e)) {
            for (EntityId child : h->children) {
                if (valid(ecs, child) && !hidden(ecs, child)) out.push_back(refOf(ecs, child));
            }
        }
    };
    if (ref == kGameInstance) {
        out.push_back(kWorkspaceInstance);
        for (EntityId e : rootEntities(ecs)) {
            const std::string cls = entityClass(ecs, e);
            if (cls != "Workspace" && isServiceClass(cls)) out.push_back(refOf(ecs, e));
        }
        out.push_back(kRunServiceInstance);
        return out;
    }
    if (ref == kWorkspaceInstance) {
        const EntityId ws = workspaceEntity(ecs);
        if (ws != kNullEntity) addChildrenOf(ws);
        for (EntityId e : rootEntities(ecs)) {
            if (parent(ecs, refOf(ecs, e)) == kWorkspaceInstance && refOf(ecs, e) != kWorkspaceInstance) {
                out.push_back(refOf(ecs, e));
            }
        }
        return out;
    }
    const EntityId e = entityOf(ecs, ref);
    if (valid(ecs, e)) addChildrenOf(e);
    return out;
}

std::vector<InstanceRef> descendants(ECS& ecs, InstanceRef ref) {
    std::vector<InstanceRef> out;
    std::function<void(InstanceRef, int)> walk = [&](InstanceRef at, int depth) {
        if (depth > 512) return;
        for (InstanceRef child : children(ecs, at)) {
            out.push_back(child);
            walk(child, depth + 1);
        }
    };
    walk(ref, 0);
    return out;
}

bool isDescendantOf(ECS& ecs, InstanceRef ref, InstanceRef ancestor) {
    int guard = 0;
    for (InstanceRef at = parent(ecs, ref); at != kNoInstance && guard < 1024; at = parent(ecs, at), ++guard) {
        if (at == ancestor) return true;
    }
    return false;
}

bool setParent(ECS& ecs, InstanceRef child, InstanceRef newParent, std::string& error) {
    if (child == kGameInstance || child == kWorkspaceInstance || child == kRunServiceInstance) {
        error = "The Parent property of " + name(ecs, child) + " is locked";
        return false;
    }
    const EntityId e = entityOf(ecs, child);
    if (!valid(ecs, e)) {
        error = "The Parent property of a destroyed Instance is locked";
        return false;
    }
    if (newParent == child || (newParent != kNoInstance && isDescendantOf(ecs, newParent, child))) {
        error = "Attempt to set " + fullName(ecs, child) + " as its own parent";
        return false;
    }
    if (newParent != kNoInstance && !isAlive(ecs, newParent)) {
        error = "Cannot set Parent to a destroyed Instance";
        return false;
    }
    if (newParent == kGameInstance && !isServiceClass(entityClass(ecs, e))) {
        error = "Kronos only keeps services directly under game";
        return false;
    }
    if (newParent == kRunServiceInstance) {
        error = "Kronos can't keep objects inside Run Service";
        return false;
    }
    const InstanceRef oldParent = parent(ecs, child);

    const Pose pose = worldPose(ecs, e);
    ensureInfo(ecs, e);
    EntityId target = newParent == kNoInstance || newParent == kGameInstance ? kNullEntity : entityOf(ecs, newParent);
    if (target == kNullEntity) {
        hierarchy::unparent(ecs, e);
    } else if (!hierarchy::setParent(ecs, e, target)) {
        error = "Could not set Parent";
        return false;
    }
    ecs.tryGetComponent<InstanceInfo>(e)->detached = newParent == kNoInstance;

    // Roblox keeps an object's world placement when its Parent changes.
    if (auto* transform = ecs.tryGetComponent<Transform>(e)) {
        const EntityId p = parentEntity(ecs, e);
        const glm::vec3 parentScale = p != kNullEntity ? worldPose(ecs, p).scale : glm::vec3(1.0f);
        transform->scale = pose.scale / glm::max(glm::abs(parentScale), glm::vec3(1e-6f));
        setWorldPose(ecs, e, pose.position, pose.rotation);
    }
    updateWorldPresence(ecs, e);
    if (oldParent != newParent) signals::parentChanged(ecs, child, oldParent);
    return true;
}

InstanceRef create(ECS& ecs, const std::string& cls, std::string& error) {
    const ClassDef* def = findClass(cls);
    if (def == nullptr || !def->creatable) {
        error = "Unable to create an Instance of type \"" + cls + "\"";
        return kNoInstance;
    }
    return createUnchecked(ecs, cls);
}

InstanceRef createUnchecked(ECS& ecs, const std::string& cls) {
    if (findClass(cls) == nullptr) return kNoInstance;
    const EntityId e = ecs.createEntity(cls);
    InstanceInfo info;
    info.className = cls;
    info.detached = true;

    if (classIsA(cls, "BasePart")) {
        const auto* meshes = ecs.raw().ctx().find<InstanceMeshes>();
        auto& r = ecs.addComponent<Renderable>(e);
        r.meshHandle = meshes != nullptr ? meshes->box : Renderable::kInvalidHandle;
        r.baseColor = glm::vec4(kDefaultPartColor, 1.0f);
        r.roughness = 0.85f;
        r.metallic = 0.0f;
        auto& mesh = ecs.addComponent<MeshSource>(e);
        mesh.kind = MeshSourceKind::Box;
        mesh.params = glm::vec3(0.5f);
        ecs.tryGetComponent<Transform>(e)->scale = cls == "SpawnLocation" ? glm::vec3(12.0f, 1.0f, 12.0f)
                                                                          : glm::vec3(4.0f, 1.0f, 2.0f);
        info.properties["Anchored"] = InstanceValue::ofBool(false);
        ecs.addComponent<InstanceInfo>(e, std::move(info));
        fitPartCollider(ecs, e);
    } else if (classIsA(cls, "Light")) {
        auto& l = ecs.addComponent<Light>(e);
        l.radius = 8.0f;
        if (cls == "SpotLight") {
            l.type = LightType::Spot;
            l.outerConeDegrees = 45.0f;
            l.innerConeDegrees = 36.0f;
        }
    } else if (cls == "Sound") {
        auto& s = ecs.addComponent<AudioSource>(e);
        s.volume = 0.5f;
        s.minDistance = 10.0f;
        s.maxDistance = 10000.0f;
        s.spatial = false;
    } else if (classIsA(cls, "LuaSourceContainer")) {
        auto& script = ecs.addComponent<Script>(e);
        script.autoRun = false;
        // Marks a Roblox script (core/RobloxScripts.hpp).
        if (classIsA(cls, "BaseScript")) info.properties["Disabled"] = InstanceValue::ofBool(false);
    }
    if (ecs.tryGetComponent<InstanceInfo>(e) == nullptr) ecs.addComponent<InstanceInfo>(e, std::move(info));
    updateWorldPresence(ecs, e);
    return refOf(ecs, e);
}

namespace {

template <typename T>
void copyComponent(ECS& ecs, EntityId from, EntityId to) {
    if (const T* c = ecs.tryGetComponent<T>(from)) ecs.addComponent<T>(to, *c);
}

EntityId cloneEntity(ECS& ecs, EntityId from) {
    if (const auto* info = ecs.tryGetComponent<InstanceInfo>(from)) {
        const auto it = info->properties.find("Archivable");
        if (it != info->properties.end() && !it->second.boolean) return kNullEntity;
    }
    const EntityId to = ecs.createEntity();
    copyComponent<Transform>(ecs, from, to);
    copyComponent<Name>(ecs, from, to);
    copyComponent<Renderable>(ecs, from, to);
    copyComponent<MeshSource>(ecs, from, to);
    copyComponent<ColliderShape>(ecs, from, to);
    copyComponent<PhysicsMaterial>(ecs, from, to);
    copyComponent<Light>(ecs, from, to);
    copyComponent<InstanceInfo>(ecs, from, to);
    if (const auto* body = ecs.tryGetComponent<RigidBody>(from)) {
        ecs.addComponent<RigidBody>(to, RigidBody{RigidBody::kInvalidBodyId, body->motionType});
    }
    if (const auto* script = ecs.tryGetComponent<Script>(from)) {
        Script copy;
        copy.source = script->source;
        copy.autoRun = script->autoRun;
        ecs.addComponent<Script>(to, std::move(copy));
    }
    if (const auto* h = ecs.tryGetComponent<Hierarchy>(from)) {
        const std::vector<EntityId> kids = h->children;
        for (EntityId child : kids) {
            if (!valid(ecs, child) || hidden(ecs, child)) continue;
            const EntityId copy = cloneEntity(ecs, child);
            if (copy != kNullEntity) linkRaw(ecs, copy, to);
        }
    }
    return to;
}

} // namespace

InstanceRef clone(ECS& ecs, InstanceRef ref) {
    const EntityId from = ref == kWorkspaceInstance ? kNullEntity : entityOf(ecs, ref);
    if (!valid(ecs, from)) return kNoInstance;
    const Pose pose = worldPose(ecs, from);
    const EntityId to = cloneEntity(ecs, from);
    if (to == kNullEntity) return kNoInstance;
    if (auto* transform = ecs.tryGetComponent<Transform>(to)) {
        transform->position = pose.position;
        transform->rotation = pose.rotation;
        transform->scale = pose.scale;
    }
    InstanceInfo& info = ensureInfo(ecs, to);
    info.className = entityClass(ecs, from);
    info.detached = true;
    updateWorldPresence(ecs, to);
    return refOf(ecs, to);
}

void destroy(ECS& ecs, InstanceRef ref) {
    if (ref == kGameInstance || ref == kWorkspaceInstance) return;
    const EntityId e = entityOf(ecs, ref);
    if (!valid(ecs, e)) return;
    signals::destroying(ecs, ref);
    hierarchy::destroyEntityRecursive(ecs, e);
}

bool isDetached(ECS& ecs, EntityId entity) {
    for (EntityId e = entity; valid(ecs, e); e = parentEntity(ecs, e)) {
        if (const auto* info = ecs.tryGetComponent<InstanceInfo>(e); info != nullptr && info->detached) return true;
    }
    return false;
}

InstanceRef findService(ECS& ecs, const std::string& serviceName) {
    if (serviceName == "Workspace") return kWorkspaceInstance;
    if (serviceName == "RunService") return kRunServiceInstance;
    if (!isServiceClass(serviceName)) return kNoInstance;
    for (EntityId e : rootEntities(ecs)) {
        if (entityClass(ecs, e) == serviceName) return refOf(ecs, e);
    }
    return kNoInstance;
}

InstanceRef getService(ECS& ecs, const std::string& serviceName, std::string& error) {
    const InstanceRef found = findService(ecs, serviceName);
    if (found != kNoInstance) return found;
    if (isServiceClass(serviceName)) {
        const EntityId e = ecs.createEntity(serviceName);
        InstanceInfo info;
        info.className = serviceName;
        ecs.addComponent<InstanceInfo>(e, std::move(info));
        return refOf(ecs, e);
    }
    if (isPlannedService(serviceName)) {
        error = serviceName + " is not in Kronos yet; it is planned for the 4.3 Roblox bridge";
    } else {
        error = "'" + serviceName + "' is not a valid Service name";
    }
    return kNoInstance;
}

bool getProperty(ECS& ecs, InstanceRef ref, const PropertyDef& property, InstanceValue& out) {
    if (property.name == "Name") {
        out = InstanceValue::ofString(name(ecs, ref));
        return true;
    }
    if (property.name == "ClassName") {
        out = InstanceValue::ofString(className(ecs, ref));
        return true;
    }
    if (property.name == "Parent") {
        out = InstanceValue::ofInstance(parent(ecs, ref));
        return true;
    }
    const EntityId e = entityOf(ecs, ref);
    if (property.get != nullptr) {
        if (!valid(ecs, e)) return false;
        out = property.get(ecs, e);
        return true;
    }
    const InstanceInfo* info = findInfo(ecs, ref);
    if (info != nullptr) {
        const auto it = info->properties.find(property.name);
        if (it != info->properties.end()) {
            out = it->second;
            if (out.type == InstanceValue::Type::Instance && !isAlive(ecs, out.ref)) out = InstanceValue{};
            return true;
        }
    }
    out = property.defaultValue;
    return true;
}

void setProperty(ECS& ecs, InstanceRef ref, const PropertyDef& property, const InstanceValue& value) {
    if (property.name == "Name") {
        setName(ecs, ref, value.text);
        return;
    }
    // Changed fires for every property whose value really moved, including
    // the ones derived from this one (Position also moves CFrame).
    std::vector<std::pair<const PropertyDef*, InstanceValue>> watched;
    if (SignalHub* hub = signals::findHub(ecs); hub != nullptr && hub->watchesChanges(ref)) {
        const std::string cls = className(ecs, ref);
        std::vector<std::string> names{property.name};
        for (const auto& group : {std::vector<std::string>{"Position", "CFrame", "Orientation", "Rotation"},
                                  std::vector<std::string>{"Color", "BrickColor"}}) {
            if (std::find(group.begin(), group.end(), property.name) != group.end()) names = group;
        }
        for (const std::string& name : names) {
            const PropertyDef* def = findProperty(cls, name);
            InstanceValue before;
            if (def != nullptr && getProperty(ecs, ref, *def, before)) watched.emplace_back(def, before);
        }
    }
    const EntityId e = entityOf(ecs, ref);
    if (property.get == nullptr) infoFor(ecs, ref).properties[property.name] = value;
    if (property.set != nullptr && valid(ecs, e)) property.set(ecs, e, value);
    for (const auto& [def, before] : watched) {
        InstanceValue after;
        if (getProperty(ecs, ref, *def, after) && !sameValue(before, after)) {
            signals::propertyChanged(ecs, ref, def->name, after);
        }
    }
}

const InstanceValue* attribute(ECS& ecs, InstanceRef ref, const std::string& attributeName) {
    const InstanceInfo* info = findInfo(ecs, ref);
    if (info == nullptr) return nullptr;
    const auto it = info->attributes.find(attributeName);
    return it == info->attributes.end() ? nullptr : &it->second;
}

void setAttribute(ECS& ecs, InstanceRef ref, const std::string& attributeName, const InstanceValue& value) {
    InstanceInfo& info = infoFor(ecs, ref);
    const auto it = info.attributes.find(attributeName);
    const InstanceValue before = it != info.attributes.end() ? it->second : InstanceValue{};
    if (value.type == InstanceValue::Type::Nil) {
        info.attributes.erase(attributeName);
    } else {
        info.attributes[attributeName] = value;
    }
    if (!sameValue(before, value)) signals::attributeChanged(ecs, ref, attributeName);
}

std::map<std::string, InstanceValue> attributes(ECS& ecs, InstanceRef ref) {
    const InstanceInfo* info = findInfo(ecs, ref);
    return info != nullptr ? info->attributes : std::map<std::string, InstanceValue>{};
}

bool addTag(ECS& ecs, InstanceRef ref, const std::string& tag) {
    if (!isAlive(ecs, ref) || hasTag(ecs, ref, tag)) return false;
    infoFor(ecs, ref).tags.push_back(tag);
    signals::tagChanged(ecs, ref, tag, true);
    return true;
}

bool removeTag(ECS& ecs, InstanceRef ref, const std::string& tag) {
    if (!hasTag(ecs, ref, tag)) return false;
    std::erase(infoFor(ecs, ref).tags, tag);
    signals::tagChanged(ecs, ref, tag, false);
    return true;
}

bool hasTag(ECS& ecs, InstanceRef ref, const std::string& tag) {
    const InstanceInfo* info = findInfo(ecs, ref);
    return info != nullptr && std::find(info->tags.begin(), info->tags.end(), tag) != info->tags.end();
}

std::vector<std::string> tags(ECS& ecs, InstanceRef ref) {
    const InstanceInfo* info = findInfo(ecs, ref);
    return info != nullptr ? info->tags : std::vector<std::string>{};
}

std::vector<InstanceRef> tagged(ECS& ecs, const std::string& tag) {
    std::vector<EntityId> found;
    for (auto [e, info] : ecs.raw().view<InstanceInfo>().each()) {
        if (std::find(info.tags.begin(), info.tags.end(), tag) != info.tags.end() && !isDetached(ecs, e)) {
            found.push_back(e);
        }
    }
    std::sort(found.begin(), found.end(),
              [](EntityId a, EntityId b) { return entt::to_entity(a) < entt::to_entity(b); });
    std::vector<InstanceRef> refs;
    for (EntityId e : found) refs.push_back(refOf(ecs, e));
    return refs;
}

bool isInWorld(ECS& ecs, EntityId entity) {
    if (!valid(ecs, entity)) return false;
    EntityId at = entity;
    for (int guard = 0; guard < 1024; ++guard) {
        if (entityClass(ecs, at) == "Workspace") return true;
        const EntityId p = parentEntity(ecs, at);
        if (p == kNullEntity) {
            const auto* info = ecs.tryGetComponent<InstanceInfo>(at);
            return !(info != nullptr && info->detached) && !isServiceClass(entityClass(ecs, at));
        }
        at = p;
    }
    return true;
}

void fitPartCollider(ECS& ecs, EntityId entity) {
    if (!valid(ecs, entity) || ecs.tryGetComponent<MeshSource>(entity) == nullptr) return;
    const glm::vec3 size = glm::abs(worldPose(ecs, entity).scale * sizeFactor(ecs, entity));
    const auto* info = ecs.tryGetComponent<InstanceInfo>(entity);
    std::string shapeName = "Block";
    if (info != nullptr) {
        const auto it = info->properties.find("Shape");
        if (it != info->properties.end()) shapeName = it->second.text;
    }
    ColliderShape shape;
    if (shapeName == "Ball") {
        shape.kind = ColliderShapeKind::Sphere;
        shape.params = {std::max(std::min({size.x, size.y, size.z}) * 0.5f, 0.01f), 0.0f, 0.0f};
    } else {
        shape.kind = ColliderShapeKind::Box;
        shape.params = glm::max(size * 0.5f, glm::vec3(0.01f));
    }
    ecs.raw().emplace_or_replace<ColliderShape>(entity, shape);
    if (ecs.tryGetComponent<PhysicsMaterial>(entity) == nullptr) ecs.addComponent<PhysicsMaterial>(entity, PhysicsMaterial{});
    const bool anchored = getAnchored(ecs, entity).boolean;
    auto& body = ecs.raw().get_or_emplace<RigidBody>(entity);
    if (body.joltBodyId == RigidBody::kInvalidBodyId) {
        body.motionType = anchored ? RigidBodyMotionType::Static : RigidBodyMotionType::Dynamic;
    }
}

bool canCollide(ECS& ecs, EntityId entity) {
    const auto* info = ecs.tryGetComponent<InstanceInfo>(entity);
    if (info == nullptr) return true;
    const auto it = info->properties.find("CanCollide");
    return it == info->properties.end() || it->second.boolean;
}

void updateWorldPresence(ECS& ecs, EntityId entity) {
    if (!valid(ecs, entity)) return;
    const bool inWorld = isInWorld(ecs, entity);
    std::function<void(EntityId, int)> apply = [&](EntityId e, int depth) {
        if (depth > 512 || hidden(ecs, e)) return;
        if (auto* r = ecs.tryGetComponent<Renderable>(e)) {
            if (!inWorld) {
                ensureInfo(ecs, e);
                r->visible = false;
            } else if (ecs.tryGetComponent<InstanceInfo>(e) != nullptr) {
                r->visible = r->baseColor.a > 0.001f;
            }
        }
        if (const auto* h = ecs.tryGetComponent<Hierarchy>(e)) {
            const std::vector<EntityId> kids = h->children;
            for (EntityId child : kids) {
                if (valid(ecs, child)) apply(child, depth + 1);
            }
        }
    };
    apply(entity, 0);
}

Pose worldPose(ECS& ecs, EntityId entity) {
    std::vector<EntityId> chain;
    for (EntityId at = entity; valid(ecs, at) && chain.size() < 1024; at = parentEntity(ecs, at)) chain.push_back(at);
    Pose pose;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const auto* t = ecs.tryGetComponent<Transform>(*it);
        if (t == nullptr) continue;
        pose.position = pose.position + pose.rotation * (pose.scale * t->position);
        pose.rotation = pose.rotation * t->rotation;
        pose.scale = pose.scale * t->scale;
    }
    return pose;
}

void markBodyMoved(ECS& ecs, EntityId entity, bool resetVelocity) {
    std::function<void(EntityId, int)> mark = [&](EntityId e, int depth) {
        if (depth > 512) return;
        if (const auto* body = ecs.tryGetComponent<RigidBody>(e); body != nullptr && body->joltBodyId != RigidBody::kInvalidBodyId) {
            auto& write = ecs.raw().get_or_emplace<PhysicsPoseWrite>(e);
            write.resetVelocity = write.resetVelocity || resetVelocity;
        }
        if (const auto* h = ecs.tryGetComponent<Hierarchy>(e)) {
            for (EntityId child : h->children) {
                if (valid(ecs, child)) mark(child, depth + 1);
            }
        }
    };
    if (valid(ecs, entity)) mark(entity, 0);
}

void teleport(ECS& ecs, EntityId entity, glm::vec3 position, glm::quat rotation, bool resetVelocity) {
    setWorldPose(ecs, entity, position, rotation);
    markBodyMoved(ecs, entity, resetVelocity);
}

bool weldParts(ECS& ecs, EntityId weld, EntityId& part0, EntityId& part1) {
    const auto* info = ecs.tryGetComponent<InstanceInfo>(weld);
    if (info == nullptr || info->className != "WeldConstraint" || !isInWorld(ecs, weld)) return false;
    auto stored = [&](const char* name) -> const InstanceValue* {
        const auto it = info->properties.find(name);
        return it != info->properties.end() ? &it->second : nullptr;
    };
    if (const InstanceValue* enabled = stored("Enabled"); enabled != nullptr && !enabled->boolean) return false;
    const InstanceValue* a = stored("Part0");
    const InstanceValue* b = stored("Part1");
    if (a == nullptr || b == nullptr || a->type != InstanceValue::Type::Instance || b->type != InstanceValue::Type::Instance) {
        return false;
    }
    part0 = entityOf(ecs, a->ref);
    part1 = entityOf(ecs, b->ref);
    auto usable = [&](EntityId p) {
        return valid(ecs, p) && classIsA(entityClass(ecs, p), "BasePart") && isInWorld(ecs, p);
    };
    return part0 != part1 && usable(part0) && usable(part1);
}

std::vector<EntityId> weldedAssembly(ECS& ecs, EntityId part) {
    std::vector<EntityId> assembly{part};
    std::vector<std::pair<EntityId, EntityId>> links;
    for (auto [weld, info] : ecs.raw().view<InstanceInfo>().each()) {
        EntityId a = kNullEntity, b = kNullEntity;
        if (info.className == "WeldConstraint" && weldParts(ecs, weld, a, b)) links.emplace_back(a, b);
    }
    for (size_t i = 0; i < assembly.size(); ++i) {
        for (const auto& [a, b] : links) {
            const EntityId other = a == assembly[i] ? b : b == assembly[i] ? a : kNullEntity;
            if (other != kNullEntity && std::find(assembly.begin(), assembly.end(), other) == assembly.end()) {
                assembly.push_back(other);
            }
        }
    }
    return assembly;
}

void setWorldPose(ECS& ecs, EntityId entity, glm::vec3 position, glm::quat rotation) {
    auto* transform = ecs.tryGetComponent<Transform>(entity);
    if (transform == nullptr) return;
    const EntityId p = parentEntity(ecs, entity);
    if (p == kNullEntity) {
        transform->position = position;
        transform->rotation = rotation;
        return;
    }
    const Pose parentPose = worldPose(ecs, p);
    const glm::quat inverseRotation = glm::inverse(parentPose.rotation);
    const glm::vec3 safeScale = glm::max(glm::abs(parentPose.scale), glm::vec3(1e-6f)) *
                                glm::sign(parentPose.scale + glm::vec3(1e-9f));
    transform->rotation = inverseRotation * rotation;
    transform->position = (inverseRotation * (position - parentPose.position)) / safeScale;
}

} // namespace instances
} // namespace engine::core
