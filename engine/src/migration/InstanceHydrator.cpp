#include "migration/InstanceHydrator.hpp"

#include <algorithm>
#include <cmath>

#include "core/Components.hpp"
#include "core/Hierarchy.hpp"
#include "core/InstanceTree.hpp"
#include "migration/PropertyDecoder.hpp"

namespace engine::migration {
namespace {

bool isPartClass(const std::string& className) {
    // Roblox's BasePart family. TrussPart/CornerWedgePart are deliberately
    // absent: they have distinctive geometry no Kronos primitive matches,
    // so importing them as boxes would misrepresent the place rather than
    // approximate it.
    return className == "Part" || className == "WedgePart" || className == "MeshPart" ||
            className == "UnionOperation" || className == "SpawnLocation" || className == "Seat" ||
            className == "VehicleSeat";
}

bool isLightClass(const std::string& className) {
    return className == "PointLight" || className == "SpotLight" || className == "SurfaceLight";
}

bool isScriptClass(const std::string& className) {
    return className == "Script" || className == "LocalScript" || className == "ModuleScript";
}

bool isGroupClass(const std::string& className) {
    return className == "Model" || className == "Folder" || className == "Workspace" ||
            className == "ReplicatedStorage" || className == "ReplicatedFirst" || className == "ServerStorage" ||
            className == "ServerScriptService" || className == "StarterPlayer" || className == "StarterGui" ||
            className == "StarterPack" || className == "StarterPlayerScripts" ||
            className == "StarterCharacterScripts" || className == "Lighting" || className == "SoundService" ||
            className == "Teams" || className == "Configuration" || className == "Accessory" ||
            className == "Tool";
}

// Enum item names for the tokens the hydrator stores; values match Roblox.
std::string enumItemName(const std::string& enumType, int value) {
    static const std::pair<int, const char*> kMaterial[] = {
        {256, "Plastic"}, {272, "SmoothPlastic"}, {288, "Neon"}, {512, "Wood"}, {528, "WoodPlanks"},
        {784, "Marble"}, {800, "Slate"}, {816, "Concrete"}, {832, "Granite"}, {848, "Brick"}, {864, "Pebble"},
        {880, "Cobblestone"}, {1040, "CorrodedMetal"}, {1056, "DiamondPlate"}, {1072, "Foil"}, {1088, "Metal"},
        {1280, "Grass"}, {1296, "Sand"}, {1312, "Fabric"}, {1536, "Ice"}, {1568, "Glass"}, {1584, "ForceField"},
        {1792, "Air"}, {2048, "Water"}};
    static const std::pair<int, const char*> kPartType[] = {
        {0, "Ball"}, {1, "Block"}, {2, "Cylinder"}, {3, "Wedge"}, {4, "CornerWedge"}};
    if (enumType == "Material") {
        for (const auto& [v, item] : kMaterial) {
            if (v == value) return item;
        }
    } else if (enumType == "PartType") {
        for (const auto& [v, item] : kPartType) {
            if (v == value) return item;
        }
    }
    return {};
}

// Copies the properties scripts read (Anchored, Material, Value, ...) into
// the entity's InstanceInfo.
void storeProperties(const ImportedInstance& node, core::ECS& ecs, core::EntityId entity) {
    using core::InstanceValue;
    const core::InstanceRef ref = core::instances::refOf(ecs, entity);
    auto set = [&](const char* property, const InstanceValue& value) {
        if (const core::PropertyDef* def = core::instances::findProperty(node.className, property)) {
            core::instances::setProperty(ecs, ref, *def, value);
        }
    };
    if (core::instances::classIsA(node.className, "BasePart")) {
        set("Anchored", InstanceValue::ofBool(decodeBool(node.properties, "Anchored", false)));
        set("CanCollide", InstanceValue::ofBool(decodeBool(node.properties, "CanCollide", true)));
        const int material = decodeInt(node.properties, "Material", 256);
        const std::string materialName = enumItemName("Material", material);
        if (!materialName.empty()) set("Material", InstanceValue::ofEnum("Material", materialName, material));
        if (node.className == "Part") {
            const int shape = hasProperty(node.properties, "shape") ? decodeInt(node.properties, "shape", 1)
                                                                     : decodeInt(node.properties, "Shape", 1);
            const std::string shapeName = enumItemName("PartType", shape);
            // Stored directly: the hydrator already picked the mesh.
            if (!shapeName.empty()) {
                ecs.tryGetComponent<core::InstanceInfo>(entity)->properties["Shape"] =
                    InstanceValue::ofEnum("PartType", shapeName, shape);
            }
        }
    }
    if (!hasProperty(node.properties, "Value")) return;
    const core::PropertyDef* value = core::instances::findProperty(node.className, "Value");
    if (value == nullptr) return;
    switch (value->type) {
        case core::PropertyType::Number:
            set("Value", InstanceValue::ofNumber(decodeFloat(node.properties, "Value", 0.0f)));
            break;
        case core::PropertyType::String: set("Value", InstanceValue::ofString(decodeString(node.properties, "Value"))); break;
        case core::PropertyType::Bool: set("Value", InstanceValue::ofBool(decodeBool(node.properties, "Value", false))); break;
        case core::PropertyType::Vector3:
            set("Value", InstanceValue::ofVector3(decodeVector3(node.properties, "Value")));
            break;
        case core::PropertyType::Color3:
            set("Value", InstanceValue::ofColor3(decodeColor3(node.properties, "Value", glm::vec3(0.0f))));
            break;
        default: break;
    }
}

// Roblox's Part.Shape token: 0 Ball, 1 Block, 2 Cylinder. Written as
// <token name="shape">N</token>.
uint32_t meshForPart(const ImportedInstance& node, const HydrationMeshes& meshes) {
    if (node.className == "Part") {
        const int shape = hasProperty(node.properties, "shape") ? decodeInt(node.properties, "shape", 1)
                                                                 : decodeInt(node.properties, "Shape", 1);
        if (shape == 0 && meshes.capsule != core::Renderable::kInvalidHandle) return meshes.capsule;
        if (shape == 2 && meshes.cylinder != core::Renderable::kInvalidHandle) return meshes.cylinder;
    }
    return meshes.box;
}

} // namespace

bool InstanceHydrator::isSupportedClass(const std::string& className) {
    return isPartClass(className) || isLightClass(className) || isScriptClass(className) || isGroupClass(className) ||
           core::instances::findClass(className) != nullptr;
}

HydrationResult InstanceHydrator::hydrate(const std::vector<ImportedInstance>& tree, core::ECS& ecs,
                                           const HydrationMeshes& meshes, const HydrationOptions& options) const {
    HydrationResult result;
    for (const ImportedInstance& root : tree) {
        const core::EntityId entity = hydrateNode(root, ecs, core::kNullEntity, WorldTransform{}, meshes, options, result);
        if (entity != core::kNullEntity) core::instances::updateWorldPresence(ecs, entity);
    }
    return result;
}

core::EntityId InstanceHydrator::hydrateNode(const ImportedInstance& node, core::ECS& ecs, core::EntityId parent,
                                              const WorldTransform& parentWorld, const HydrationMeshes& meshes,
                                              const HydrationOptions& options, HydrationResult& result) const {
    const bool part = isPartClass(node.className);
    const bool light = isLightClass(node.className);
    const bool script = isScriptClass(node.className);
    // Any other class Kronos knows (Folder, IntValue, RemoteEvent, ...) is an
    // entity with no visuals, so scripts can find it.
    const bool group = isGroupClass(node.className) ||
                       (!part && !light && !script && core::instances::findClass(node.className) != nullptr);

    if (!part && !light && !script && !group) {
        ++result.skippedCount;
        result.notes.push_back(node.name + " (" + node.className + "): no Kronos equivalent, skipped");
        // Children are still walked: a supported Part nested under an
        // unsupported container must not be lost with it. It re-parents to
        // the nearest supported ancestor instead.
        for (const ImportedInstance& child : node.children) {
            hydrateNode(child, ecs, parent, parentWorld, meshes, options, result);
        }
        return core::kNullEntity;
    }

    if (group && !options.createGroupEntities) {
        for (const ImportedInstance& child : node.children) {
            hydrateNode(child, ecs, parent, parentWorld, meshes, options, result);
        }
        return core::kNullEntity;
    }

    // A service already in the scene (Workspace from an earlier import, say)
    // takes the new children instead of getting a duplicate.
    if (parent == core::kNullEntity) {
        const core::ClassDef* def = core::instances::findClass(node.className);
        if (def != nullptr && def->service) {
            const core::EntityId existing =
                core::instances::entityOf(ecs, core::instances::findService(ecs, node.className));
            if (existing != core::kNullEntity) {
                const core::instances::Pose pose = core::instances::worldPose(ecs, existing);
                const WorldTransform existingWorld{pose.position, pose.rotation, pose.scale};
                for (const ImportedInstance& child : node.children) {
                    hydrateNode(child, ecs, existing, existingWorld, meshes, options, result);
                }
                return existing;
            }
        }
    }

    const core::EntityId entity = ecs.createEntity(node.name);
    result.createdEntities.push_back(entity);
    core::InstanceInfo info;
    info.className = node.className;
    ecs.addComponent<core::InstanceInfo>(entity, std::move(info));

    // --- transform ---------------------------------------------------------
    // Decoded as WORLD-space, which is what a CFrame is. Things without a
    // CFrame (lights, Folders, Models) sit at their parent.
    WorldTransform world = parentWorld;
    if (hasProperty(node.properties, "CFrame") || node.properties.count("CFrame.X") != 0) {
        world.position = decodeCFramePosition(node.properties, "CFrame") * options.studsToUnits;
        world.rotation = decodeCFrameRotation(node.properties, "CFrame");
        world.scale = glm::vec3(1.0f);
    }
    if (part) {
        // The hydration meshes are 1-unit shapes, so scale is the full Size.
        const glm::vec3 size = decodeVector3(node.properties, "size", glm::vec3(4.0f, 1.2f, 2.0f));
        world.scale = glm::max(size * options.studsToUnits, glm::vec3(1e-3f));
    }

    if (auto* transform = ecs.tryGetComponent<core::Transform>(entity)) {
        // Convert world -> parent-local. Exact for the translate/rotate/
        // scale transforms a CFrame can express (no shear), which is why
        // this composes the parts directly instead of decomposing an
        // inverse matrix -- glm::decompose returns a conjugated rotation
        // and would need working around here for no benefit.
        const glm::quat inverseParentRotation = glm::inverse(parentWorld.rotation);
        const glm::vec3 safeParentScale = glm::max(glm::abs(parentWorld.scale), glm::vec3(1e-6f)) *
                                           glm::sign(parentWorld.scale + glm::vec3(1e-9f));
        transform->rotation = inverseParentRotation * world.rotation;
        transform->position = (inverseParentRotation * (world.position - parentWorld.position)) / safeParentScale;
        transform->scale = world.scale / safeParentScale;
    }

    // --- renderable --------------------------------------------------------
    if (part) {
        ++result.partCount;
        auto& renderable = ecs.addComponent<core::Renderable>(entity);
        renderable.meshHandle = meshForPart(node, meshes);

        const glm::vec3 color = hasProperty(node.properties, "Color3uint8")
                                     ? decodeColor3(node.properties, "Color3uint8", glm::vec3(0.64f))
                                     : decodeColor3(node.properties, "Color", glm::vec3(0.64f));
        // Roblox Transparency is 0 opaque .. 1 invisible; Kronos alpha is
        // the opposite convention.
        const float transparency = std::clamp(decodeFloat(node.properties, "Transparency", 0.0f), 0.0f, 1.0f);
        renderable.baseColor = glm::vec4(color, 1.0f - transparency);

        // Reflectance is Roblox's single "how mirror-like" scalar. It is
        // not metalness, but it is the only PBR-adjacent signal a legacy
        // part carries, so it drives both roughness and a little metallic
        // rather than being dropped.
        const float reflectance = std::clamp(decodeFloat(node.properties, "Reflectance", 0.0f), 0.0f, 1.0f);
        renderable.roughness = std::clamp(0.85f - reflectance * 0.7f, 0.05f, 1.0f);
        renderable.metallic = std::clamp(reflectance * 0.6f, 0.0f, 1.0f);
        renderable.castsShadow = decodeBool(node.properties, "CastShadow", true);
        renderable.visible = transparency < 0.999f;

        auto& meshSource = ecs.addComponent<core::MeshSource>(entity);
        meshSource.kind = core::MeshSourceKind::Box;
        meshSource.params = glm::vec3(0.5f);
    }

    // --- light -------------------------------------------------------------
    if (light) {
        ++result.lightCount;
        auto& lightComponent = ecs.addComponent<core::Light>(entity);
        lightComponent.color = decodeColor3(node.properties, "Color", glm::vec3(1.0f));
        lightComponent.intensity = std::max(decodeFloat(node.properties, "Brightness", 1.0f), 0.0f);
        lightComponent.radius = std::max(decodeFloat(node.properties, "Range", 8.0f) * options.studsToUnits, 0.01f);
        lightComponent.enabled = decodeBool(node.properties, "Enabled", true);
    }

    // --- script ------------------------------------------------------------
    if (script) {
        ++result.scriptCount;
        auto& scriptComponent = ecs.addComponent<core::Script>(entity);
        scriptComponent.source = decodeString(node.properties, "Source");
        scriptComponent.autoRun = options.autoRunImportedScripts;
    }

    if (group) ++result.groupCount;
    storeProperties(node, ecs, entity);

    // --- hierarchy ---------------------------------------------------------
    // Parented AFTER the transform is written. core::hierarchy::setParent
    // preserves world position by rewriting the child's local transform,
    // so parenting first and then overwriting the transform would discard
    // exactly that correction.
    if (parent != core::kNullEntity) {
        core::hierarchy::setParent(ecs, entity, parent);
    }
    if (part) core::instances::fitPartCollider(ecs, entity);

    for (const ImportedInstance& child : node.children) {
        hydrateNode(child, ecs, entity, world, meshes, options, result);
    }
    return entity;
}

} // namespace engine::migration
