#include "studio/StudioApp.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "core/Components.hpp"
#include "studio/plugins/AlignPlugin.hpp"
#include "studio/plugins/AnimatorPlugin.hpp"
#include "studio/plugins/AudioPreviewPlugin.hpp"
#include "studio/plugins/AnimationPreviewerPlugin.hpp"
#include "studio/plugins/AvatarPreviewer.hpp"
#include "studio/plugins/CataloguePanel.hpp"
#include "studio/plugins/CreatorToolsPlugin.hpp"
#include "studio/plugins/DiagnosticsPlugin.hpp"
#include "studio/plugins/ModerationPanel.hpp"
#include "studio/plugins/NetworkOverlayPlugin.hpp"
#include "studio/plugins/PublishingPanel.hpp"
#include "studio/plugins/CreatorAssetBrowserPlugin.hpp"
#include "studio/plugins/CreatorConsolePlugin.hpp"
#include "studio/plugins/LightingToolsPlugin.hpp"
#include "studio/plugins/TimelineEditorPlugin.hpp"
#include "studio/plugins/MaterialPlugin.hpp"
#include "studio/plugins/ModelImporterPlugin.hpp"
#include "studio/plugins/ParticleEditorPlugin.hpp"
#include "studio/plugins/PhysicsPreviewPlugin.hpp"
#include "studio/plugins/PluginBrowserPlugin.hpp"
#include "studio/plugins/PrefabPlugin.hpp"
#include "studio/plugins/ShopPlugin.hpp"
#include "studio/plugins/TerrainEditorPlugin.hpp"
#include "studio/plugins/TexturePreviewPlugin.hpp"
#include "studio/plugins/TntWarsPlugin.hpp"
#include "studio/plugins/LauncherPlugin.hpp"
#include "studio/plugins/TrailerPanel.hpp"
#include "studio/plugins/UploadAnimationPlugin.hpp"
#include "studio/plugins/UploadAvatarItemPlugin.hpp"
#include "studio/StudioStyle.hpp"

#include <imgui.h>
// Sprint 7 ("Studio UI Revamp") task category 3: DockBuilder (used only
// in drawDockspace(), see its own comment) lives in ImGui's internal
// header -- standard, expected practice for programmatically building a
// default dock layout (ImGui's own demo/examples do the same); it's
// still linked against the exact same imgui::imgui target every other
// Studio source file already uses, not a separate/riskier dependency.
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_vulkan.h>

