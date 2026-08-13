#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/SceneFile.hpp"

namespace engine::studio {

// Captures the live Studio ECS into a core::SceneFile and rebuilds a live
// ECS from one -- the GPU/ECS-touching half of scene persistence
// (core::SceneFile itself is pure data, see its header comment). Owns no
// state that outlives one call except the small bookkeeping below;
// StudioApp owns the real ecs_/meshLibrary_/viewportPanel_ this operates
// on and passes them in explicitly each call, the same "manager doesn't
// own the resources it operates on" shape as PluginManager.
//
// loadScene() destroys every entity currently in the ECS and rebuilds
// from the file -- a full *replace*, not a merge. There's no "load scene
// B into the middle of scene A" operation; combining scenes isn't
// something Studio has any UI or use case for yet.
//
// "Scene tabs" here means a list of *file paths*, not N independently-
// live ECS worlds held in memory at once -- switching tabs saves the
// current scene (if a path is set) then loads the target tab's file into
// the one shared ECS/MeshLibrary/camera StudioApp owns. Giving each tab
// its own live MeshLibrary/Renderer target would be a substantially
// larger architectural change (every GPU-owning subsystem StudioApp has
// would need to become per-tab) than this pass's scope justifies.
//
// Dirty tracking (drives autosave, see tickAutosave()) is a *coarse*
// heuristic, stated plainly: automatic on any entity-count change
// (covers every spawn/delete path, from any plugin, for free -- see
// tickAutosave()'s body) plus whatever explicitly calls markDirty().
// In-place edits that don't change entity count (dragging a gizmo,
// tweaking a material slider) are only covered once their call site
// calls markDirty() -- StudioApp wires this to UndoStack gaining a new
// entry (see StudioApp.cpp), which covers whatever UndoStack::push()
// covers today (InspectorPanel's Transform fields, per UndoStack.hpp's
// own stated scope) and grows automatically as more panels adopt
// UndoStack. This is real, working dirty tracking, just not
// field-by-field comprehensive on day one -- the same kind of stated
// scope boundary as UndoStack.hpp's own header comment.
class SceneManager {
public:
    // Walks every entity with Transform+Name (see core::SceneEntityRecord's
    // comment on why an unnamed entity can't round-trip) into a
    // core::SceneFile and writes it to `path`. On success, `path` becomes
    // currentScenePath() and the dirty flag clears.
    [[nodiscard]] bool saveScene(const std::string& path, core::ECS& ecs, const core::Camera& camera);

    // Destroys every entity currently in `ecs` and rebuilds from `path`:
    // regenerates each entity's mesh (procedural regeneration via
    // core::Mesh::createX, or a real core::loadObj() re-parse for
    // MeshSourceKind::Obj) using the Vulkan handles below, restores every
    // component core::SceneEntityRecord carries, and restores `camera`.
    // Returns false (leaving `ecs` unchanged) if the file can't be read
    // at all; a per-entity mesh regeneration failure (e.g. the source
    // .obj moved) is not fatal to the whole load -- that one entity is
    // created without a valid meshHandle instead, logged to stderr, same
    // "fail soft, don't abort the whole load" precedent as
    // AnimationClip/Prefab's loaders. On success, `path` becomes
    // currentScenePath() and the dirty flag clears.
    [[nodiscard]] bool loadScene(const std::string& path, core::ECS& ecs, core::MeshLibrary& meshLibrary,
                                  VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                  core::Camera& camera);

    // Clears `ecs` and resets bookkeeping to "new, unsaved scene".
    void newScene(core::ECS& ecs);

    [[nodiscard]] const std::string& currentScenePath() const { return currentScenePath_; }
    [[nodiscard]] bool isDirty() const { return dirty_; }
    void markDirty() { dirty_ = true; }

    // Tabs: each entry is a real scene file path (an unsaved scene isn't
    // added until its first real save). activeTabIndex() indexes into
    // this; -1 means "no tab is the active one" (a brand new, never-
    // saved scene). Purely bookkeeping -- StudioApp's File/tab-strip UI
    // drives saveScene()/loadScene() itself when switching; this class
    // doesn't call them on its own.
    [[nodiscard]] std::vector<std::string>& openScenePaths() { return openScenePaths_; }
    [[nodiscard]] int activeTabIndex() const { return activeTabIndex_; }
    void setActiveTabIndex(int index) { activeTabIndex_ = index; }

    // Periodic autosave -- ticked once per frame from StudioApp. Writes
    // to recoveryPathFor(currentScenePath_), never over the real file, so
    // a bad autosave can never clobber the user's last deliberate Save.
    // No-ops (and resets the timer) whenever there's no current scene
    // path or nothing is dirty by the heuristic above -- see class
    // comment.
    void tickAutosave(float dt, core::ECS& ecs, const core::Camera& camera);

    [[nodiscard]] static std::string recoveryPathFor(const std::string& scenePath);
    [[nodiscard]] static bool hasRecoveryFile(const std::string& scenePath);

    static constexpr float kAutosaveIntervalSeconds = 60.0f;

    // The pure in-memory half of saveScene() (that method is just this
    // plus a file write + bookkeeping), made public in Sprint 13
    // ("Publishing & Game Packaging") for its second real caller:
    // studio::plugins::PublishingPanel needs a real, in-memory
    // core::SceneFile to bundle into a publishing::WorldPackage without
    // round-tripping through a temp file on disk first.
    [[nodiscard]] core::SceneFile captureScene(core::ECS& ecs, const core::Camera& camera) const;

private:
    std::string currentScenePath_;
    bool dirty_ = false;
    std::vector<std::string> openScenePaths_;
    int activeTabIndex_ = -1;

    float autosaveTimer_ = 0.0f;
    size_t lastSeenEntityCount_ = 0;
};

} // namespace engine::studio
