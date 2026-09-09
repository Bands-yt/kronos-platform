#include "core/PluginRegistry.hpp"

#include <algorithm>

namespace engine::core {

namespace {

// Plain data, not PluginMetadata itself -- PluginMetadata's `enabled` field
// has no meaning yet for a stub, and every entry below defaults it false;
// keeping the raw list is one field shorter and can't accidentally seed a
// stub as "enabled".
struct StubEntry {
    const char* id;
    const char* displayName;
    const char* description;
    PluginCategory category;
};

// The 100 essential plugins, 10 per category, in the exact order and
// grouping of the roadmap list -- this table IS the roadmap, so a new
// plugin gets added here first and nowhere else drives the count.
constexpr StubEntry kStubEntries[] = {
    // Core Editor & UX Workflow
    {"CommandPalette", "Command Palette", "Fuzzy-searchable action launcher for every editor command.", PluginCategory::CoreEditorUx},
    {"WorkspaceLayoutManager", "Workspace Layout Manager", "Save, name, and swap between ImGui dockspace layouts.", PluginCategory::CoreEditorUx},
    {"RadialContextMenu", "Radial Context Menu", "Pie-style context menu for viewport actions under the cursor.", PluginCategory::CoreEditorUx},
    {"FocusMode", "Focus Mode", "Hides all panels but the viewport for distraction-free editing.", PluginCategory::CoreEditorUx},
    {"KeymapRemapper", "Keymap Remapper", "Rebind editor shortcuts and export/import keymap presets.", PluginCategory::CoreEditorUx},
    {"ProjectExplorerFilter", "Project Explorer Filter", "Type and tag filtering for the asset/project tree.", PluginCategory::CoreEditorUx},
    {"MultiTabSceneDock", "Multi-Tab Scene Dock", "Open and switch between multiple scenes as dockable tabs.", PluginCategory::CoreEditorUx},
    {"SmartNotificationHub", "Smart Notification Hub", "Central toast/log feed for build, import, and save events.", PluginCategory::CoreEditorUx},
    {"ColorPaletteManager", "Color Palette Manager", "Create and share named color swatches across materials and UI.", PluginCategory::CoreEditorUx},
    {"BookmarkPinPanel", "Bookmark & Pin Panel", "Pin scene entities and viewport locations for quick recall.", PluginCategory::CoreEditorUx},

    // Scripting & Logic
    {"LuauVisualGraphEditor", "Luau Visual Graph Editor", "Node-graph front end that compiles to Luau script.", PluginCategory::ScriptingLogic},
    {"LiveREPLConsole", "Live REPL Console", "Interactive Luau REPL against the running scene.", PluginCategory::ScriptingLogic},
    {"IntegratedDebugger", "Integrated Debugger", "Breakpoints, stepping, and call-stack inspection for scripts.", PluginCategory::ScriptingLogic},
    {"ScriptProfiler", "Script Profiler", "Per-function timing and hot-path capture for Luau scripts.", PluginCategory::ScriptingLogic},
    {"AutoDocGenerator", "Auto Doc Generator", "Generates API docs from script and binding annotations.", PluginCategory::ScriptingLogic},
    {"CppBindingExporter", "C++ Binding Exporter", "Exports native engine APIs as Luau bindings.", PluginCategory::ScriptingLogic},
    {"UnitTestRunnerGUI", "Unit Test Runner GUI", "Runs and reports script/engine unit tests inside the editor.", PluginCategory::ScriptingLogic},
    {"ASTCodeRefactorer", "AST Code Refactorer", "AST-driven rename/extract/inline refactors for scripts.", PluginCategory::ScriptingLogic},
    {"LiveScriptHotReload", "Live Script Hot Reload", "Reloads edited scripts into the running session without a restart.", PluginCategory::ScriptingLogic},
    {"LuauTypeDefinitionExporter", "Luau Type Definition Exporter", "Emits .d.luau type stubs for editor autocomplete.", PluginCategory::ScriptingLogic},

    // 3D Modeling & Mesh Editing
    {"ProceduralGeometryGenerator", "Procedural Geometry Generator", "Parametric primitive and geometry generation nodes.", PluginCategory::ModelingMeshEditing},
    {"ModifierStack", "Modifier Stack", "Non-destructive, stackable mesh modifiers.", PluginCategory::ModelingMeshEditing},
    {"AutoUVUnwrapper", "Auto UV Unwrapper", "Automatic UV unwrapping with seam angle control.", PluginCategory::ModelingMeshEditing},
    {"TexelDensityChecker", "Texel Density Checker", "Visualizes and reports per-mesh texel density.", PluginCategory::ModelingMeshEditing},
    {"PolyReductionDecimator", "Poly Reduction Decimator", "Mesh decimation with target triangle-count control.", PluginCategory::ModelingMeshEditing},
    {"QuadRemesherBridge", "Quad Remesher Bridge", "Converts triangulated meshes to quad-dominant topology.", PluginCategory::ModelingMeshEditing},
    {"CurveSweepExtruder", "Curve Sweep Extruder", "Extrudes a profile mesh along a spline curve.", PluginCategory::ModelingMeshEditing},
    {"CSGBooleanEngine", "CSG Boolean Engine", "Union/subtract/intersect boolean operations on meshes.", PluginCategory::ModelingMeshEditing},
    {"VertexColorPainter", "Vertex Color Painter", "Brush-based per-vertex color painting.", PluginCategory::ModelingMeshEditing},
    {"NormalVectorEditor", "Normal Vector Editor", "Manual vertex normal editing and smoothing groups.", PluginCategory::ModelingMeshEditing},

    // Materials & Shading
    {"VisualShaderGraph", "Visual Shader Graph", "Node-based shader authoring compiled to the render pipeline.", PluginCategory::MaterialsShading},
    {"PBRMaterialSynthesizer", "PBR Material Synthesizer", "Generates PBR texture sets from a base pattern or photo.", PluginCategory::MaterialsShading},
    {"MaterialPresetLibrary", "Material Preset Library", "Browsable library of ready-made PBR material presets.", PluginCategory::MaterialsShading},
    {"ChannelPacker", "Channel Packer", "Packs grayscale maps into RGBA channel-packed textures.", PluginCategory::MaterialsShading},
    {"ShaderMinifier", "Shader Minifier", "Strips and optimizes shader source for shipping builds.", PluginCategory::MaterialsShading},
    {"TriplanarMapping", "Triplanar Mapping", "Seamless triplanar texture projection for materials.", PluginCategory::MaterialsShading},
    {"SubsurfaceScatteringStudio", "Subsurface Scattering Studio", "Tuning UI for subsurface-scattering material parameters.", PluginCategory::MaterialsShading},
    {"DecalProjectionManager", "Decal Projection Manager", "Places and manages projected decals on scene geometry.", PluginCategory::MaterialsShading},
    {"ParallaxOcclusionBuilder", "Parallax Occlusion Builder", "Authoring tool for parallax-occlusion height materials.", PluginCategory::MaterialsShading},
    {"ShaderFrameCostAnalyzer", "Shader Frame Cost Analyzer", "Reports per-shader GPU cost across a captured frame.", PluginCategory::MaterialsShading},

    // Animation & Cinematics
    {"NLETimelineSequencer", "NLE Timeline Sequencer", "Non-linear editing timeline for cinematic sequences.", PluginCategory::AnimationCinematics},
    {"InverseKinematicsSuite", "Inverse Kinematics Suite", "IK rigs and solvers for skeletal animation.", PluginCategory::AnimationCinematics},
    {"AnimationBlendTree", "Animation Blend Tree", "State-machine and blend-tree authoring for animation clips.", PluginCategory::AnimationCinematics},
    {"CameraSplineRail", "Camera Spline Rail", "Attaches a cinematic camera to an editable spline rail.", PluginCategory::AnimationCinematics},
    {"RootMotionExtractor", "Root Motion Extractor", "Extracts root motion from animation clips into movement data.", PluginCategory::AnimationCinematics},
    {"PoseLibraryManager", "Pose Library Manager", "Stores and applies reusable named skeletal poses.", PluginCategory::AnimationCinematics},
    {"BVHMotionCaptureImporter", "BVH Motion Capture Importer", "Imports BVH mocap data onto a rig's skeleton.", PluginCategory::AnimationCinematics},
    {"RagdollGenerator", "Ragdoll Generator", "Auto-generates physics ragdolls from a skeleton hierarchy.", PluginCategory::AnimationCinematics},
    {"FacialBlendshapeDriver", "Facial Blendshape Driver", "Drives facial blendshapes from curves or audio visemes.", PluginCategory::AnimationCinematics},
    {"SecondaryMotionPhysics", "Secondary Motion Physics", "Jiggle/cloth-style secondary motion bones on a rig.", PluginCategory::AnimationCinematics},

    // World Building & Environment
    {"HeightmapTerrainSculptor", "Heightmap Terrain Sculptor", "Brush-based raise/lower/smooth sculpting on terrain heightmaps.", PluginCategory::WorldBuildingEnvironment},
    {"FoliageScatterBrush", "Foliage Scatter Brush", "Paints foliage instances across terrain with density control.", PluginCategory::WorldBuildingEnvironment},
    {"RoadRiverSplineGenerator", "Road & River Spline Generator", "Generates roads and rivers that conform to terrain from a spline.", PluginCategory::WorldBuildingEnvironment},
    {"ProceduralBuildingStudio", "Procedural Building Studio", "Rule-based procedural building and structure generation.", PluginCategory::WorldBuildingEnvironment},
    {"DynamicAtmosphereController", "Dynamic Atmosphere Controller", "Runtime sky, cloud, and atmospheric scattering control.", PluginCategory::WorldBuildingEnvironment},
    {"DayNightCycleSequencer", "Day/Night Cycle Sequencer", "Drives sun angle and lighting through a scripted day/night cycle.", PluginCategory::WorldBuildingEnvironment},
    {"OcclusionCullingVolumeGen", "Occlusion Culling Volume Generator", "Auto-generates occlusion culling volumes for a scene.", PluginCategory::WorldBuildingEnvironment},
    {"AutoLODGenerator", "Auto LOD Generator", "Generates mesh LOD chains at configurable reduction ratios.", PluginCategory::WorldBuildingEnvironment},
    {"LightmapBaker", "Lightmap Baker", "Bakes static lighting into scene lightmaps.", PluginCategory::WorldBuildingEnvironment},
    {"WaterSurfaceEngine", "Water Surface Engine", "Simulated water surfaces with waves, foam, and shorelines.", PluginCategory::WorldBuildingEnvironment},

    // Lighting & Rendering
    {"PathTracingViewportToggle", "Path Tracing Viewport Toggle", "Switches the viewport between rasterized and path-traced preview.", PluginCategory::LightingRendering},
    {"VolumetricFogLightShafts", "Volumetric Fog & Light Shafts", "Volumetric fog and god-ray light shaft rendering controls.", PluginCategory::LightingRendering},
    {"ACESColorGradingStack", "ACES Color Grading Stack", "ACES-based tonemapping and color grading pipeline.", PluginCategory::LightingRendering},
    {"SSAOTuner", "SSAO Tuner", "Tuning UI for screen-space ambient occlusion parameters.", PluginCategory::LightingRendering},
    {"SSRManager", "SSR Manager", "Screen-space reflection quality and fallback settings.", PluginCategory::LightingRendering},
    {"LensFlareBloomCustomizer", "Lens Flare & Bloom Customizer", "Authoring tool for lens flare and bloom post-process effects.", PluginCategory::LightingRendering},
    {"DynamicLightprobeAllocator", "Dynamic Lightprobe Allocator", "Places and updates light probes for dynamic global illumination.", PluginCategory::LightingRendering},
    {"VignetteFilmGrainStack", "Vignette & Film Grain Stack", "Vignette and film grain post-process layer controls.", PluginCategory::LightingRendering},
    {"DepthOfFieldStudio", "Depth of Field Studio", "Camera depth-of-field authoring with focus distance preview.", PluginCategory::LightingRendering},
    {"ShadowMapResolutionMatrix", "Shadow Map Resolution Matrix", "Per-light shadow map resolution and cascade configuration.", PluginCategory::LightingRendering},

    // Physics & Simulation
    {"JoltPhysicsTuner", "Jolt Physics Tuner", "Tuning UI for Jolt physics world and solver settings.", PluginCategory::PhysicsSimulation},
    {"DestructionMeshFracturer", "Destruction Mesh Fracturer", "Pre-fractures meshes into physics-driven destructible pieces.", PluginCategory::PhysicsSimulation},
    {"SoftBodyClothSimulator", "Soft Body Cloth Simulator", "Cloth and soft-body simulation authoring and pinning.", PluginCategory::PhysicsSimulation},
    {"GPUParticleEmitterStudio", "GPU Particle Emitter Studio", "GPU-simulated particle emitter authoring tool.", PluginCategory::PhysicsSimulation},
    {"ConvexHullGenerator", "Convex Hull Generator", "Generates convex collision hulls from render meshes.", PluginCategory::PhysicsSimulation},
    {"PhysicsMaterialLibrary", "Physics Material Library", "Library of reusable friction/restitution physics materials.", PluginCategory::PhysicsSimulation},
    {"VehicleDynamicsWizard", "Vehicle Dynamics Wizard", "Guided setup for wheeled vehicle physics rigs.", PluginCategory::PhysicsSimulation},
    {"BuoyancyFluidSolver", "Buoyancy Fluid Solver", "Buoyancy forces and simple fluid volume interaction.", PluginCategory::PhysicsSimulation},
    {"KinematicCharacterController", "Kinematic Character Controller", "Capsule-based kinematic controller for player characters.", PluginCategory::PhysicsSimulation},
    {"CollisionMatrixVisualizer", "Collision Matrix Visualizer", "Visualizes and edits the collision layer interaction matrix.", PluginCategory::PhysicsSimulation},

    // Audio & Soundscapes
    {"SpatialAudioNodeGraph", "Spatial Audio Node Graph", "Node-graph authoring for 3D spatialized audio routing.", PluginCategory::AudioSoundscapes},
    {"AudioZoneOcclusion", "Audio Zone Occlusion", "Geometry-based audio occlusion and zone muffling.", PluginCategory::AudioSoundscapes},
    {"InteractiveMusicWeaver", "Interactive Music Weaver", "Layers and transitions adaptive music stems by game state.", PluginCategory::AudioSoundscapes},
    {"ReverbDSPFXInspector", "Reverb DSP FX Inspector", "Inspects and tunes reverb and DSP effect chains.", PluginCategory::AudioSoundscapes},
    {"VoiceoverSubtitleSync", "Voiceover Subtitle Sync", "Aligns subtitle timing to voiceover audio tracks.", PluginCategory::AudioSoundscapes},
    {"BinauralAudioSimulator", "Binaural Audio Simulator", "Binaural rendering preview for headphone spatial audio.", PluginCategory::AudioSoundscapes},
    {"WaveformAudioTrimmer", "Waveform Audio Trimmer", "Waveform view for trimming and fading audio clips.", PluginCategory::AudioSoundscapes},
    {"RealtimeAudioAnalyzer", "Realtime Audio Analyzer", "Live spectrum and loudness analysis of playing audio.", PluginCategory::AudioSoundscapes},
    {"DopplerEffectController", "Doppler Effect Controller", "Per-source doppler shift strength and falloff control.", PluginCategory::AudioSoundscapes},
    {"AudioPoolAllocator", "Audio Pool Allocator", "Manages pooled audio voice allocation and priority stealing.", PluginCategory::AudioSoundscapes},

    // Build, Network & Pipeline
    {"MultiPlatformExporter", "Multi-Platform Exporter", "Builds and packages the project for multiple target platforms.", PluginCategory::BuildNetworkPipeline},
    {"HeadlessServerExporter", "Headless Server Exporter", "Builds a headless dedicated-server variant of the project.", PluginCategory::BuildNetworkPipeline},
    {"GitLFSVersionHelper", "Git LFS Version Helper", "Helps track and lock large binary assets under Git LFS.", PluginCategory::BuildNetworkPipeline},
    {"MultiplayerLatencySimulator", "Multiplayer Latency Simulator", "Simulates latency, jitter, and packet loss for netcode testing.", PluginCategory::BuildNetworkPipeline},
    {"AssetBundleCompressor", "Asset Bundle Compressor", "Compresses and packs assets into shippable bundles.", PluginCategory::BuildNetworkPipeline},
    {"VRAMMemoryProfiler", "VRAM Memory Profiler", "Tracks per-resource GPU memory usage across a session.", PluginCategory::BuildNetworkPipeline},
    {"CrashDumpAnalyzer", "Crash Dump Analyzer", "Parses crash dumps into readable stack traces.", PluginCategory::BuildNetworkPipeline},
    {"DeviceRemoteProfilerBridge", "Device Remote Profiler Bridge", "Streams profiling data from a remote/target device.", PluginCategory::BuildNetworkPipeline},
    {"LocalizationStringTableEditor", "Localization String Table Editor", "Edits and validates localized string tables.", PluginCategory::BuildNetworkPipeline},
    {"AutomatedCICDExporter", "Automated CI/CD Exporter", "Triggers automated build exports from a CI/CD pipeline.", PluginCategory::BuildNetworkPipeline},
};

} // namespace

void PluginRegistry::registerAll() {
    modules_.reserve(modules_.size() + std::size(kStubEntries));
    for (const auto& entry : kStubEntries) {
        PluginMetadata metadata;
        metadata.id = entry.id;
        metadata.displayName = entry.displayName;
        metadata.description = entry.description;
        metadata.category = entry.category;
        modules_.push_back(std::make_unique<StubPluginModule>(std::move(metadata)));
    }
}

std::vector<const PluginMetadata*> PluginRegistry::byCategory(PluginCategory category) const {
    std::vector<const PluginMetadata*> result;
    for (const auto& module : modules_) {
        if (module->metadata().category == category) result.push_back(&module->metadata());
    }
    return result;
}

const IPluginModule* PluginRegistry::find(const std::string& id) const {
    auto it = std::find_if(modules_.begin(), modules_.end(),
                            [&id](const auto& module) { return module->metadata().id == id; });
    return it == modules_.end() ? nullptr : it->get();
}

} // namespace engine::core