namespace engine::studio {

namespace {
void checkVkResult(VkResult err) {
    if (err != VK_SUCCESS) {
        std::fprintf(stderr, "StudioApp: ImGui Vulkan backend error (VkResult=%d)\n", static_cast<int>(err));
    }
}
} // namespace

StudioApp::StudioApp() = default;
StudioApp::~StudioApp() { shutdown(); }

bool StudioApp::initialize() {
    core::Window::CreateInfo windowInfo;
    windowInfo.title = "Studio";
    windowInfo.width = 1600;
    windowInfo.height = 900;
    if (!window_.initialize(windowInfo)) {
        std::fprintf(stderr, "StudioApp: Window::initialize failed.\n");
        return false;
    }

    core::Renderer::CreateInfo rendererInfo;
    rendererInfo.window = &window_;
    rendererInfo.appName = "Studio";
    rendererInfo.enableValidation = true;
    if (!renderer_.initialize(rendererInfo)) {
        std::fprintf(stderr, "StudioApp: Renderer::initialize failed.\n");
        return false;
    }

    if (!initImGuiVulkanBackend()) {
        std::fprintf(stderr, "StudioApp: ImGui Vulkan backend init failed.\n");
        return false;
    }

    // Forward every raw SDL event to ImGui (see Window.hpp's
    // setRawEventCallback doc comment for why this is a callback hook
    // rather than a second event-pump loop).
    window_.setRawEventCallback([](const SDL_Event& event) { ImGui_ImplSDL2_ProcessEvent(&event); });

    // Composite ImGui's draw data into the exact same frame the runtime
    // renders, via the overlay hook described in Renderer.hpp -- this is
    // the concrete mechanism behind Principle 4 ("what you see in Studio
    // is what ships"): Studio's UI is drawn *on top of* the shared render
    // graph's output, not by a second, separate renderer.
    //
    // Submits pendingDrawData_ (set unconditionally by endFrame(), see its
    // comment) rather than calling ImGui::Render() here -- renderFrame()
    // can return early (e.g. VK_ERROR_OUT_OF_DATE_KHR -> swapchain
    // recreation) without ever reaching this callback, and ImGui::Render()
    // must always be called exactly once per NewFrame() regardless of
    // that, or the next NewFrame() hits ImGui's own
    // "Forgot to call Render()" assertion.
    renderer_.setOverlayCallback([this](VkCommandBuffer cmd, VkImageView /*targetView*/, VkExtent2D /*extent*/) {
        if (pendingDrawData_) {
            ImGui_ImplVulkan_RenderDrawData(pendingDrawData_, cmd);
        }
    });

    // The other half of the render-graph-sharing story: renders the real
    // scene into viewportTarget_ (resized to whatever the Viewport panel
    // asked for last frame) *before* the swapchain command buffer's own
    // work begins, then hands it to ImGui as a texture. See
    // OffscreenTarget.hpp's doc comment and Renderer::setPrePassCallback's.
    renderer_.setPrePassCallback([this](VkCommandBuffer cmd) {
        VkExtent2D desired = viewportPanel_.desiredExtent();
        if (desired.width == 0 || desired.height == 0) return;

        viewportTarget_.ensureSize(renderer_.allocator(), renderer_.device(), renderer_.swapchainFormat(),
                                    renderer_.depthFormat(), desired);
        if (!viewportTarget_.isValid()) return;

        renderer_.drawSceneInto(cmd, viewportTarget_.colorImage(), viewportTarget_.colorView(),
                                 viewportTarget_.depthImage(), viewportTarget_.depthView(), viewportTarget_.extent(),
                                 viewportPanel_.camera(), ecs_, meshLibrary_, particleSystem_, textureLibrary_);
        renderer_.transitionImage(cmd, viewportTarget_.colorImage(), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);

        // Every other open studio::PreviewScene-owning panel renders into
        // its own OffscreenTarget in this same callback, back to back --
        // core::Renderer supports only one PrePassCallback (see its
        // header), not one for the whole render graph plus one per
        // preview widget. Each call is fully self-contained (its own
        // images/transitions), so stacking them here is safe; each also
        // internally no-ops if its panel isn't open or hasn't rendered
        // anything to preview yet.
        if (avatarPreviewer_ != nullptr && avatarPreviewer_->isOpen()) {
            avatarPreviewer_->renderPreview(cmd, renderer_);
        }
        if (cataloguePanel_ != nullptr && cataloguePanel_->isOpen()) {
            cataloguePanel_->renderPreview(cmd, renderer_);
        }
        if (uploadAvatarItemPlugin_ != nullptr && uploadAvatarItemPlugin_->isOpen()) {
            uploadAvatarItemPlugin_->renderPreview(cmd, renderer_);
        }
        if (animationPreviewerPlugin_ != nullptr && animationPreviewerPlugin_->isOpen()) {
            animationPreviewerPlugin_->renderPreview(cmd, renderer_);
        }
        if (uploadAnimationPlugin_ != nullptr && uploadAnimationPlugin_->isOpen()) {
            uploadAnimationPlugin_->renderPreview(cmd, renderer_);
        }
        if (materialPlugin_ != nullptr && materialPlugin_->isOpen()) {
            materialPlugin_->renderPreview(cmd, renderer_);
        }
        if (particleEditorPlugin_ != nullptr && particleEditorPlugin_->isOpen()) {
            particleEditorPlugin_->renderPreview(cmd, renderer_);
        }
        if (publishingPanel_ != nullptr && publishingPanel_->isOpen()) {
            publishingPanel_->renderPreview(cmd, renderer_, ecs_);
        }
        if (trailerPanel_ != nullptr && trailerPanel_->isOpen()) {
            trailerPanel_->renderPreview(cmd, renderer_, ecs_);
        }
    });

    buildBringUpScene();

    // First-party plugins -- see PluginManager.hpp/IStudioPlugin.hpp for
    // what registering here actually buys: a toolbar/menu entry plus a
    // per-frame update()+drawPanel() call, nothing more (no plugin
    // sandboxing needed since these are native code compiled into this
    // binary, not untrusted third-party scripts).
    pluginManager_.registerPlugin(std::make_unique<plugins::AnimatorPlugin>());
    pluginManager_.registerPlugin(std::make_unique<plugins::AlignPlugin>());
    auto materialPlugin = std::make_unique<plugins::MaterialPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        textureLibrary_);
    materialPlugin_ = materialPlugin.get();
    pluginManager_.registerPlugin(std::move(materialPlugin));
    auto terrainEditor = std::make_unique<plugins::TerrainEditorPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        undoStack_);
    terrainEditorPlugin_ = terrainEditor.get();
    pluginManager_.registerPlugin(std::move(terrainEditor));

    // Creator Tools (Sprint 7 task category 3) -- needs terrainEditorPlugin_
    // to already exist (see CreatorToolsPlugin.hpp's class comment on why
    // it shares TerrainEditorPlugin's own core::Terrain rather than owning
    // a second one), so it's registered right after.
    pluginManager_.registerPlugin(std::make_unique<plugins::CreatorToolsPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        renderer_, *terrainEditorPlugin_));

    auto particleEditor = std::make_unique<plugins::ParticleEditorPlugin>(
        particleSystem_, renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(),
        meshLibrary_, textureLibrary_);
    particleEditorPlugin_ = particleEditor.get();
    pluginManager_.registerPlugin(std::move(particleEditor));
    pluginManager_.registerPlugin(std::make_unique<plugins::PrefabPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_));
    pluginManager_.registerPlugin(std::make_unique<plugins::ModelImporterPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_));

    // Avatar Item System / Catalogue / Loadout -- catalogueDatabase_'s
    // on-disk file is loaded here if it already exists (a fresh Studio
    // checkout with no prior uploads just starts with an empty
    // catalogue, not an error) and catalogueIndex_ built from it, before
    // any of the three plugins below that query/write through them exist.
    if (catalogueDatabase_.loadFromFile(catalogueDatabasePath_)) {
        catalogueIndex_.rebuild(catalogueDatabase_);
    }
    // Same "load if it already exists, empty is not an error" reasoning
    // for the Animation Upload Pipeline's own database.
    (void)animationDatabase_.loadFromFile(animationDatabasePath_);

    // AnimationPreviewerPlugin is constructed first among this group --
    // it has no dependency on any of the others, but AvatarPreviewer now
    // depends on *it* (the Emote System's "equip an emote -> preview it
    // on a real rigged body" hand-off, see AvatarPreviewer's class
    // comment), so it must already exist first.
    auto animationPreviewer = std::make_unique<plugins::AnimationPreviewerPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(),
        riggedMeshLibrary_);
    animationPreviewerPlugin_ = animationPreviewer.get();
    pluginManager_.registerPlugin(std::move(animationPreviewer));

    auto avatarPreviewer = std::make_unique<plugins::AvatarPreviewer>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        textureLibrary_, animationDatabase_, *animationPreviewerPlugin_);
    avatarPreviewer_ = avatarPreviewer.get();
    pluginManager_.registerPlugin(std::move(avatarPreviewer));

    auto cataloguePanel = std::make_unique<plugins::CataloguePanel>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        textureLibrary_, catalogueIndex_, catalogueDatabase_, *avatarPreviewer_);
    cataloguePanel_ = cataloguePanel.get();
    pluginManager_.registerPlugin(std::move(cataloguePanel));

    auto uploadAvatarItem = std::make_unique<plugins::UploadAvatarItemPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        textureLibrary_, catalogueDatabase_, catalogueIndex_, catalogueDatabasePath_);
    uploadAvatarItemPlugin_ = uploadAvatarItem.get();
    pluginManager_.registerPlugin(std::move(uploadAvatarItem));

    auto uploadAnimation = std::make_unique<plugins::UploadAnimationPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        textureLibrary_, animationDatabase_, animationDatabasePath_);
    uploadAnimationPlugin_ = uploadAnimation.get();
    pluginManager_.registerPlugin(std::move(uploadAnimation));

    // Physics Preview (task category 6 "Scene Physics Integration" +
    // category 4 "Physics Debug Tools") -- default-constructible, no
    // shared-resource dependency on any plugin above (its own
    // core::Physics is a private, independently-initialized instance --
    // see play()'s lazy physics_.initialize()).
    auto physicsPreview = std::make_unique<plugins::PhysicsPreviewPlugin>();
    physicsPreviewPlugin_ = physicsPreview.get();
    pluginManager_.registerPlugin(std::move(physicsPreview));

    // Shop (task category 4) -- default-constructible, its own sandboxed
    // economy state, no dependency on any plugin above -- see
    // ShopPlugin.hpp's class comment.
    auto shopPlugin = std::make_unique<plugins::ShopPlugin>();
    shopPlugin_ = shopPlugin.get();
    pluginManager_.registerPlugin(std::move(shopPlugin));

    // Diagnostics (Sprint 8 "Performance Stats & Debug Tools" task
    // category 4) -- real references into profiler_/lastPerformanceMetrics_/
    // notifications_, all already-constructed members by this point (see
    // DiagnosticsPlugin.hpp's own comment on why a stored reference to
    // lastPerformanceMetrics_ keeps reflecting the latest frame's data).
    pluginManager_.registerPlugin(
        std::make_unique<plugins::DiagnosticsPlugin>(profiler_, lastPerformanceMetrics_, notifications_));

    // Network Overlay (Sprint 11 "Networking Foundation" task 4) -- real
    // reference into networkSession_ (already default-constructed by this
    // point; NetworkSession's own real constructor/no-arg-safe design
    // means "not yet initialize()'d" is itself a valid, inert state, see
    // NetworkSession.hpp's own comment). See NetworkOverlayPlugin.hpp's
    // class comment for why this is Host/Join/Stress-Test only, not a
    // playable networked session inside Studio.
    pluginManager_.registerPlugin(std::make_unique<plugins::NetworkOverlayPlugin>(networkSession_));

    // Moderation (Sprint 12 "Moderation & Safety Systems" task 5) --
    // real reference into the same networkSession_ NetworkOverlayPlugin
    // above already uses; see ModerationPanel.hpp's class comment on why
    // this is a separate panel rather than a section bolted onto that
    // one.
    pluginManager_.registerPlugin(std::make_unique<plugins::ModerationPanel>(networkSession_));

    // Publishing (Sprint 13 "Publishing & Game Packaging" task 5) --
    // real references into sceneManager_/viewportPanel_'s own camera/
    // meshLibrary_/textureLibrary_/networkSession_, all already-
    // constructed members by this point. Raw pointer captured for its
    // own renderPreview()/shutdown() hooks, same pattern as
    // materialPlugin_/particleEditorPlugin_ above.
    auto publishingPanel = std::make_unique<plugins::PublishingPanel>(sceneManager_, viewportPanel_.camera(), meshLibrary_,
                                                                        textureLibrary_, networkSession_);
    publishingPanel_ = publishingPanel.get();
    pluginManager_.registerPlugin(std::move(publishingPanel));

    // TNT-Wars (Sprint 14 "TNT-Wars Core Game Build") -- real map-editing
    // (spawns real procedural level geometry via the same real Vulkan
    // mesh-build handles plugins::PrefabPlugin already takes) + class/map
    // tuning, both through the same real networkSession_ reference every
    // other networked plugin above already uses. A built TNT-Wars map is
    // just live ECS content at that point -- publishingPanel_ above (no
    // new publish path needed) is what actually validates/packages it,
    // matching the brief's own "world package validated via
    // PublishingPanel" phrasing.
    pluginManager_.registerPlugin(std::make_unique<plugins::TntWarsPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        textureLibrary_, networkSession_));

    // Kronos ("Studio Finalisation"): the real "front door" -- biome
    // selection + a real renderer-settings panel + real JSON-backed
    // preferences save/load, none of which existed anywhere in Studio
    // before this (confirmed by this pass's own research). Takes a real
    // core::Renderer& directly (not just its Vulkan-handle accessors,
    // unlike TntWarsPlugin above) since its own real Settings section
    // needs to call live setRTReflectionsEnabled()/etc. -- the first
    // plugin in this codebase to hold a Renderer reference.
    pluginManager_.registerPlugin(std::make_unique<plugins::LauncherPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        textureLibrary_, renderer_));

    // TNT-Wars Trailer (Sprint 15 "TNT-Wars Trailer Production") -- the
    // real Studio-side counterpart to engine_runtime's own --trailer
    // mode, driving the exact same real trailer::TrailerDirector class.
    // Own real renderPreview()/shutdown() hooks, same pattern as
    // publishingPanel_ above.
    auto trailerPanel = std::make_unique<plugins::TrailerPanel>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), ecs_, meshLibrary_,
        textureLibrary_, particleSystem_, networkSession_);
    trailerPanel_ = trailerPanel.get();
    pluginManager_.registerPlugin(std::move(trailerPanel));

    // Lighting Tools (Sprint 9 "Creator Tools Phase 1" task category 3) --
    // default-constructible beyond the real Renderer& it edits live.
    pluginManager_.registerPlugin(std::make_unique<plugins::LightingToolsPlugin>(renderer_));

    // Creator Console (Sprint 9 task category 5) -- needs terrainEditorPlugin_
    // (already captured above) to know whether a terrain exists for its
    // real scene-health scan.
    pluginManager_.registerPlugin(std::make_unique<plugins::CreatorConsolePlugin>(*terrainEditorPlugin_));

    // Timeline Editor (Sprint 10 "Creator Tools Phase 2" task category 3) --
    // default-constructible, edits whatever entity is selected.
    pluginManager_.registerPlugin(std::make_unique<plugins::TimelineEditorPlugin>());

    // Creator Asset Browser (Sprint 10 task category 4) -- needs
    // terrainEditorPlugin_ (already captured above) for its Terrain
    // preset entries, same reasoning as CreatorConsolePlugin above.
    pluginManager_.registerPlugin(std::make_unique<plugins::CreatorAssetBrowserPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), meshLibrary_,
        *terrainEditorPlugin_));

    auto texturePreview = std::make_unique<plugins::TexturePreviewPlugin>(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue());
    texturePreviewPlugin_ = texturePreview.get();
    pluginManager_.registerPlugin(std::move(texturePreview));

    // AudioPreviewPlugin needs an explicit initialize() call (it owns a
    // real core::Audio/miniaudio engine) before StudioApp can hand it
    // off to pluginManager_ -- see audioPreviewPlugin_'s doc comment on
    // why the raw pointer is captured here.
    auto audioPreview = std::make_unique<plugins::AudioPreviewPlugin>();
    if (!audioPreview->initialize()) {
        std::fprintf(stderr, "StudioApp: AudioPreviewPlugin::initialize failed -- continuing without it.\n");
    }
    audioPreviewPlugin_ = audioPreview.get();
    pluginManager_.registerPlugin(std::move(audioPreview));

    // The one plugin that lets a user load *more* plugins at runtime --
    // see PluginBrowserPlugin.hpp. Registered last among first-party
    // plugins so every studio::plugins::ScriptedPlugin it constructs
    // later lands after it in pluginManager_'s list, keeping same-
    // category plugins contiguous for PluginManager::drawMenu()'s
    // category-change separator logic.
    pluginManager_.registerPlugin(std::make_unique<plugins::PluginBrowserPlugin>(pluginManager_, networkSession_));

    if (!debugConsolePanel_.initialize(ecs_)) {
        std::fprintf(stderr, "StudioApp: DebugConsolePanel::initialize failed.\n");
        return false;
    }

    initialized_ = true;
    return true;
}

