#pragma once

#include <memory>

#include "core/AnimationDatabase.hpp"
#include "core/CatalogueDatabase.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ParticleSystem.hpp"
#include "core/PerformanceDiagnostics.hpp"
#include "core/ProcessStats.hpp"
#include "core/Profiler.hpp"
#include "core/ProjectFile.hpp"
#include "core/Renderer.hpp"
#include "core/RiggedMesh.hpp"
#include "core/Texture.hpp"
#include "core/Window.hpp"
#include "net/NetworkSession.hpp"
#include "studio/Notification.hpp"
#include "studio/OffscreenTarget.hpp"
#include "studio/PluginManager.hpp"
#include "studio/SceneManager.hpp"
#include "studio/UndoStack.hpp"
#include "studio/panels/DebugConsolePanel.hpp"
#include "studio/panels/ExplorerPanel.hpp"
#include "studio/panels/InspectorPanel.hpp"
#include "studio/panels/SceneSearchPanel.hpp"
#include "studio/panels/ScriptEditorPanel.hpp"
#include "studio/panels/StatsPanel.hpp"
#include "studio/panels/ViewportPanel.hpp"

struct VkDescriptorPool_T;
using VkDescriptorPool = VkDescriptorPool_T*;
struct ImDrawData;

namespace engine::studio::plugins {
class AudioPreviewPlugin;
class TexturePreviewPlugin;
class AvatarPreviewer;
class CataloguePanel;
class UploadAvatarItemPlugin;
class AnimationPreviewerPlugin;
class UploadAnimationPlugin;
class PhysicsPreviewPlugin;
class ShopPlugin;
class TerrainEditorPlugin;
class CreatorToolsPlugin;
class MaterialPlugin;
class ParticleEditorPlugin;
class PublishingPanel;
class TrailerPanel;
}

namespace engine::studio {

// The desktop Studio shell (docs/ARCHITECTURE.md §5/§9): Explorer,
// Inspector, Viewport, Script Editor, docked via Dear ImGui. Owns its own
// core::Window/core::Renderer/core::ECS -- deliberately the *same* classes
// engine_runtime uses, not a parallel editor-only renderer, which is what
// makes Principle 4 ("what you see in Studio is what ships") a build-graph
// fact rather than a design intention someone could accidentally violate
// later.
//
// The Viewport panel shows the *real* rendered scene -- core::Renderer's
// PBR pipeline, drawn into an offscreen target (OffscreenTarget.hpp) via
// Renderer's pre-pass hook, then displayed with ImGui::Image(). Studio's
// own swapchain background stays a plain clear (Renderer::setScene() is
// never called here -- see that method's doc comment); only the docked
// Viewport panel's region actually shows the 3D scene, matching how a
// real editor's viewport is one panel among several, not the whole window.
//
// Scene/project persistence (File menu -> New/Save/Load Scene, Save/Open
// Project; see SceneManager.hpp) is real -- what this class still does
// NOT do: run Physics/Scripting/Audio as a live session (Play Solo per
// §5's workflow example spins up a full runtime session -- not wired
// here). initialize() always starts from buildBringUpScene()'s
// placeholder ECS entities (the same ground-plane/material-grid dressing
// engine_runtime's main.cpp creates) regardless of any project that was
// open last session -- there is no "reopen last project on launch"
// behavior yet, only File > Open Project to load one explicitly.
class StudioApp {
public:
    StudioApp();
    ~StudioApp();

    StudioApp(const StudioApp&) = delete;
    StudioApp& operator=(const StudioApp&) = delete;

    [[nodiscard]] bool initialize();
    void shutdown();
    void run();

    // Sprint 7 ("Studio UI Revamp") task category 6 -- real, public so
    // any plugin/panel can push a real toast (save success/failure,
    // build errors) through the one shared queue instead of each one
    // inventing its own status-text convention.
    [[nodiscard]] NotificationCenter& notifications() { return notifications_; }

private:
    void beginFrame();
    void drawDockspace();
    void endFrame();
    [[nodiscard]] bool initImGuiVulkanBackend();

