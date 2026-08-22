#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/ECS.hpp"
#include "migration/InstanceTreeBuilder.hpp"

namespace engine::migration {

// Mesh handles the hydrator needs to give imported parts something to
// draw. Registered by the caller (which owns the MeshLibrary and the
// device), exactly as studio::spawnPropAuthoring already takes them.
struct HydrationMeshes {
    uint32_t box = core::Renderable::kInvalidHandle;
    uint32_t capsule = core::Renderable::kInvalidHandle;
    uint32_t cylinder = core::Renderable::kInvalidHandle;
};

struct HydrationOptions {
    // Roblox measures in studs. 1 stud is 0.28 m by Roblox's own
    // convention, but Kronos scenes are authored at 1 unit = 1 m and a
    // place imported at 0.28 makes every part tiny next to existing
    // content. Defaulting to 1:1 keeps an imported place the same
    // apparent size it was authored at; set this to 0.28f for physically
    // faithful proportions.
    float studsToUnits = 1.0f;
    // Instances Roblox uses purely for organisation still become
    // entities, so the hierarchy an author built is preserved and a Model
    // can be moved as a unit.
    bool createGroupEntities = true;
    // Scripts arrive as a core::Script component with autoRun off. An
    // imported script is untrusted and almost always references Roblox
    // APIs Kronos does not have (see LuauApiCompatibility), so running it
    // on import would spray errors into the log at exactly the moment the
    // author is trying to read the import report.
    bool autoRunImportedScripts = false;
};

struct HydrationResult {
    // Every entity created, in creation order. The caller owns undoing
    // this -- see StudioApp's import command.
    std::vector<core::EntityId> createdEntities;
    size_t partCount = 0;
    size_t lightCount = 0;
    size_t scriptCount = 0;
    size_t groupCount = 0;
    size_t skippedCount = 0; // instances with no Kronos representation
    std::vector<std::string> notes;
};

// Turns an ImportedInstance tree into live ECS entities.
//
// This is the step InstanceTreeBuilder.hpp's own TODO described as
// needing "the Instance-over-ECS translation layer": rather than build a
// general Instance/DataModel view (a much larger design question), this
// maps the specific Roblox classes that have a real Kronos equivalent --
// BasePart family, PointLight/SpotLight/SurfaceLight, Model/Folder and
// the service containers, Script/LocalScript/ModuleScript -- and reports
// everything else as skipped rather than silently dropping it.
//
// Deliberately free of Vulkan: it consumes already-registered mesh
// handles and writes components. That keeps the whole mapping testable
// against a bare ECS, which is where the coordinate and hierarchy bugs
// actually live.
class InstanceHydrator {
public:
    [[nodiscard]] HydrationResult hydrate(const std::vector<ImportedInstance>& tree, core::ECS& ecs,
                                           const HydrationMeshes& meshes,
                                           const HydrationOptions& options = {}) const;

    // True when this class has a real mapping for `className`.
    [[nodiscard]] static bool isSupportedClass(const std::string& className);

private:
    // The accumulated world transform of the parent chain.
    //
    // Carried down the recursion rather than recomputed, because Roblox
    // CFrames are WORLD-absolute while core::Transform is parent-LOCAL
    // and core::hierarchy::setParent does not convert between them (only
    // unparent() world-preserves). Writing a world CFrame straight into a
    // Transform and then parenting composes it with its ancestors a
    // second time, so every nested part lands at the wrong place --
    // invisible whenever the parents happen to sit at the origin, which
    // is most Models, and badly wrong the moment one does not.
    struct WorldTransform {
        glm::vec3 position{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    core::EntityId hydrateNode(const ImportedInstance& node, core::ECS& ecs, core::EntityId parent,
                                const WorldTransform& parentWorld, const HydrationMeshes& meshes,
                                const HydrationOptions& options, HydrationResult& result) const;
};

} // namespace engine::migration