void StudioApp::buildBringUpScene() {
    // Same dressing as engine_runtime's main.cpp -- real geometry, real
    // PBR materials -- so Explorer/Inspector/Viewport all have something
    // substantial to show immediately, and so Studio's viewport is
    // visibly rendering the exact same kind of content the runtime does
    // (Principle 4). No Physics here (see class comment): these are
    // static entities, not simulated bodies.
    uint32_t planeMesh = meshLibrary_.registerMesh(core::Mesh::createPlane(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), 25.0f, 25.0f));
    uint32_t boxMesh = meshLibrary_.registerMesh(core::Mesh::createBox(
        renderer_.allocator(), renderer_.device(), renderer_.commandPool(), renderer_.graphicsQueue(), {0.5f, 0.5f, 0.5f}));

    auto ground = ecs_.createEntity("GroundPlane");
    auto& groundRenderable = ecs_.addComponent<core::Renderable>(ground);
    groundRenderable.meshHandle = planeMesh;
    groundRenderable.baseColor = {0.35f, 0.36f, 0.4f, 1.0f};
    groundRenderable.metallic = 0.0f;
    groundRenderable.roughness = 0.85f;
    auto& groundMeshSource = ecs_.addComponent<core::MeshSource>(ground);
    groundMeshSource.kind = core::MeshSourceKind::Plane;
    groundMeshSource.params = {25.0f, 0.0f, 25.0f};

    auto box = ecs_.createEntity("DynamicBox");
    if (auto* transform = ecs_.tryGetComponent<core::Transform>(box)) {
        transform->position = {0.0f, 2.0f, 0.0f};
    }
    auto& boxRenderable = ecs_.addComponent<core::Renderable>(box);
    boxRenderable.meshHandle = boxMesh;
    boxRenderable.baseColor = {0.95f, 0.64f, 0.54f, 1.0f};
    boxRenderable.metallic = 1.0f;
    boxRenderable.roughness = 0.25f;
    auto& boxMeshSource = ecs_.addComponent<core::MeshSource>(box);
    boxMeshSource.kind = core::MeshSourceKind::Box;
    boxMeshSource.params = {0.5f, 0.5f, 0.5f};

    constexpr int kGridSize = 6;
    for (int xi = 0; xi < kGridSize; ++xi) {
        for (int zi = 0; zi < kGridSize; ++zi) {
            auto entity = ecs_.createEntity("MaterialSample");
            if (auto* transform = ecs_.tryGetComponent<core::Transform>(entity)) {
                transform->position = {(xi - kGridSize / 2) * 1.4f, 0.5f, 5.0f + zi * 1.4f};
                transform->scale = {0.45f, 0.45f, 0.45f};
            }
            auto& renderable = ecs_.addComponent<core::Renderable>(entity);
            renderable.meshHandle = boxMesh;
            renderable.baseColor = {0.9f, 0.9f, 0.92f, 1.0f};
            renderable.metallic = static_cast<float>(xi) / static_cast<float>(kGridSize - 1);
            renderable.roughness = std::max(0.05f, static_cast<float>(zi) / static_cast<float>(kGridSize - 1));
            auto& sampleMeshSource = ecs_.addComponent<core::MeshSource>(entity);
            sampleMeshSource.kind = core::MeshSourceKind::Box;
            sampleMeshSource.params = {0.5f, 0.5f, 0.5f};
        }
    }

    viewportPanel_.camera().position = {0.0f, 8.0f, -10.0f};
    viewportPanel_.camera().yawDegrees = 90.0f;
    viewportPanel_.camera().pitchDegrees = -22.0f;
}