    void buildBringUpScene();
    void drawFileMenu();
    void drawPendingFileActionPopup();
    void drawSceneTabsBar();
    void drawRecoveryBanner();
    // Kronos ("First-Launch Experience"): a real welcome panel, shown
    // only the one time drawDockspace()'s own DockBuilder detects a
    // genuinely first-ever launch (DockBuilderGetNode() == nullptr) --
    // reuses that existing detection rather than a second marker-file
    // mechanism. See .cpp for the real buttons (Open Default Project /
    // View Quickstart).
    void drawWelcomePanel();
    // Kronos (Alpha Completion Checklist, "Project System Finalization" --
    // "auto-backup"): see projectAutosaveTimer_'s own comment.
    void tickProjectAutosave(float dt);
    // Saves whatever scene is currently open (if any path is set) before
    // loading `path` -- the "switch" half of scene tabs (see
    // SceneManager.hpp's class comment on what "tabs" means here) and
    // also what a plain File > Load Scene... goes through, so loading a
    // scene always adds/selects it as a tab too rather than being a
    // separate, tab-unaware code path.
    void switchToScene(const std::string& path);

    core::Window window_;
    core::Renderer renderer_;
    core::ECS ecs_;
    core::MeshLibrary meshLibrary_;
    core::TextureLibrary textureLibrary_;
    // Shared the same way meshLibrary_/textureLibrary_ are -- any plugin
    // with real skinned/rigged content (today: just AnimationPreviewerPlugin's
    // demo body) registers its RiggedMesh instances here rather than each
    // owning a separate GPU-resource pool. Destroyed in shutdown() the
    // same way meshLibrary_/textureLibrary_ are.
    core::RiggedMeshLibrary riggedMeshLibrary_;
    // Ticked every frame in run() (see its comment) so the Particle
    // Editor plugin gets a real live preview -- the one deliberate
    // exception to "Studio's bring-up scene is static, not simulated"
    // (see class comment: "no Physics here... not simulated bodies"):
    // particle simulation doesn't depend on Physics/Scripting stepping
    // and previewing an effect live is the entire point of a particle
    // editor, so ticking just this one lightweight subsystem is a small,
    // deliberate, documented exception rather than a silent scope creep.
    core::ParticleSystem particleSystem_;
    OffscreenTarget viewportTarget_;

    panels::ExplorerPanel explorerPanel_;
    panels::InspectorPanel inspectorPanel_;
    panels::ViewportPanel viewportPanel_;
    panels::ScriptEditorPanel scriptEditorPanel_;
    panels::StatsPanel statsPanel_;
    panels::SceneSearchPanel sceneSearchPanel_;
    panels::DebugConsolePanel debugConsolePanel_;

    // First-party plugins (Animator, Align & Distribute, Material Editor,
    // ...) are registered here in initialize() -- see PluginManager.hpp's
    // header comment for what "plugin" does and doesn't mean yet.
    PluginManager pluginManager_;

    // Raw, non-owning pointer into a plugin pluginManager_ already owns
    // (via unique_ptr) -- IStudioPlugin has no generic init/shutdown hook
    // (most plugins need neither), but AudioPreviewPlugin owns a real
    // core::Audio (a real miniaudio ma_engine) that does need explicit
    // initialize()/shutdown() calls StudioApp has to make directly, the
    // same way it already does for debugConsolePanel_'s Scripting
    // instance. Set once in initialize(), right after
    // registerPlugin() hands ownership to pluginManager_.
    plugins::AudioPreviewPlugin* audioPreviewPlugin_ = nullptr;
    // Same reasoning, different resource kind: TexturePreviewPlugin owns
    // real Vulkan/VMA resources directly (see its shutdown()'s doc
    // comment) that must be torn down before the device, not left to its
    // destructor's timing.
    plugins::TexturePreviewPlugin* texturePreviewPlugin_ = nullptr;
    // Same pattern -- MaterialPlugin owns a real studio::PreviewScene
    // (Sprint 10's live preview sphere) needing the same explicit
    // renderPreview()/shutdown() calls every other PreviewScene owner gets.
    plugins::MaterialPlugin* materialPlugin_ = nullptr;
    // Same pattern -- ParticleEditorPlugin's own live preview window
    // (Sprint 10) needs the same explicit renderPreview()/shutdown() calls.
    plugins::ParticleEditorPlugin* particleEditorPlugin_ = nullptr;

