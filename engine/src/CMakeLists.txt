# ---------------------------------------------------------------------------
# Shader compilation -- GLSL source -> SPIR-V via glslc, at build time.
# Compiled .spv files land in an absolute build-tree path
# (ENGINE_SHADER_DIR) that engine_core is compiled with as a #define, so
# both engine_runtime and studio can find them regardless of the process's
# working directory. This is a development-time convenience, explicitly
# not a deployment story: a real install step would embed compiled shaders
# as binary resources rather than loading loose files off disk at runtime.
# ---------------------------------------------------------------------------
find_program(GLSLC_EXECUTABLE glslc REQUIRED)

set(ENGINE_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_instanced.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_skinned.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/scene_rt.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/shadow.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/particle.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/bloom_extract.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/composite.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/cinematic.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/volumetric_fog.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/luminance.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/sky.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ssr.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/glass.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/glass.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/shaders/ui.frag
)
set(ENGINE_SHADER_OUTPUT_DIR ${CMAKE_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${ENGINE_SHADER_OUTPUT_DIR})

set(ENGINE_COMPILED_SHADERS "")
foreach(shader_source ${ENGINE_SHADER_SOURCES})
    get_filename_component(shader_name ${shader_source} NAME)
    set(shader_output ${ENGINE_SHADER_OUTPUT_DIR}/${shader_name}.spv)
    add_custom_command(
        OUTPUT ${shader_output}
        # Sprint 14 ("RTX Upgrade"): --target-env=vulkan1.2 (SPIR-V 1.5)
        # instead of glslc's default vulkan1.0/SPIR-V 1.0 -- required for
        # scene.frag's real GL_EXT_ray_query usage (ray query needs SPIR-V
        # 1.4+). Every other shader here still compiles unchanged under
        # the newer target; this is a strict superset, not a behavior
        # change for anything not using newer features.
        COMMAND ${GLSLC_EXECUTABLE} --target-env=vulkan1.2 ${shader_source} -o ${shader_output}
        DEPENDS ${shader_source}
        COMMENT "Compiling shader ${shader_name} -> SPIR-V"
        VERBATIM
    )
    list(APPEND ENGINE_COMPILED_SHADERS ${shader_output})
endforeach()

add_custom_target(engine_shaders DEPENDS ${ENGINE_COMPILED_SHADERS})

# ---------------------------------------------------------------------------
# engine_core: everything that is platform-agnostic per docs/ARCHITECTURE.md
# Principle 6 -- render graph, ECS, physics, audio, scripting, netcode,
# migration, trust & safety, anti-cheat, platform adapters, marketplace,
# accessibility, analytics. Linked by both engine_runtime and studio, so
# "what you see in Studio is what ships" (Principle 4) is a build-graph fact,
# not just a design intention.
# ---------------------------------------------------------------------------

add_library(engine_core STATIC
    core/Application.cpp
    core/Window.cpp
    core/Renderer.cpp
    core/UIRenderer.cpp
    core/RayTracingScene.cpp
    core/VmaImpl.cpp
    core/Mesh.cpp
    core/CharacterController.cpp
    core/Animation.cpp
    core/PropAnimation.cpp
    core/RuntimeAnimationPlayer.cpp
    core/ScriptWorldApi.cpp
    core/ScriptNetworkApi.cpp
    core/ScriptUiApi.cpp
    core/TrailerScriptApi.cpp
    core/ScenePicking.cpp
    core/ObjLoader.cpp
    core/AssetMetadata.cpp
    core/AssetCache.cpp
    core/AssetRegistry.cpp
    core/LocalProfile.cpp
    core/ParticleSystem.cpp
    core/SceneFile.cpp
    core/ProjectFile.cpp
    core/AvatarItem.cpp
    core/AvatarItemManifest.cpp
    core/CatalogueDatabase.cpp
    core/CatalogueIndex.cpp
    core/AvatarAttachment.cpp
    core/AvatarLoadout.cpp
    core/AvatarLoadoutSync.cpp
    core/Skeleton.cpp
    core/SkinWeights.cpp
    core/RiggedMesh.cpp
    core/AnimationPlayer.cpp
    core/RiggedAvatar.cpp
    core/AvatarController.cpp
    core/AnimationItem.cpp
    core/AnimationManifest.cpp
    core/AnimationDatabase.cpp
    core/EmoteSystem.cpp
    core/Interactable.cpp
    core/SceneTypes.cpp
    core/Terrain.cpp
    core/Texture.cpp
    core/ProceduralMaterials.cpp
    core/Prefab.cpp
    core/ECS.cpp
    core/Hierarchy.cpp
    core/Physics.cpp
    core/PhysicsMaterial.cpp
    core/CollisionLayers.cpp
    core/OreNode.cpp
    core/Inventory.cpp
    core/Economy.cpp
    core/UpgradeSystem.cpp
    core/Shop.cpp
    core/VisualFeedback.cpp
    core/WorldProp.cpp
    core/Biome.cpp
    core/TimeOfDay.cpp
    core/Weather.cpp
    core/Wind.cpp
    core/Navigation.cpp
    core/Noise.cpp
    core/ProcessStats.cpp
    core/PerformanceDiagnostics.cpp
    core/Profiler.cpp
    core/Logger.cpp
    core/CrashReporter.cpp
    core/ResourcePaths.cpp
    core/Audio.cpp
    core/Scripting.cpp

    runtime/GameLoop.cpp

    platform/LinuxWindow.cpp
    platform/WindowsWindow.cpp

    net/ENetTransport.cpp
    net/ClientPrediction.cpp
    net/ServerReconciliation.cpp
    net/RemoteEvent.cpp
    net/NetworkIdentity.cpp
    net/Serialization.cpp
    net/NetworkStats.cpp
    net/RemoteEntityInterpolation.cpp
    net/RateLimiter.cpp
    net/InteractionValidation.cpp
    net/NetworkedMovement.cpp
    net/NetworkSession.cpp
    net/SessionHistory.cpp
    net/LanDiscoveryProtocol.cpp
    net/LanSessionAnnouncer.cpp
    net/LanSessionBrowser.cpp

    migration/RbxlxParser.cpp
    migration/InstanceTreeBuilder.cpp
    migration/AssetConverter.cpp
    migration/ScriptCompatShimLoader.cpp
    migration/ImportSafetyGuard.cpp

    safety/TrustSafetyService.cpp
    safety/TextClassifierStub.cpp
    safety/VoiceASRStub.cpp
    safety/ModerationPipeline.cpp
    safety/IPInfringementScanner.cpp
    safety/TextNormalize.cpp
    safety/CreatorIdentityGuard.cpp
    safety/AssetSafetyGuard.cpp

    anticheat/DeviceFingerprint.cpp
    anticheat/ExploitSignatureDB.cpp
    anticheat/RollingEventCounter.cpp
    anticheat/ClientIntegrityCheck.cpp
    anticheat/CurrencyAnomalyDetector.cpp

    moderation/ProfanityFilter.cpp
    moderation/MuteBlockRegistry.cpp
    moderation/ChatLog.cpp
    moderation/ReportLog.cpp
    moderation/TrustedCreatorRegistry.cpp
    moderation/ReviewQueue.cpp

    publishing/WorldMetadata.cpp
    publishing/PublishValidation.cpp
    publishing/WorldPackage.cpp
    publishing/WorldRegistry.cpp
    publishing/ThumbnailCapture.cpp

    tntwars/ClassSystem.cpp
    tntwars/Team.cpp
    tntwars/UltimateCharge.cpp
    tntwars/CinematicSequence.cpp
    tntwars/Projectile.cpp
    tntwars/ThrusterHp.cpp
    tntwars/LavaEruption.cpp
    tntwars/TorpedoStealth.cpp
    tntwars/RadarIntercept.cpp
    tntwars/MapDefinition.cpp
    tntwars/MapLayout.cpp
    tntwars/MapMaterial.cpp
    tntwars/MatchFlow.cpp
    tntwars/DestructibleGeometry.cpp
    tntwars/DestructibleGeometryVisual.cpp
    tntwars/MapLayoutVisual.cpp
    tntwars/Oxygen.cpp
    tntwars/SkyMapVisual.cpp
    tntwars/SkyMapTerrain.cpp
    tntwars/SpaceMapTerrain.cpp
    tntwars/AtmosphereZones.cpp
    tntwars/FlickerLight.cpp
    tntwars/SpaceTraversal.cpp
    tntwars/CombatMob.cpp
    tntwars/PvPNode.cpp
    tntwars/Explosives.cpp
    tntwars/Decal.cpp
    tntwars/Respawn.cpp
    tntwars/TntChargeVisual.cpp
    tntwars/ProjectileVisual.cpp
    tntwars/VolcanoMapVisual.cpp
    tntwars/UnderwaterMapVisual.cpp
    tntwars/TrenchesMapVisual.cpp
    tntwars/TntCharge.cpp
    tntwars/CombatFx.cpp
    tntwars/Scavenging.cpp
    tntwars/ScavengeNodeVisual.cpp
    tntwars/TraversalChallenge.cpp
    tntwars/TntWarsUpgrades.cpp
    tntwars/TntWarsUpgradeStation.cpp
    tntwars/Crafting.cpp
    tntwars/Missile.cpp
    tntwars/Movement.cpp
    tntwars/TrenchesWall.cpp
    tntwars/TrenchesCover.cpp
    tntwars/SkyPlatforms.cpp
    tntwars/TntWarsAntiCheat.cpp
    tntwars/TntWarsMatch.cpp

    trailer/TrailerTimeline.cpp
    trailer/TrailerCinematics.cpp
    trailer/TrailerScenes.cpp
    trailer/CaptureRig.cpp
    trailer/TrailerDirector.cpp
    trailer/TimeglassModel.cpp
    trailer/RenderShowcase.cpp

    miningsim/MiningSimRtx.cpp
    miningsim/Rarity.cpp
    miningsim/Zone.cpp
    miningsim/Portal.cpp
    miningsim/MiningTools.cpp
    miningsim/Forging.cpp
    miningsim/ZoneVisualTheme.cpp
    miningsim/Dungeon.cpp
    miningsim/Mob.cpp
    miningsim/Boss.cpp
    miningsim/PvpArena.cpp
    miningsim/DungeonVisual.cpp
    miningsim/MobVisual.cpp
    miningsim/ForgeVisual.cpp

    platform_adapters/UnifiedInput.cpp
    platform_adapters/adapters/WindowsAdapter.cpp
    platform_adapters/adapters/LinuxAdapter.cpp
    platform_adapters/adapters/MacOSAdapter.cpp
    platform_adapters/adapters/IOSAdapter.cpp
    platform_adapters/adapters/AndroidAdapter.cpp
    platform_adapters/adapters/XboxAdapter.cpp
    platform_adapters/adapters/PlayStationAdapter.cpp
    platform_adapters/adapters/SwitchAdapter.cpp

    marketplace/MarketplaceService.cpp
    marketplace/ListingReviewPipeline.cpp
    marketplace/adapters/SteamPaymentAdapter.cpp
    marketplace/adapters/StoreKitPaymentAdapter.cpp
    marketplace/adapters/PlayBillingPaymentAdapter.cpp
    marketplace/adapters/XboxPaymentAdapter.cpp
    marketplace/adapters/PSNPaymentAdapter.cpp
    marketplace/adapters/EShopPaymentAdapter.cpp

    accessibility/AccessibilityOptions.cpp

    analytics/TelemetryQueue.cpp
    analytics/TelemetrySender.cpp
    analytics/CrashReporter.cpp
)

target_include_directories(engine_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_features(engine_core PUBLIC cxx_std_20)
target_compile_definitions(engine_core PUBLIC
    ENGINE_SHADER_DIR="${ENGINE_SHADER_OUTPUT_DIR}"
    # Kronos ("Sound Design" world-building): mirrors ENGINE_SHADER_DIR's
    # own real "absolute build-tree path baked in as a #define" convention
    # (see that field's own comment) -- real, procedurally-synthesized
    # (no licensed/scraped asset) TNT/grenade explosion .wav files live
    # in engine/assets/audio, loaded via core::Audio::loadSound() at TNT
    # Wars live-mode startup.
    ENGINE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../assets"
    # Without this, glm::perspective()/glm::ortho() default to OpenGL's
    # clip-space Z range ([-1,1]) -- Vulkan requires [0,1] (its clip
    # volume is defined by 0<=z<=w, not -w<=z<=w). Depth *testing* still
    # happens to look right either way (VK_COMPARE_OP_LESS only needs
    # near<far, which both conventions preserve), which is how this went
    # unnoticed through every earlier visual check this session -- but
    # any code that reconstructs NDC-space positions and unprojects them
    # (Renderer::computeCascades()'s frustum-corner math) needs the actual
    # range to match what it assumes, or it fits shadow cascades to the
    # wrong volume -- exactly the "shadows missing/wrong depending on
    # camera angle" bug this fixes. PUBLIC so every consumer (engine_core's
    # own .cpp files, engine_runtime's main.cpp, studio/, tests/) compiles
    # every glm header inclusion with the same, correct convention -- a
    # per-file #define would be fragile (must precede the *first* glm
    # include in every TU) and easy to silently miss in a new file.
    GLM_FORCE_DEPTH_ZERO_TO_ONE
)
add_dependencies(engine_core engine_shaders)

target_link_libraries(engine_core PUBLIC
    SDL2::SDL2
    Vulkan::Headers
    volk
    vma::vma
    EnTT::EnTT
    glm::glm
    Jolt
    Luau.VM
    Luau.Compiler
    enet
    miniaudio::miniaudio
    stb::image
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(WIN32)
    target_link_libraries(engine_core PUBLIC ws2_32 winmm)
elseif(UNIX)
    # miniaudio dlopen()s the platform audio backend (ALSA/PulseAudio) at
    # runtime rather than being linked against it directly.
    target_link_libraries(engine_core PUBLIC dl m)
endif()

# ---------------------------------------------------------------------------
# engine_runtime: the shippable client/server binary (item 7 GameLoop lives
# here). Kronos ("Active Joining UI"): now links real ImGui too, for the
# real Home Screen/Session Browser shell (runtime/RuntimeShell.cpp) --
# still no Studio-only privileges (§5/§6 debugger hooks, no imguizmo, no
# docking dockspace); this is a real, separate, much smaller ImGui use
# than Studio's own dockable panel host, not a shared surface with it.
# ---------------------------------------------------------------------------
if(ENGINE_BUILD_RUNTIME)
    add_executable(engine_runtime
        main.cpp
        runtime/RuntimeShell.cpp
    )
    target_link_libraries(engine_runtime PRIVATE engine_core imgui::imgui)
endif()

# ---------------------------------------------------------------------------
# studio: the desktop Studio shell (item 9). Links engine_core for the exact
# same render graph, ECS and Luau VM the runtime uses, plus ImGui for the
# dockable panel host.
# ---------------------------------------------------------------------------
if(ENGINE_BUILD_STUDIO)
    add_executable(studio
        studio/StudioMain.cpp
        studio/StudioApp.cpp
        studio/OffscreenTarget.cpp
        studio/PluginManager.cpp
        studio/StudioEcsScriptApi.cpp
        studio/SceneManager.cpp
        studio/StudioStyle.cpp
        studio/StudioIcons.cpp
        studio/PluginChrome.cpp
        studio/Notification.cpp
        studio/EntityClassification.cpp
        studio/CreatorToolsSpawning.cpp
        studio/CreatorConsole.cpp
        studio/PreviewScene.cpp
        studio/ThumbnailCameraRig.cpp
        studio/panels/ViewportPanel.cpp
        studio/panels/ExplorerPanel.cpp
        studio/panels/InspectorPanel.cpp
        studio/panels/ScriptEditorPanel.cpp
        studio/panels/StatsPanel.cpp
        studio/panels/SceneSearchPanel.cpp
        studio/panels/DebugConsolePanel.cpp
        studio/plugins/AnimatorPlugin.cpp
        studio/plugins/AlignPlugin.cpp
        studio/plugins/MaterialPlugin.cpp
        studio/plugins/TerrainEditorPlugin.cpp
        studio/plugins/CreatorToolsPlugin.cpp
        studio/plugins/ParticleEditorPlugin.cpp
        studio/plugins/PrefabPlugin.cpp
        studio/plugins/ModelImporterPlugin.cpp
        studio/plugins/TexturePreviewPlugin.cpp
        studio/plugins/AudioPreviewPlugin.cpp
        studio/PluginManifest.cpp
        studio/LocalPluginDirectory.cpp
        studio/plugins/ScriptedPlugin.cpp
        studio/plugins/PluginBrowserPlugin.cpp
        studio/plugins/AvatarPreviewer.cpp
        studio/plugins/CataloguePanel.cpp
        studio/plugins/UploadAvatarItemPlugin.cpp
        studio/plugins/AnimationPreviewerPlugin.cpp
        studio/plugins/UploadAnimationPlugin.cpp
        studio/plugins/PhysicsPreviewPlugin.cpp
        studio/plugins/ShopPlugin.cpp
        studio/plugins/DiagnosticsPlugin.cpp
        studio/plugins/NetworkOverlayPlugin.cpp
        studio/plugins/ModerationPanel.cpp
        studio/plugins/PublishingPanel.cpp
        studio/plugins/LightingToolsPlugin.cpp
        studio/plugins/CreatorConsolePlugin.cpp
        studio/plugins/TimelineEditorPlugin.cpp
        studio/plugins/CreatorAssetBrowserPlugin.cpp
        studio/plugins/TntWarsPlugin.cpp
        studio/plugins/TrailerPanel.cpp
        studio/plugins/LauncherPlugin.cpp
        studio/ParticlePresets.cpp
    )
    target_link_libraries(studio PRIVATE engine_core imgui::imgui imguizmo::imguizmo)
endif()