bool StudioApp::initImGuiVulkanBackend() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // docking branch -- see cmake/Dependencies.cmake
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    applyStudioStyle(); // Studio's own palette/rounding on top of the stock dark theme -- see StudioStyle.hpp

    if (!ImGui_ImplSDL2_InitForVulkan(window_.handle())) {
        return false;
    }

    std::array<VkDescriptorPoolSize, 1> poolSizes{{{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64}}};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 64;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(renderer_.device(), &poolInfo, nullptr, &imguiDescriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "StudioApp: vkCreateDescriptorPool (ImGui) failed.\n");
        return false;
    }

    VkFormat colorFormat = renderer_.swapchainFormat();

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = renderer_.instance();
    initInfo.PhysicalDevice = renderer_.physicalDevice();
    initInfo.Device = renderer_.device();
    initInfo.QueueFamily = renderer_.graphicsQueueFamily();
    initInfo.Queue = renderer_.graphicsQueue();
    initInfo.DescriptorPool = imguiDescriptorPool_;
    initInfo.MinImageCount = renderer_.framesInFlight();
    initInfo.ImageCount = renderer_.swapchainImageCount();
    initInfo.UseDynamicRendering = true;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &colorFormat;
    initInfo.Allocator = nullptr;
    initInfo.CheckVkResultFn = &checkVkResult;

    if (!ImGui_ImplVulkan_Init(&initInfo)) {
        return false;
    }

    std::fprintf(stdout, "StudioApp: ImGui Vulkan backend initialized (dynamic rendering, format=%d)\n",
                 static_cast<int>(colorFormat));
    return true;
}