    // Avatar/Catalogue system (docs task: Avatar Item System, Catalogue
    // Backend/Viewer, Avatar Previewer, Loadout, Upload Pipeline).
    // catalogueDatabase_/catalogueIndex_ are owned here (not by any one
    // plugin) since all three new plugins below need to share the same
    // one -- CataloguePanel searches it, UploadAvatarItemPlugin writes to
    // it, AvatarPreviewer resolves equipped item ids through it.
    core::CatalogueDatabase catalogueDatabase_;
    core::CatalogueIndex catalogueIndex_;
    std::string catalogueDatabasePath_ = "catalogue.json";

    // Animation Upload Pipeline (docs task category 5) -- same "owned
    // here, not by the one plugin that writes to it" reasoning as
    // catalogueDatabase_ above; a future Emote System catalogue read
    // (task category 6) needs it too.
    core::AnimationDatabase animationDatabase_;
    std::string animationDatabasePath_ = "animations.json";

    // Same "raw pointer into what pluginManager_ owns" pattern as
    // audioPreviewPlugin_/texturePreviewPlugin_ above: all three own a
    // studio::PreviewScene (its own OffscreenTarget) that needs an
    // explicit per-frame render hook from the pre-pass callback (see
    // PreviewScene.hpp -- Renderer supports only one PrePassCallback, so
    // every open preview scene's render() call happens back to back
    // inside StudioApp's one callback) and an explicit shutdown() before
    // device teardown.
    plugins::AvatarPreviewer* avatarPreviewer_ = nullptr;
    plugins::CataloguePanel* cataloguePanel_ = nullptr;
    plugins::UploadAvatarItemPlugin* uploadAvatarItemPlugin_ = nullptr;
    plugins::AnimationPreviewerPlugin* animationPreviewerPlugin_ = nullptr;
    plugins::UploadAnimationPlugin* uploadAnimationPlugin_ = nullptr;
    // Same "raw pointer into what pluginManager_ owns" pattern -- see
    // PhysicsPreviewPlugin.hpp's class comment for what Play/Stop does.
    // ViewportPanel reads this directly (debug-draw toggles, recentContacts(),
    // castTestRay()) since physics debug visualization is drawn as part of
    // the viewport overlay, not the plugin's own ImGui panel.
    plugins::PhysicsPreviewPlugin* physicsPreviewPlugin_ = nullptr;
    // Same "raw pointer into what pluginManager_ owns" pattern -- see
    // ShopPlugin.hpp's class comment for why it owns its own sandboxed
    // economy state rather than a live ECS entity's.
    plugins::ShopPlugin* shopPlugin_ = nullptr;
    // Same pattern -- CreatorToolsPlugin's constructor needs a reference
    // to the *same* TerrainEditorPlugin instance (captured here first, so
    // it exists before CreatorToolsPlugin is constructed) rather than
    // owning a second, competing core::Terrain. See
    // CreatorToolsPlugin.hpp's class comment.
    plugins::TerrainEditorPlugin* terrainEditorPlugin_ = nullptr;
    // Same "raw pointer into what pluginManager_ owns" pattern --
    // PublishingPanel's own studio::ThumbnailCameraRig (Sprint 13) needs
    // the same explicit per-frame renderPreview()/shutdown() calls every
    // other real GPU-touching preview owner gets.
    plugins::PublishingPanel* publishingPanel_ = nullptr;
    // Sprint 15 ("TNT-Wars Trailer Production") -- same real "raw
    // pointer into what pluginManager_ owns, needs its own real
    // renderPreview()/shutdown() calls" pattern as publishingPanel_ above.
    plugins::TrailerPanel* trailerPanel_ = nullptr;

    // Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) handled in run() -- see UndoStack.hpp
    // for exactly which edits currently push commands onto this.
    UndoStack undoStack_;

    // Sprint 7 -- see notifications() above.
    NotificationCenter notifications_;

