#pragma once

#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <glm/glm.hpp>

#include "core/Components.hpp"
#include "core/Mesh.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/plugins/TerrainEditorPlugin.hpp"

namespace engine::studio::plugins {

// The four real primitive shapes this plugin can spawn. Cube/Sphere have
// a MeshSourceKind (Box/Capsule) and round-trip fully through Save
// Scene; Cylinder/Wedge don't (see Mesh::createCylinder()'s own comment)
// -- both spawn and render correctly for the rest of the session, they
// just won't reload with their exact shape after a save/reopen. Flagged
// honestly in the panel itself, not silently hidden.
enum class BlockShape { Cube, Sphere, Cylinder, Wedge };

// Kronos ("Block Builder"): a real, Roblox-Studio-style block placement
// tool -- found missing during a live playtest ("add a custom block/stud
// maker"). Deliberately scoped to placement, not full mesh editing (see
// docs/MODEL_MAKER_AI_ROADMAP.md for the larger, not-yet-started
// vertex/edge/face editing vision this is the first phase of): place a
// primitive at a snapped position, then the viewport's own existing
// translate/rotate/scale gizmo (ViewportPanel.cpp's ImGuizmo::Manipulate,
// already generic over any selected entity) does the real drag-resize/
// rotate -- no new gizmo/widget code needed for that part, it already
// works on any entity with a Transform.
//
// Same authoring-only shape as CreatorToolsPlugin (no live Physics --
// see that class's own comment for why) and the same non-destructive
// snap-to-grid/snap-to-surface pattern (effectiveSpawnPosition()) copied
// from CreatorToolsPlugin::effectiveSpawnPosition() rather than
// reinvented -- this codebase already has two independent copies of this
// exact rounding math (CreatorToolsPlugin, AlignPlugin), this is a
// third; there's no shared free function to call into instead.
class BlockBuilderPlugin final : public IStudioPlugin {
public:
    BlockBuilderPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                        core::MeshLibrary& meshLibrary, TerrainEditorPlugin& terrainEditor);

    [[nodiscard]] const char* name() const override { return "Block Builder"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    [[nodiscard]] glm::vec3 effectiveSpawnPosition() const;
    core::EntityId spawnBlock(core::ECS& ecs, BlockShape shape);

    core::MeshLibrary* meshLibrary_;
    TerrainEditorPlugin* terrainEditor_;

    uint32_t cubeMesh_ = core::Renderable::kInvalidHandle;
    uint32_t sphereMesh_ = core::Renderable::kInvalidHandle;     // zero-half-height capsule, same trick main.cpp's own "bouncingSphere" already uses
    uint32_t cylinderMesh_ = core::Renderable::kInvalidHandle;
    uint32_t wedgeMesh_ = core::Renderable::kInvalidHandle;

    glm::vec3 spawnPosition_{0.0f, 1.0f, 0.0f};
    int blockSpawnCount_ = 0; // disambiguates entity names ("Cube 3") across repeated spawns, same as CreatorToolsPlugin's propSpawnCount_

    glm::vec3 spawnRotationDegrees_{0.0f, 0.0f, 0.0f};
    glm::vec3 spawnScale_{1.0f, 1.0f, 1.0f};
    glm::vec4 spawnColor_{0.70f, 0.70f, 0.75f, 1.0f};

    bool snapToGridEnabled_ = false;
    float gridSize_ = 1.0f;
    bool snapToSurfaceEnabled_ = false;
};

} // namespace engine::studio::plugins