void StudioApp::drawDockspace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_MenuBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("StudioDockHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    if (ImGui::BeginMenuBar()) {
        // Sprint 7 ("Studio UI Revamp") task category 1: real branded
        // header text -- "Bands Studio", tinted with the same accent
        // StudioStyle.hpp already uses for every interactive element, so
        // it reads as this app's own identity rather than a stock ImGui
        // menu bar. A thin vertical separator (drawn manually -- ImGui's
        // own ImGui::Separator() is horizontal-only and would break the
        // menu bar's single-line layout) keeps it visually distinct from
        // the File/Edit/View menus that follow.
        ImGui::TextColored(ImVec4(0.33f, 0.72f, 0.70f, 1.0f), "Bands Studio");
        ImGui::SameLine(0.0f, 12.0f);
        ImVec2 sepTop = ImGui::GetCursorScreenPos();
        float sepHeight = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddLine(sepTop, ImVec2(sepTop.x, sepTop.y + sepHeight),
                                             ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
        ImGui::Dummy(ImVec2(1.0f, sepHeight));
        ImGui::SameLine(0.0f, 12.0f);

        drawFileMenu();
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, undoStack_.canUndo())) {
                undoStack_.undo();
            }
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, undoStack_.canRedo())) {
                undoStack_.redo();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Explorer", nullptr, true, false);
            ImGui::MenuItem("Inspector", nullptr, true, false);
            ImGui::MenuItem("Viewport", nullptr, true, false);
            ImGui::MenuItem("Script Editor", nullptr, true, false);
            ImGui::MenuItem("Stats", nullptr, true, false);
            ImGui::MenuItem("Scene Search", nullptr, true, false);
            ImGui::EndMenu();
        }
        pluginManager_.drawMenu();
        ImGui::EndMenuBar();
    }

    drawSceneTabsBar();
    drawRecoveryBanner();

    ImGuiID dockspaceId = ImGui::GetID("StudioDockSpace");

    // Sprint 7 ("Studio UI Revamp") task category 3: a real, one-time
    // default layout -- Explorer left, Inspector right, Debug Console +
    // Stats bottom, Viewport/Script Editor filling the remaining center.
    // Built *only* the very first time this dockspace ID has no docking
    // data at all (DockBuilderGetNode() returns null): ImGui itself
    // already loads any saved layout from imgui.ini before this function
    // ever runs, so a real user's own customized/dragged-around layout
    // from a previous session is never silently clobbered on a later
    // launch -- this only ever fires on a genuinely first-ever run (or
    // after imgui.ini is deleted).
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID centerId = dockspaceId;
        ImGuiID leftId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Left, 0.20f, nullptr, &centerId);
        ImGuiID rightId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Right, 0.23f, nullptr, &centerId);
        ImGuiID bottomId = ImGui::DockBuilderSplitNode(centerId, ImGuiDir_Down, 0.28f, nullptr, &centerId);

        ImGui::DockBuilderDockWindow("Explorer", leftId);
        ImGui::DockBuilderDockWindow("Scene Search", leftId);
        ImGui::DockBuilderDockWindow("Inspector", rightId);
        ImGui::DockBuilderDockWindow("Debug Console", bottomId);
        ImGui::DockBuilderDockWindow("Stats", bottomId);
        ImGui::DockBuilderDockWindow("Viewport", centerId);
        ImGui::DockBuilderDockWindow("Script Editor", centerId);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);
    ImGui::End();

    drawPendingFileActionPopup();
}

void StudioApp::drawFileMenu() {
    if (!ImGui::BeginMenu("File")) return;

    if (ImGui::MenuItem("New Scene")) {
        if (!sceneManager_.currentScenePath().empty()) {
            (void)sceneManager_.saveScene(sceneManager_.currentScenePath(), ecs_, viewportPanel_.camera());
        }
        sceneManager_.newScene(ecs_);
        explorerPanel_.setSelected(core::kNullEntity);
    }

    bool canSaveDirect = !sceneManager_.currentScenePath().empty();
    if (ImGui::MenuItem("Save Scene", "Ctrl+S", false, canSaveDirect)) {
        bool saveOk = sceneManager_.saveScene(sceneManager_.currentScenePath(), ecs_, viewportPanel_.camera());
        fileActionStatus_ = saveOk ? "Saved " + sceneManager_.currentScenePath() : "Save failed: " + sceneManager_.currentScenePath();
        notifications_.push(fileActionStatus_, saveOk ? NotificationSeverity::Success : NotificationSeverity::Error);
    }
    if (ImGui::MenuItem("Save Scene As...")) {
        std::string suggested = canSaveDirect ? sceneManager_.currentScenePath() : "scenes/scene.scene";
        std::snprintf(filePathBuffer_, sizeof(filePathBuffer_), "%s", suggested.c_str());
        pendingFileAction_ = PendingFileAction::SaveScene;
    }
    if (ImGui::MenuItem("Load Scene...")) {
        std::string suggested = canSaveDirect ? sceneManager_.currentScenePath() : "scenes/scene.scene";
        std::snprintf(filePathBuffer_, sizeof(filePathBuffer_), "%s", suggested.c_str());
        pendingFileAction_ = PendingFileAction::LoadScene;
    }

    ImGui::Separator();
    if (ImGui::MenuItem("Save Project As...")) {
        std::string suggested = currentProjectPath_.empty() ? "project.project" : currentProjectPath_;
        std::snprintf(filePathBuffer_, sizeof(filePathBuffer_), "%s", suggested.c_str());
        pendingFileAction_ = PendingFileAction::SaveProject;
    }
    if (ImGui::MenuItem("Open Project...")) {
        std::string suggested = currentProjectPath_.empty() ? "project.project" : currentProjectPath_;
        std::snprintf(filePathBuffer_, sizeof(filePathBuffer_), "%s", suggested.c_str());
        pendingFileAction_ = PendingFileAction::OpenProject;
    }

    ImGui::Separator();
    ImGui::MenuItem("Import .rbxlx... (not implemented -- see src/migration/)");

    if (!fileActionStatus_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("%s", fileActionStatus_.c_str());
    }

    ImGui::EndMenu();
}