    // Sprint 8 ("Performance Stats & Debug Tools") -- see drawDockspace()'s
    // (well, run()'s) stats composition for how these feed StatsPanel/
    // DiagnosticsPlugin every frame. Public accessors so a future
    // Diagnostics/Profiler-control plugin can reach them without
    // StudioApp having to forward every individual field.
public:
    [[nodiscard]] core::Profiler& profiler() { return profiler_; }
    [[nodiscard]] const core::PerformanceMetrics& lastPerformanceMetrics() const { return lastPerformanceMetrics_; }

private:
    core::Profiler profiler_;
    core::ProcessStatsSampler processStatsSampler_;
    core::PerformanceMetrics lastPerformanceMetrics_;

    // Sprint 11 ("Networking Foundation") task 4 -- the real
    // net::NetworkSession backing plugins::NetworkOverlayPlugin's Host/
    // Join/Stress-Test controls (see that plugin's own class comment).
    // Owned here, not by the plugin, the same "StudioApp owns the
    // resource, the plugin is just the UI over a reference to it"
    // split profiler_/lastPerformanceMetrics_ above already establish
    // for DiagnosticsPlugin.
    net::NetworkSession networkSession_;

    // Scene/project persistence (docs task category 5) -- see
    // SceneManager.hpp for the capture/rebuild logic and what "tabs"/
    // "autosave" mean here. currentProject_/currentProjectPath_ are
    // deliberately lightweight: a project is just a name + a list of
    // scene paths (core/ProjectFile.hpp) -- StudioApp doesn't switch its
    // own ecs_/meshLibrary_ per "opened project", only File > Open
    // Project's scenePaths populate sceneManager_'s tab list.
    SceneManager sceneManager_;
    core::ProjectFile currentProject_;
    std::string currentProjectPath_;
    size_t lastSeenUndoCount_ = 0; // see SceneManager.hpp's class comment on dirty-tracking

    // Kronos (Alpha Completion Checklist, "Project System Finalization" --
    // "auto-backup"/"recovery"): the exact same real, non-clobbering
    // periodic-write pattern SceneManager::tickAutosave() already
    // established, applied to the project's own metadata (which scenes
    // are open, active tab) instead of scene content -- see
    // core::ProjectFile::recoveryPathFor()'s own comment. Ticked once per
    // frame from the same place sceneManager_.tickAutosave() is.
    float projectAutosaveTimer_ = 0.0f;
    int lastAutosavedActiveSceneIndex_ = -2; // -2 (not a real index) forces the first tick to always write once
    size_t lastAutosavedSceneCount_ = static_cast<size_t>(-1);

    enum class PendingFileAction { None, SaveScene, LoadScene, SaveProject, OpenProject };
    PendingFileAction pendingFileAction_ = PendingFileAction::None;
    char filePathBuffer_[256] = "";
    std::string fileActionStatus_;

    // Non-empty whenever the scene just loaded at that path has a pending
    // autosave recovery file newer-than-nothing (see
    // SceneManager::hasRecoveryFile) -- drives the "Recover unsaved
    // changes?" banner drawn by drawRecoveryBanner(), cleared once the
    // user picks Recover or Dismiss.
    std::string recoveryOfferPath_;
    // Kronos ("First-Launch Experience"): set true exactly once, by
    // drawDockspace()'s own first-launch detection -- see
    // drawWelcomePanel()'s own comment.
    bool welcomePanelOpen_ = false;
    // The project-level counterpart to recoveryOfferPath_ above -- see
    // core::ProjectFile::hasRecoveryFile()'s own comment. Kept as a
    // separate field (not reusing recoveryOfferPath_) since a scene
    // recovery offer and a project recovery offer are independent events
    // that can both be pending at once (opening a project whose last
    // session both had unsaved scene edits AND an unsaved scene-list
    // change).
    std::string projectRecoveryOfferPath_;

    VkDescriptorPool imguiDescriptorPool_ = nullptr;
    // Set by endFrame()'s unconditional ImGui::Render(), read by the
    // Renderer overlay callback -- see endFrame()'s comment for why
    // Render() cannot live inside the callback itself.
    ImDrawData* pendingDrawData_ = nullptr;
    bool initialized_ = false;
};

} // namespace engine::studio