void StudioApp::drawPendingFileActionPopup() {
    if (pendingFileAction_ == PendingFileAction::None) return;

    const char* title = "##file_action_popup";
    const char* verb = "Confirm";
    switch (pendingFileAction_) {
        case PendingFileAction::SaveScene: title = "Save Scene As"; verb = "Save"; break;
        case PendingFileAction::LoadScene: title = "Load Scene"; verb = "Load"; break;
        case PendingFileAction::SaveProject: title = "Save Project As"; verb = "Save"; break;
        case PendingFileAction::OpenProject: title = "Open Project"; verb = "Open"; break;
        case PendingFileAction::None: return;
    }

    ImGui::OpenPopup(title);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(380.0f);
        ImGui::InputText("##file_action_path", filePathBuffer_, sizeof(filePathBuffer_));

        bool confirmed = ImGui::Button(verb) || (ImGui::IsWindowFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter, false));
        ImGui::SameLine();
        bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false);

        if (confirmed && filePathBuffer_[0] != '\0') {
            std::string path = filePathBuffer_;
            switch (pendingFileAction_) {
                case PendingFileAction::SaveScene:
                    fileActionStatus_ =
                        sceneManager_.saveScene(path, ecs_, viewportPanel_.camera()) ? "Saved " + path : "Save failed: " + path;
                    if (fileActionStatus_.rfind("Saved", 0) == 0) {
                        auto& tabs = sceneManager_.openScenePaths();
                        if (std::find(tabs.begin(), tabs.end(), path) == tabs.end()) tabs.push_back(path);
                        sceneManager_.setActiveTabIndex(
                            static_cast<int>(std::distance(tabs.begin(), std::find(tabs.begin(), tabs.end(), path))));
                    }
                    break;
                case PendingFileAction::LoadScene:
                    switchToScene(path);
                    break;
                case PendingFileAction::SaveProject: {
                    currentProject_.scenePaths = sceneManager_.openScenePaths();
                    currentProject_.activeSceneIndex = sceneManager_.activeTabIndex();
                    currentProject_.touch();
                    fileActionStatus_ = currentProject_.saveToFile(path) ? "Saved project " + path : "Save failed: " + path;
                    if (fileActionStatus_.rfind("Saved", 0) == 0) {
                        currentProjectPath_ = path;
                        // A deliberate Save supersedes any pending project
                        // recovery snapshot -- same "explicit save wins"
                        // rule SceneManager::saveScene() already applies
                        // to its own .autosave file.
                        (void)std::filesystem::remove(core::ProjectFile::recoveryPathFor(path));
                        lastAutosavedSceneCount_ = static_cast<size_t>(-1);
                        lastAutosavedActiveSceneIndex_ = -2;
                    }
                    break;
                }
                case PendingFileAction::OpenProject: {
                    core::ProjectFile loaded;
                    if (loaded.loadFromFile(path)) {
                        currentProject_ = loaded;
                        currentProjectPath_ = path;
                        sceneManager_.openScenePaths() = loaded.scenePaths;
                        sceneManager_.setActiveTabIndex(loaded.activeSceneIndex);
                        if (loaded.activeSceneIndex >= 0 &&
                            static_cast<size_t>(loaded.activeSceneIndex) < loaded.scenePaths.size()) {
                            switchToScene(loaded.scenePaths[static_cast<size_t>(loaded.activeSceneIndex)]);
                        }
                        fileActionStatus_ = loaded.isCompatibleVersion()
                                                 ? "Opened project " + path
                                                 : "Opened project " + path + " (warning: saved by project format version " +
                                                       loaded.version + ", this build writes " +
                                                       std::string(core::kProjectFormatVersion) + ")";
                        projectRecoveryOfferPath_ = core::ProjectFile::hasRecoveryFile(path) ? path : std::string();
                        lastAutosavedSceneCount_ = static_cast<size_t>(-1);
                        lastAutosavedActiveSceneIndex_ = -2;
                    } else {
                        fileActionStatus_ = "Open failed: " + path;
                    }
                    break;
                }
                case PendingFileAction::None: break;
            }
            pendingFileAction_ = PendingFileAction::None;
            ImGui::CloseCurrentPopup();
        } else if (cancelled) {
            pendingFileAction_ = PendingFileAction::None;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void StudioApp::drawSceneTabsBar() {
    const auto& tabs = sceneManager_.openScenePaths();
    if (tabs.empty()) return;

    ImGui::BeginChild("SceneTabsBar", ImVec2(0.0f, 28.0f), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < tabs.size(); ++i) {
        bool isActive = static_cast<int>(i) == sceneManager_.activeTabIndex();
        std::string label = std::filesystem::path(tabs[i]).filename().string();
        if (label.empty()) label = tabs[i];
        if (isActive && sceneManager_.isDirty()) label += " *";
        label += "##scene_tab_" + std::to_string(i);

        if (isActive) {
            ImGui::PushStyleColor(ImGuiCol_Header, ImGui::GetStyle().Colors[ImGuiCol_TabSelected]);
        }
        if (ImGui::Selectable(label.c_str(), isActive, ImGuiSelectableFlags_None, ImVec2(160.0f, 0.0f))) {
            if (!isActive) switchToScene(tabs[i]);
        }
        if (isActive) ImGui::PopStyleColor();
        if (i + 1 < tabs.size()) ImGui::SameLine();
    }
    ImGui::EndChild();
}

void StudioApp::tickProjectAutosave(float dt) {
    // A real, honest no-op until a project has actually been saved or
    // opened at least once -- matches SceneManager::tickAutosave()'s own
    // "no current scene path -> nothing to protect yet" precedent.
    if (currentProjectPath_.empty()) return;

    projectAutosaveTimer_ += dt;
    if (projectAutosaveTimer_ < SceneManager::kAutosaveIntervalSeconds) return;
    projectAutosaveTimer_ = 0.0f;

    // Coarse dirty check, same spirit as SceneManager's own entity-count
    // heuristic: only the scene list and which tab is active can actually
    // change on a live ProjectFile between explicit Saves (name/version
    // aren't editable live), so comparing those two is a real, sufficient
    // "did anything worth backing up change" signal, not a guess.
    const auto& openPaths = sceneManager_.openScenePaths();
    int activeIndex = sceneManager_.activeTabIndex();
    if (openPaths.size() == lastAutosavedSceneCount_ && activeIndex == lastAutosavedActiveSceneIndex_) return;

    core::ProjectFile snapshot = currentProject_;
    snapshot.scenePaths = openPaths;
    snapshot.activeSceneIndex = activeIndex;
    snapshot.touch();
    if (!snapshot.saveToFile(core::ProjectFile::recoveryPathFor(currentProjectPath_))) {
        std::fprintf(stderr, "StudioApp: project autosave to \"%s\" failed\n",
                      core::ProjectFile::recoveryPathFor(currentProjectPath_).c_str());
    }
    lastAutosavedSceneCount_ = openPaths.size();
    lastAutosavedActiveSceneIndex_ = activeIndex;
}

void StudioApp::drawRecoveryBanner() {
    if (recoveryOfferPath_.empty()) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.32f, 0.05f, 1.0f));
    ImGui::BeginChild("RecoveryBanner", ImVec2(0.0f, 32.0f), ImGuiChildFlags_Borders);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("An autosaved recovery snapshot exists for \"%s\" from a previous session.", recoveryOfferPath_.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Recover")) {
        std::string recoveryPath = SceneManager::recoveryPathFor(recoveryOfferPath_);
        std::string originalPath = recoveryOfferPath_;
        if (sceneManager_.loadScene(recoveryPath, ecs_, meshLibrary_, renderer_.allocator(), renderer_.device(),
                                     renderer_.commandPool(), renderer_.graphicsQueue(), viewportPanel_.camera())) {
            // Commits the recovered content back to the real scene path
            // (not the .autosave file) -- also correctly resets
            // currentScenePath_/dirty_ and deletes the now-consumed
            // recovery file (see saveScene()'s comment).
            (void)sceneManager_.saveScene(originalPath, ecs_, viewportPanel_.camera());
            explorerPanel_.setSelected(core::kNullEntity);
            fileActionStatus_ = "Recovered " + originalPath;
        } else {
            fileActionStatus_ = "Recovery failed to load: " + recoveryPath;
        }
        recoveryOfferPath_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Dismiss")) {
        (void)std::filesystem::remove(SceneManager::recoveryPathFor(recoveryOfferPath_));
        recoveryOfferPath_.clear();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (projectRecoveryOfferPath_.empty()) return;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.45f, 0.32f, 0.05f, 1.0f));
    ImGui::BeginChild("ProjectRecoveryBanner", ImVec2(0.0f, 32.0f), ImGuiChildFlags_Borders);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("An autosaved project recovery snapshot exists for \"%s\" from a previous session.",
                projectRecoveryOfferPath_.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Recover##project")) {
        core::ProjectFile recovered;
        std::string recoveryPath = core::ProjectFile::recoveryPathFor(projectRecoveryOfferPath_);
        if (recovered.loadFromFile(recoveryPath)) {
            currentProject_ = recovered;
            sceneManager_.openScenePaths() = recovered.scenePaths;
            sceneManager_.setActiveTabIndex(recovered.activeSceneIndex);
            (void)currentProject_.saveToFile(projectRecoveryOfferPath_);
            (void)std::filesystem::remove(recoveryPath);
            fileActionStatus_ = "Recovered project " + projectRecoveryOfferPath_;
        } else {
            fileActionStatus_ = "Project recovery failed to load: " + recoveryPath;
        }
        projectRecoveryOfferPath_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Dismiss##project")) {
        (void)std::filesystem::remove(core::ProjectFile::recoveryPathFor(projectRecoveryOfferPath_));
        projectRecoveryOfferPath_.clear();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void StudioApp::switchToScene(const std::string& path) {
    if (!sceneManager_.currentScenePath().empty() && sceneManager_.currentScenePath() != path) {
        (void)sceneManager_.saveScene(sceneManager_.currentScenePath(), ecs_, viewportPanel_.camera());
    }

    if (!sceneManager_.loadScene(path, ecs_, meshLibrary_, renderer_.allocator(), renderer_.device(),
                                  renderer_.commandPool(), renderer_.graphicsQueue(), viewportPanel_.camera())) {
        fileActionStatus_ = "Load failed: " + path;
        return;
    }

    explorerPanel_.setSelected(core::kNullEntity);
    fileActionStatus_ = "Loaded " + path;
    recoveryOfferPath_ = SceneManager::hasRecoveryFile(path) ? path : std::string();

    auto& tabs = sceneManager_.openScenePaths();
    auto it = std::find(tabs.begin(), tabs.end(), path);
    if (it == tabs.end()) {
        tabs.push_back(path);
        sceneManager_.setActiveTabIndex(static_cast<int>(tabs.size()) - 1);
    } else {
        sceneManager_.setActiveTabIndex(static_cast<int>(std::distance(tabs.begin(), it)));
    }
}

void StudioApp::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    drawDockspace();
}

void StudioApp::endFrame() {
    // Unconditional -- always pairs with beginFrame()'s ImGui::NewFrame(),
    // regardless of whether renderer_.renderFrame() (called right after
    // this, in run()) ends up actually reaching the overlay callback that
    // consumes pendingDrawData_. See that callback's comment in
    // initialize() for why this split exists.
    ImGui::Render();
    pendingDrawData_ = ImGui::GetDrawData();
}

void StudioApp::run() {
    using Clock = std::chrono::steady_clock;
    auto previous = Clock::now();

    while (window_.pumpEvents()) {
        auto now = Clock::now();
        float deltaTime = std::chrono::duration<float>(now - previous).count();
        previous = now;

        beginFrame();

        // See particleSystem_'s declaration comment for why this one
        // subsystem is ticked despite Studio's scene otherwise being static.
        particleSystem_.update(deltaTime, ecs_);
        // Same reasoning as particleSystem_: the console's own Scripting
        // instance needs ticking every frame for task.wait/spawn/defer and
        // events.onUpdate to actually work, independent of whether a Play
        // Solo session exists (see DebugConsolePanel.hpp's class comment).
        debugConsolePanel_.tick(deltaTime);

        explorerPanel_.draw(ecs_);
        inspectorPanel_.draw(ecs_, explorerPanel_.selectedEntity(), undoStack_);

        // Ctrl+Z / Ctrl+Y (or Ctrl+Shift+Z) -- checked once per frame,
        // not per-widget, so it works regardless of which panel has
        // keyboard focus. ImGui::GetIO().WantCaptureKeyboard isn't
        // checked here deliberately: an active text-input field (e.g.
        // Inspector's Name field) capturing Ctrl+Z for its own text-undo
        // would be the more surprising behavior for a scene editor,
        // where Ctrl+Z overwhelmingly means "undo my last scene edit".
        {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                if (io.KeyShift) {
                    undoStack_.redo();
                } else {
                    undoStack_.undo();
                }
            } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                undoStack_.redo();
            } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !sceneManager_.currentScenePath().empty()) {
                bool saveOk = sceneManager_.saveScene(sceneManager_.currentScenePath(), ecs_, viewportPanel_.camera());
                fileActionStatus_ = saveOk ? "Saved " + sceneManager_.currentScenePath() : "Save failed: " + sceneManager_.currentScenePath();
                notifications_.push(fileActionStatus_, saveOk ? NotificationSeverity::Success : NotificationSeverity::Error);
            }
        }

        // undoCount() changing (either direction -- see UndoStack::undoCount's
        // comment) is the in-place-edit half of SceneManager's dirty
        // tracking; entity-count changes are covered automatically inside
        // tickAutosave() itself. See SceneManager.hpp's class comment.
        if (size_t undoCount = undoStack_.undoCount(); undoCount != lastSeenUndoCount_) {
            sceneManager_.markDirty();
            lastSeenUndoCount_ = undoCount;
        }
        sceneManager_.tickAutosave(deltaTime, ecs_, viewportPanel_.camera());
        tickProjectAutosave(deltaTime);

        panels::ViewportDebugContext viewportDebugContext;
        if (terrainEditorPlugin_ != nullptr && terrainEditorPlugin_->hasTerrain()) {
            viewportDebugContext.terrain = &terrainEditorPlugin_->terrain();
        }
        viewportDebugContext.renderer = &renderer_;
        viewportPanel_.draw(deltaTime, viewportTarget_.imguiTextureId(), viewportTarget_.extent(), &ecs_,
                             &meshLibrary_, explorerPanel_, physicsPreviewPlugin_, viewportDebugContext);
        scriptEditorPanel_.draw();
        debugConsolePanel_.draw();

        // Sprint 8 ("Performance Stats & Debug Tools"): compose this
        // frame's full metrics -- Renderer's real render-only numbers,
        // plus real physics body counts (only meaningful while
        // PhysicsPreviewPlugin is actually Play-ing; Studio otherwise
        // runs no live core::Physics at all, see StudioApp's class
        // comment) and real terrain chunk counts (only meaningful once
        // TerrainEditorPlugin actually has a terrain) and real process
        // stats -- then feed it to the same real Profiler engine_runtime
        // uses (spike/stall detection, optional JSON recording) before
        // handing it to StatsPanel/DiagnosticsPlugin.
        {
            uint32_t activeBodies = 0, totalBodies = 0;
            if (physicsPreviewPlugin_ != nullptr && physicsPreviewPlugin_->isPlaying()) {
                activeBodies = physicsPreviewPlugin_->physics().activeBodyCount();
                totalBodies = physicsPreviewPlugin_->physics().totalBodyCount();
            }
            uint32_t loadedChunks = 0, totalChunks = 0;
            if (terrainEditorPlugin_ != nullptr && terrainEditorPlugin_->hasTerrain()) {
                loadedChunks = static_cast<uint32_t>(terrainEditorPlugin_->terrain().loadedChunkCount());
                totalChunks = static_cast<uint32_t>(terrainEditorPlugin_->terrain().chunkCount());
            }
            core::ProcessStats processStats = processStatsSampler_.sample();
            lastPerformanceMetrics_ = core::composePerformanceMetrics(renderer_.metrics(), activeBodies, totalBodies,
                                                                        loadedChunks, totalChunks, processStats);
            double nowSeconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            profiler_.recordFrame(lastPerformanceMetrics_.frameTimeMs, nowSeconds);
            profiler_.recordSnapshot(lastPerformanceMetrics_, nowSeconds);
        }
        statsPanel_.draw(lastPerformanceMetrics_);
        if (core::EntityId searchClicked = sceneSearchPanel_.draw(ecs_, explorerPanel_.selectedEntity());
            searchClicked != core::kNullEntity) {
            explorerPanel_.setSelected(searchClicked);
        }

        // update() runs for every registered plugin regardless of whether
        // its window is open (see IStudioPlugin.hpp) -- e.g. the Animator
        // plugin keeps advancing playback even while its panel is closed.
        // drawPanel() only runs for the ones currently toggled open.
        pluginManager_.update(deltaTime, ecs_, explorerPanel_.selectedEntity(), explorerPanel_.selectedEntities());
        pluginManager_.drawPanels(ecs_, explorerPanel_.selectedEntity(), explorerPanel_.selectedEntities());

        // Sprint 7 ("Studio UI Revamp") task category 6 -- ticked and
        // drawn every frame regardless of whether anything was just
        // pushed, the same "always runs, only visibly does something
        // when there's real state" pattern debugConsolePanel_.tick()
        // above already uses.
        notifications_.tick(deltaTime);
        notifications_.draw();

        endFrame();

        if (!renderer_.renderFrame()) {
            std::fprintf(stderr, "StudioApp: renderFrame() reported an unrecoverable error.\n");
            break;
        }
    }
}

void StudioApp::shutdown() {
    if (!initialized_) return;

    networkSession_.shutdown();
    debugConsolePanel_.shutdown();
    if (audioPreviewPlugin_ != nullptr) audioPreviewPlugin_->shutdown();
    if (texturePreviewPlugin_ != nullptr) texturePreviewPlugin_->shutdown();
    // Same ordering requirement as texturePreviewPlugin_ above: each of
    // these owns a studio::PreviewScene (its own OffscreenTarget /
    // ImGui descriptor set), which must be torn down before
    // ImGui_ImplVulkan_Shutdown() runs further down, same class of bug
    // as the one documented on viewportTarget_.destroy() below.
    if (avatarPreviewer_ != nullptr) avatarPreviewer_->shutdown(renderer_);
    if (cataloguePanel_ != nullptr) cataloguePanel_->shutdown(renderer_);
    if (uploadAvatarItemPlugin_ != nullptr) uploadAvatarItemPlugin_->shutdown(renderer_);
    if (animationPreviewerPlugin_ != nullptr) animationPreviewerPlugin_->shutdown(renderer_);
    if (uploadAnimationPlugin_ != nullptr) uploadAnimationPlugin_->shutdown(renderer_);
    if (materialPlugin_ != nullptr) materialPlugin_->shutdown(renderer_);
    if (particleEditorPlugin_ != nullptr) particleEditorPlugin_->shutdown(renderer_);
    if (publishingPanel_ != nullptr) publishingPanel_->shutdown(renderer_);
    if (trailerPanel_ != nullptr) trailerPanel_->shutdown(renderer_);

    vkDeviceWaitIdle(renderer_.device());

    // viewportTarget_ holds a descriptor set from ImGui_ImplVulkan_AddTexture(),
    // and destroy() below calls ImGui_ImplVulkan_RemoveTexture() to free it.
    // That call reads ImGui's Vulkan backend data via io.BackendRendererUserData
    // -- ImGui_ImplVulkan_Shutdown() nulls that pointer, so RemoveTexture()
    // must run first or it dereferences null. Found by running Studio: it
    // segfaulted in ImGui_ImplVulkan_RemoveTexture on every exit.
    //
    // Same ordering contract as core::Application otherwise: VMA allocations
    // (mesh buffers, the offscreen target's images) must be freed before
    // renderer_.shutdown() destroys the allocator -- see Renderer::shutdown()'s NOTE.
    viewportTarget_.destroy(renderer_.allocator(), renderer_.device());

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (imguiDescriptorPool_) {
        vkDestroyDescriptorPool(renderer_.device(), imguiDescriptorPool_, nullptr);
        imguiDescriptorPool_ = nullptr;
    }

    meshLibrary_.destroyAll(renderer_.allocator());
    textureLibrary_.destroyAll(renderer_.allocator(), renderer_.device());
    riggedMeshLibrary_.destroyAll(renderer_.allocator());
    renderer_.shutdown();
    window_.shutdown();
    initialized_ = false;
}

} // namespace engine::studio
