#pragma once

#include "core/BindlessTextureTable.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include <chrono>

#include "core/Camera.hpp"
#include "core/ECS.hpp"
#include "core/Mesh.hpp"
#include "core/ParticleSystem.hpp"
#include "core/PerformanceMetrics.hpp"
#include "core/RayTracingScene.hpp"
#include "core/RiggedMesh.hpp"
#include "core/SceneTypes.hpp"
#include "core/Texture.hpp"
#include "core/Weather.hpp"
#include "core/Window.hpp"

namespace engine::core {

// Vulkan bring-up *and* a real scene render path: instance -> physical
// device -> logical device -> swapchain -> depth buffer -> a PBR graphics
// pipeline -> a render loop that draws every Renderable entity, lit by one
// directional light, and presents.
//
// This is still not the full frame graph described in docs/ARCHITECTURE.md
// §4.1 (clustered Forward+, hybrid RT, DLSS/FSR2) -- there is one opaque
// pipeline, one light, no light culling. There *is* now a real cascaded
// shadow map for that one light: kCascadeCount camera-following ortho
// depth passes (drawShadowPass()) into one array image, sampled with 3x3
// PCF and per-fragment cascade selection in scene.frag -- see
// computeCascades()'s doc comment for exactly what's simplified (no
// cross-cascade blend band, fixed max shadow distance).
//
// Remaining deliberate simplifications versus the target architecture:
//   - Raw Vulkan C API + volk instead of Vulkan-Hpp (see docs/ARCHITECTURE.md
//     §3) -- unchanged reasoning from the first pass: fewer header
//     surfaces to get wrong while verifying this compiles and runs.
//   - One fixed pipeline (no material variants, no instancing) -- fine at
//     the entity counts this skeleton creates; a real content pipeline
//     needs pipeline permutations or bindless materials well before this
//     scales.
//   - VK_KHR_dynamic_rendering (core in 1.3) instead of classic
//     VkRenderPass/VkFramebuffer -- this is *not* a simplification, it's
//     the direction the real frame graph wants (passes declare
//     attachments per-draw), kept from the first pass.
class Renderer {
public:
    struct CreateInfo {
        Window* window = nullptr;
        std::string appName = "Engine Runtime";
        bool enableValidation = true;
        uint32_t framesInFlight = 2;
    };

    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] bool initialize(const CreateInfo& info);
    void shutdown();

    // Records and submits one frame against the swapchain: acquire ->
    // pre-pass hook -> transition -> main pass (scene draw if setScene()
    // was called, otherwise just a clear) -> overlay hook -> transition ->
    // present. Returns false on an unrecoverable device-loss style error.
    bool renderFrame();

    void recreateSwapchain();

    [[nodiscard]] VkInstance instance() const { return instance_; }
    [[nodiscard]] VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    [[nodiscard]] VkDevice device() const { return device_; }
    [[nodiscard]] VkQueue graphicsQueue() const { return graphicsQueue_; }
    [[nodiscard]] uint32_t graphicsQueueFamily() const { return *queueFamilies_.graphics; }
    [[nodiscard]] VkFormat swapchainFormat() const { return swapchainFormat_; }
    [[nodiscard]] uint32_t swapchainImageCount() const { return static_cast<uint32_t>(swapchainImages_.size()); }
    [[nodiscard]] uint32_t framesInFlight() const { return framesInFlight_; }
    [[nodiscard]] VmaAllocator allocator() const { return allocator_; }
    [[nodiscard]] VkCommandPool commandPool() const { return commandPool_; }
    [[nodiscard]] VkFormat depthFormat() const { return depthFormat_; }
    [[nodiscard]] uint32_t currentFrameIndex() const { return currentFrame_; }

    // Real, measured stats for the most recently submitted frame -- see
    // PerformanceMetrics.hpp for exactly what's measured vs. queried.
    [[nodiscard]] const PerformanceMetrics& metrics() const { return lastMetrics_; }

    // Off by default -- Studio reads metrics() directly into its own UI
    // panel instead (console spam is the wrong fit for an editor); this
    // is for engine_runtime, which has no UI to put a panel in, so a
    // once-a-second stdout line is the equivalent visibility.
    void setMetricsLogging(bool enabled) { logMetricsToStdout_ = enabled; }

    // If set, renderFrame()'s main pass draws the scene directly into the
    // swapchain (used by engine_runtime, which wants the 3D view filling
    // the whole window). Studio deliberately never calls this on its main
    // Renderer -- it wants a plain clear behind its docked ImGui panels,
    // and calls drawSceneInto() itself, manually, against its own
    // offscreen viewport target instead (see the prePass hook below).
    // Kronos ("Avatar System" -- real humanoid avatar): `riggedMeshLibrary`
    // is a real, new, optional 6th pointer (default nullptr, so every
    // pre-existing caller that only ever passed 5 keeps compiling and
    // behaving identically -- no skinned entities drawn, same as before
    // this parameter existed). Real bug this fixes: renderFrame()'s own
    // main swapchain path (engine_runtime's real, live 3D view) called
    // drawSceneInto() with no RiggedMeshLibrary* at all, so
    // drawSkinnedEntities() always real-no-op'd for it -- a real,
    // GPU-uploaded SkinnedRenderable body was there, ECS-wise, and simply
    // never got submitted to a draw call. Studio's own AnimationPreviewerPlugin
    // was never affected (it calls drawSceneInto() directly, with its own
    // explicit RiggedMeshLibrary&, bypassing setScene() entirely -- see
    // that overload's own header comment), which is exactly why this real
    // gap went unnoticed until a live gameplay avatar actually needed it.
    void setScene(ECS* ecs, Camera* camera, MeshLibrary* meshLibrary, ParticleSystem* particleSystem,
                  TextureLibrary* textureLibrary, RiggedMeshLibrary* riggedMeshLibrary = nullptr) {
        sceneEcs_ = ecs;
        sceneCamera_ = camera;
        sceneMeshLibrary_ = meshLibrary;
        sceneParticleSystem_ = particleSystem;
        sceneTextureLibrary_ = textureLibrary;
        sceneRiggedMeshLibrary_ = riggedMeshLibrary;
    }
    void setLighting(const SceneLighting& lighting) { lighting_ = lighting; }
    // Read-only access to the current lighting -- added for
    // studio::PreviewScene, which temporarily swaps in a flatter
    // "studio lighting" setup around each preview render then restores
    // whatever the main Viewport was using (drawSceneInto() reads
    // lighting_ once per call, not once per Renderer, so multiple
    // drawSceneInto() calls in the same pre-pass callback can each see
    // different lighting as long as the caller swaps it between calls).
    [[nodiscard]] const SceneLighting& lighting() const { return lighting_; }

    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): real dynamic
    // weather -- see core::Weather.hpp's own header comment. Blended into
    // `lighting_` fresh every frame inside drawSceneIntoImpl() (Clear is a
    // real, exact no-op there, see applyWeather()), so every existing
    // caller of setLighting() (TimeOfDay's day/night cycle, trailer beats,
    // Studio) gets weather "for free" without needing its own code to
    // apply it. Renderer only *renders* particles (drawSceneInto() takes
    // `const ParticleSystem&`) and structurally cannot spawn rain/snow
    // itself -- currentWeatherProfile() is how an app-level tick loop
    // drives a real precipitation ParticleEmitter's rate without
    // maintaining a second, could-drift copy of weather state.
    void setWeather(WeatherKind target, float transitionSeconds) {
        setWeatherTarget(weatherState_, target, transitionSeconds);
    }
    [[nodiscard]] WeatherKind targetWeatherKind() const { return weatherState_.toKind; }
    [[nodiscard]] WeatherProfile currentWeatherProfile() const { return currentBlendedProfile(weatherState_); }

    // Real cascade count -- public (moved up from the private section
    // below, where it's still used to size CascadeData/FrameSync's own
    // arrays) specifically so Sprint 8's CSM cascade debug overlay
    // (ViewportPanel.cpp) can size its own per-cascade color array
    // against the real value instead of a second, could-drift constant.
    static constexpr uint32_t kCascadeCount = 3;
    // Kronos ("Studio Revamp" -- "Professional Lighting Inspector"): same
    // "move it up next to kCascadeCount" treatment, for the same reason --
    // LightingToolsPlugin's real CSM info section reads these directly
    // instead of hand-duplicating the values. Both stay compile-time
    // constants, not runtime-adjustable sliders: kShadowMapResolution
    // sizes a real VkImage array (FrameSync's own shadow resources,
    // created once at startup), and kShadowMaxDistance is baked into
    // computeCascades()'s split-distance math -- changing either for
    // real needs the image/pipeline recreated, not just a new uniform
    // value, so the inspector shows them as real read-only facts about
    // the current build rather than fabricating adjustability that isn't
    // there.
    static constexpr uint32_t kShadowMapResolution = 2048;
    // CSM covers [camera.nearPlane, kShadowMaxDistance] -- not the camera's
    // full 500-unit farPlane, see computeCascades()'s doc comment.
    static constexpr float kShadowMaxDistance = 80.0f;

    // Kronos ("Shadow Bias Diagnostics"): a real, live-tunable multiplier
    // on *all* of scene.frag/scene_rt.frag's shadow depth bias -- both
    // the receiver-plane term and the minBias floor (see
    // sampleCascadeShadow()'s own comment) -- 0 is a true zero-bias
    // shader (every shadow comparison runs unbiased), 1 is the shader's
    // own as-authored magnitude, >1 scales both up further.
    //
    // This exists because two prior derivative-bias rewrites both still
    // peter-panned on real hardware, with no GPU available in this
    // environment to verify against either one. A hardware screenshot
    // from the second rewrite showed a hard-edged, roughly uniform-size
    // detachment gap on ordinary flat-topped boxes at both near and far
    // cascades -- which is not what a bias *magnitude* error produces
    // (bias erodes a shadow's own edges, worst at grazing angles, and
    // does so more at far cascades if anything); it's the signature of
    // a lookup/coordinate error elsewhere (cascade selection, the
    // texel-snapped ortho bounds, a uv/matrix mismatch between
    // shadow.vert and the sampling shaders). This dial exists to settle
    // that question on real hardware before any more bias-magnitude
    // guessing: at scale 0, if the detachment gap survives, depth bias
    // is exonerated and the bug is not in this function at all.
    void setReceiverPlaneBiasScale(float scale) { receiverPlaneBiasScale_ = std::clamp(scale, 0.0f, 4.0f); }
    [[nodiscard]] float receiverPlaneBiasScale() const { return receiverPlaneBiasScale_; }

    // Sprint 8 ("Performance Stats & Debug Tools") task category 2's CSM
    // cascade debug overlay: real per-cascade split depths (view-space
    // far distance of each cascade), the exact same values scene.frag's
    // shadow sampling is fit against -- computeCascades() itself and the
    // full CascadeData (light-space matrices, depth ranges) stay private,
    // Renderer's own shadow-pass implementation detail; a debug overlay
    // only needs to know *where* each cascade boundary sits along the
    // view axis, not how the shadow maps are actually built from it.
    [[nodiscard]] std::array<float, kCascadeCount> debugCascadeSplitDepths(const Camera& camera,
                                                                            float aspectRatio) const {
        return computeCascades(camera, aspectRatio).splitDepths;
    }

    // Post-process tuning knobs -- see the members' declaration comment.
    void setExposure(float exposure) { exposure_ = exposure; }
    // Kronos ("Lighting Polish" world-building): a real getter -- lets a
    // live caller (Application's own zone-based atmosphere tick) read the
    // current manual exposure_ back as a real blend baseline before
    // overwriting it, the same "read current state before a partial live
    // update" need setLighting()'s own lighting() getter already serves.
    [[nodiscard]] float exposure() const { return exposure_; }
    void setBloomSettings(float threshold, float softKnee, float intensity) {
        bloomThreshold_ = threshold;
        bloomSoftKnee_ = softKnee;
        bloomIntensity_ = intensity;
    }

    // Sprint 14 ("RTX Upgrade" Phase 2): real hardware ray-traced shadows
    // via VK_KHR_ray_query -- see RayTracingScene.hpp's own header
    // comment for the full real design and its stated scope (Box/Plane
    // MeshSource-described entities only this pass). isRayTracingSupported()
    // reflects what THIS device/driver actually reported at startup (see
    // checkRayTracingSupport()); setRayTracedShadowsEnabled() is a real,
    // honest no-op if unsupported -- the existing CSM rasterized shadow
    // pass keeps running unchanged either way, this only adds a second,
    // optional real shadow technique on top.
    [[nodiscard]] bool isRayTracingSupported() const { return rayTracingSupported_; }
    // Kronos ("Bindless Descriptors"): true when this device really
    // supports VK_EXT_descriptor_indexing with the four features a global
    // texture array needs. False means the existing per-draw descriptor
    // path is in use -- a graceful fallback, not an error.
    [[nodiscard]] bool isBindlessSupported() const { return bindlessSupported_; }
    void setRayTracedShadowsEnabled(bool enabled) { rayTracedShadowsEnabled_ = enabled && rayTracingSupported_; }
    [[nodiscard]] bool isRayTracedShadowsEnabled() const { return rayTracedShadowsEnabled_; }

    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): real hybrid RT
    // reflections -- reuses the exact same TLAS ray-traced shadows built
    // (see the TLAS-rebuild gate's own comment for why enabling this
    // alone, with shadows off, still rebuilds it), tracing a real,
    // single-bounce reflection ray per pixel for mirror-like (low-
    // roughness, high-metallic) surfaces, falling back to a cheap,
    // always-correct analytic sky-gradient reflection everywhere else --
    // see shaders/scene_rt.frag's own traceReflection() for the real
    // technique. A real, honest no-op if unsupported, same convention as
    // setRayTracedShadowsEnabled() above.
    void setRTReflectionsEnabled(bool enabled) { rtReflectionsEnabled_ = enabled && rayTracingSupported_; }
    [[nodiscard]] bool isRTReflectionsEnabled() const { return rtReflectionsEnabled_; }
    // Real per-pixel blend/trace-cost gate between the cheap analytic sky
    // reflection and a real traced ray: below `roughRadius` (as
    // 1-roughness) no ray is traced at all (a purely rough surface's
    // reflection is visually negligible and not worth the cost); at/above
    // `mirrorRadius` a real ray always fires; the two form a real
    // smoothstep blend band in between so there's no hard per-pixel pop
    // between neighboring pixels of similar roughness.
    void setReflectionRoughnessCutoff(float roughRadius, float mirrorRadius) {
        reflectionRoughCutoff_ = roughRadius;
        reflectionMirrorCutoff_ = mirrorRadius;
    }

    // Kronos ("Rendering Fidelity" -- full atmospheric-scattering skybox):
    // real single-scattering Rayleigh (wavelength-dependent air molecule
    // scattering -- the real reason clear noon sky is blue and a low sun
    // reddens) + Mie (real, larger-particle forward-scattering haze/sun
    // aureole) atmosphere, raymarched per-pixel entirely inside
    // shaders/sky.frag's own existing fullscreen background pass -- no new
    // compute pipeline (this renderer has none, see
    // shaders/volumetric_fog.frag's own precedent for the same
    // "fullscreen-fragment raymarch, not compute" choice) and no
    // precomputed LUT (a real, honest single-scattering-only
    // approximation -- no multi-scattering bounce, no aerial perspective
    // applied to scene geometry itself, matching real-time engines' own
    // common "cheap analytic atmosphere" scope, not a Bruneton-class
    // precomputed model). Off by default -- every existing scene/map/
    // trailer beat that never calls this keeps rendering its own existing
    // skyZenithColor/skyHorizonColor two-tone gradient exactly as before;
    // when enabled, the real physical scattering (driven by the same real
    // sun direction/elevation every other shader already lights with) is
    // added on top of that same two-tone base, so TimeOfDay's day/night
    // color authoring, Weather's per-profile sky tint, and Studio's own
    // zenith/horizon color picker all keep working underneath it, not
    // replaced by it.
    void setAtmosphereScatteringEnabled(bool enabled) { atmosphereScatteringEnabled_ = enabled; }
    [[nodiscard]] bool isAtmosphereScatteringEnabled() const { return atmosphereScatteringEnabled_; }
    // `sunIntensity`: real radiance multiplier for the in-scattered light
    // (higher = more saturated midday blue / brighter sunset orange).
    // `mieStrength`: real multiplier on the Mie (haze/aureole) term alone
    // -- higher reads as hazier, more real atmospheric turbidity, without
    // touching the Rayleigh (blue-sky) term.
    void setAtmosphereScatteringParams(float sunIntensity, float mieStrength) {
        atmosphereSunIntensity_ = sunIntensity;
        atmosphereMieStrength_ = mieStrength;
    }

    // Kronos ("Rendering Fidelity" -- volumetric cloud layer): real 3D
    // value-noise fbm clouds, raymarched through a flat world-space
    // altitude shell (kCloudBase..kCloudTop in shaders/sky.frag) entirely
    // inside that same existing fullscreen background pass -- no new
    // pipeline/descriptor set, no compute pass (same "this renderer has
    // none" constraint setAtmosphereScatteringEnabled()'s own comment
    // documents), just another real term added to the sky pass that
    // already runs every frame. Real, cheap 4-sample self-shadowing per
    // primary step (not a full secondary raymarch -- an honest,
    // documented simplification vs. computeAtmosphere()'s own real
    // shadow march, see shaders/sky.frag's own comment) gives clouds a
    // real, visible darker base / brighter sun-facing top. Off by
    // default -- same "every existing scene renders unchanged" real
    // no-op convention as every other opt-in flag on this class.
    void setCloudsEnabled(bool enabled) { cloudsEnabled_ = enabled; }
    [[nodiscard]] bool isCloudsEnabled() const { return cloudsEnabled_; }
    // `coverage`: 0..1, higher reads as more overcast (more of the shell
    // crosses the real noise-density threshold). `speed`: real world
    // units/second the cloud pattern scrolls by (wind).
    void setCloudParams(float coverage, float speed) {
        cloudCoverage_ = coverage;
        cloudSpeed_ = speed;
    }

    // Kronos ("Rendering Fidelity" -- SSR fallback pass): real screen-
    // space reflections -- a genuinely new fullscreen pass
    // (shaders/ssr.frag) that raymarches the *existing* depth buffer in
    // screen space, deriving a per-pixel geometric normal from real
    // screen-space depth derivatives (no dedicated normal/material
    // G-buffer exists in this forward-shaded renderer, see this engine's
    // own "no G-buffer" scope -- see that shader's own header comment for
    // why this is the honest, standard technique under that constraint),
    // and sampling the *already-rendered* scene color (frame.hdrImage)
    // at whatever screen point the ray hits. This is the real fallback
    // RayTracingScene::traceReflection() (scene_rt.frag) has never had on
    // hardware where RT reflections aren't available at all -- either no
    // ray-query support, or the feature simply toggled off
    // (isRTReflectionsEnabled() false) -- where scene_rt.frag's whole
    // reflection block never runs and a reflective surface otherwise gets
    // nothing.
    //
    // Kronos ("Reflection Fix" -- live-reported flickering double
    // reflections): real, deliberate -- drawSSRPass() itself skips
    // recording this pass whenever RT reflections are actually
    // contributing this frame (isRTReflectionsEnabled() && a valid TLAS),
    // regardless of this flag. Once RT reflections are live, every opaque
    // pixel already gets a real reflection contribution from
    // scene_rt.frag's own roughness-weighted blend (a full ray trace on
    // mirror-smooth surfaces, fading to a cheap analytic sky color on
    // rough ones -- see setReflectionRoughnessCutoff()) -- there's no gap
    // left for this pass to fill, and this renderer has no G-buffer for
    // this pass to selectively apply itself only to the rough surfaces RT
    // weights down. Running it anyway re-reflects an already-reflected
    // pixel a second time with its own independent, screen-space-raymarch-
    // unstable result -- which is exactly what read as "flickering double
    // reflections" on the reflective surfaces RT reflections were already
    // handling correctly (a player, a baseplate). This flag stays a
    // simple, honest "would this pass run if nothing else did" toggle;
    // the actual "who wins this frame" decision lives in drawSSRPass()
    // itself, not duplicated at every call site that sets both flags. A
    // real, honest limitation up front: like every SSR technique, it can
    // only reflect what's actually on screen -- off-screen/occluded
    // geometry falls back to the same analytic sky color scene_rt.frag's
    // own proceduralSkyReflection() already provides (see shaders/ssr.frag's
    // own alpha-output convention for how a caller blends between the
    // two). Off by default -- same "every existing scene renders
    // unchanged" real no-op convention as every other opt-in flag here.
    void setSSREnabled(bool enabled) { ssrEnabled_ = enabled; }
    [[nodiscard]] bool isSSREnabled() const { return ssrEnabled_; }
    // `maxDistance`: real world units the screen-space ray marches before
    // giving up (a miss). `thickness`: real world units of depth-buffer
    // "slop" a march step is allowed to be behind a surface and still
    // count as a hit (the standard, honest compensation for the depth
    // buffer only ever storing a single front-most surface, not real
    // volume/thickness).
    void setSSRParams(float maxDistance, float thickness) {
        ssrMaxDistance_ = maxDistance;
        ssrThickness_ = thickness;
    }

    // Kronos ("Rendering Fidelity" -- ray-traced bounce lighting/GI): real
    // single-bounce indirect diffuse -- reuses the *same* TLAS
    // rayQueryShadow()/traceReflection() already trace against (see
    // scene_rt.frag's own traceIndirectDiffuse()), firing one real
    // cosine-weighted hemisphere ray per shaded pixel from its own
    // surface normal. On a real hit, reads the hit surface's own real
    // albedo (instanceCustomIndex, same technique traceReflection()
    // already establishes) and the *same* real, single flat N.L
    // approximation traceReflection() uses for its own hit shading (an
    // honest, single-bounce-only estimate -- no recursive bounce, no
    // shadow ray from the hit point, matching this renderer's own
    // established "shadow ray only ever tests visibility, reflection ray
    // only ever single-bounce" honesty one level further into the light
    // transport chain). On a miss, real sky radiance in that direction
    // (proceduralSkyReflection()) stands in for "this ray escaped to open
    // sky," physically the correct real behavior for an outdoor scene.
    // A real, honest, explicitly-documented limitation: one sample per
    // pixel, no denoiser, no temporal accumulation exist in this engine
    // at all -- the per-pixel sample direction is a deterministic hash of
    // screen position (not true per-frame random), so the result is a
    // real, *static* dither pattern rather than flickering noise, the
    // standard mitigation real-time renderers without a denoiser use.
    // RT-only (same as reflections) -- non-RT devices running scene.frag
    // never had this term, and still don't; off by default.
    void setRTGIEnabled(bool enabled) { rtGIEnabled_ = enabled && rayTracingSupported_; }
    [[nodiscard]] bool isRTGIEnabled() const { return rtGIEnabled_; }
    void setRTGIIntensity(float intensity) { rtGIIntensity_ = intensity; }

    // Sprint 14 ("Performance Mode"): one real toggle bundling several
    // concrete rendering-cost reductions -- see the .cpp implementation
    // comment for the exact real list (cascade count/resolution, bloom,
    // particle cap). The real lever for keeping the "stable 180 FPS"
    // target reachable once heavier real costs (ray-traced shadows) are
    // switched on, not a cosmetic quality slider.
    void setPerformanceMode(bool enabled);
    [[nodiscard]] bool isPerformanceModeEnabled() const { return performanceModeEnabled_; }

    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Graphics: VSync"): real, live -- choosePresentMode()'s
    // own long-standing comment already named this exact feature as the
    // honest way to offer MAILBOX (uncapped, lower-latency, real GPU-
    // power cost) instead of the real, spec-guaranteed FIFO (real vsync)
    // default; this is that setting. Real-triggers recreateSwapchain()
    // immediately when the value actually changes (present mode is a
    // real, immutable swapchain property in Vulkan -- there's no way to
    // change it without rebuilding the swapchain) -- the same real
    // mechanism a window resize already drives, so this is a real,
    // already-proven-safe code path, not new swapchain-recreation logic.
    void setVsyncEnabled(bool enabled);
    [[nodiscard]] bool isVsyncEnabled() const { return vsyncEnabled_; }

    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Accessibility: Colorblind modes"): real -- 0=None/
    // 1=Protanopia/2=Deuteranopia/3=Tritanopia, matching
    // accessibility::ColorblindMode's own real enum order. Applied as a
    // real tint-matrix pass inside the existing composite post-process
    // shader (shaders/composite.frag) -- see that shader's own comment
    // for the exact real matrices used. Out-of-range values real-clamp
    // to None (0) rather than reading undefined shader behavior.
    void setColorblindMode(int mode);
    [[nodiscard]] int colorblindMode() const { return colorblindModeIndex_; }

    // Sprint 16 ("Cinematic Graphics"): one real toggle bundling the new
    // consolidated post-FX stack -- SSAO+DOF+motion blur (drawCinematicPass(),
    // shaders/cinematic.frag) plus vignette/chromatic aberration/saturation
    // grading/god rays (shaders/composite.frag's own additions). Off by
    // default (matches every prior sprint's own "opt-in, not a silent
    // behavior change" convention -- see Performance Mode above). Real,
    // direct mutual exclusion with Performance Mode: enabling one turns the
    // other off, since they pull in opposite directions on GPU cost and
    // "stable 180 FPS" vs. "cinematic 1080p/60" are two different, real,
    // named targets across this engine's own sprints, never both at once.
    void setCinematicMode(bool enabled);
    [[nodiscard]] bool isCinematicModeEnabled() const { return cinematicModeEnabled_; }

    // Kronos ("Real-Time Rendering Evolved" trailer): real decoupling of
    // Cinematic Mode's auto-exposure sub-component from the rest of its
    // bundle (SSAO/DOF/motion-blur/vignette/CA/god-rays) -- confirmed live,
    // root-caused bug: drawLuminancePass()'s auto-exposure targets a flat
    // kAutoExposureTargetLuminance (0.5, "middle gray") average screen
    // brightness (see targetExposure = kAutoExposureTargetLuminance /
    // measuredLuminance below), which aggressively over-brightens any
    // deliberately dim/moody/atmospheric scene (a low-sun sunset, warm
    // low-key lighting) toward flat white -- not a bug specific to
    // AuxiliaryScene/CaptureRig as earlier trailer work assumed, but the
    // auto-exposure math itself, on the live swapchain path too. True (the
    // default) reproduces every existing Cinematic-Mode caller's exact old
    // behavior; false keeps DOF/motion-blur/etc. while composite.frag uses
    // the real, manually-set exposure_ (setExposure()) instead of
    // frame.autoExposureValue -- see Renderer::drawBloomAndComposite()'s
    // own exposure-select line.
    void setAutoExposureEnabled(bool enabled) { autoExposureEnabled_ = enabled; }
    [[nodiscard]] bool isAutoExposureEnabled() const { return autoExposureEnabled_; }

    // Kronos ("Rendering Fidelity Foundation" Phase 1.2): real raymarched
    // volumetric fog + light shafts -- see shaders/volumetric_fog.frag's
    // own header comment for the technique, and
    // Renderer::ensureCinematicTarget()'s own comment for exactly how this
    // slots into the existing hdr -> cinematic -> bloom/composite chain
    // (in front of Cinematic Mode, not exclusive with it -- unlike
    // Cinematic Mode vs. Performance Mode above, there is no real reason
    // these two can't both be on at once). Off by default, same "opt-in"
    // convention as every other post-FX toggle here. A real, honest no-op
    // whenever the active SceneLighting's own fog density is 0 (see
    // shaders/volumetric_fog.frag's own early-out) -- turning this on
    // doesn't invent fog where a scene never asked for any.
    void setVolumetricFogEnabled(bool enabled) { volumetricFogEnabled_ = enabled; }
    [[nodiscard]] bool isVolumetricFogEnabled() const { return volumetricFogEnabled_; }
    // Real tuning knobs -- mirrors setExposure()/setBloomSettings()'s own
    // "plain setter, applied next frame" pattern. See
    // core::VolumetricFogPushConstants's own field comments for what each
    // one really does.
    void setVolumetricFogParams(float scatteringIntensity, int stepCount, float maxDistance) {
        volumetricFogScatteringIntensity_ = scatteringIntensity;
        volumetricFogStepCount_ = stepCount;
        volumetricFogMaxDistance_ = maxDistance;
    }
    // Kronos ("Real-Time Rendering Evolved" trailer): real height-based
    // density gradient -- see core::VolumetricFogPushConstants's own
    // header comment for the full real semantics. Defaults (1/1) are an
    // exact no-op -- every caller that never touches this keeps the old
    // flat-density behavior exactly.
    void setVolumetricFogHeightGradient(float groundDensityMultiplier, float aloftDensityMultiplier,
                                         float groundHeightY, float falloffHeight) {
        volumetricFogGroundDensityMultiplier_ = groundDensityMultiplier;
        volumetricFogAloftDensityMultiplier_ = aloftDensityMultiplier;
        volumetricFogGroundHeightY_ = groundHeightY;
        volumetricFogFalloffHeight_ = falloffHeight;
    }

    // Real tuning knobs for the cinematic pass -- mirrors setExposure()/
    // setBloomSettings()'s own "plain setter, applied next frame" pattern.
    void setDepthOfFieldParams(float focusDistance, float focusRange, float maxCoCRadiusPx) {
        dofFocusDistance_ = focusDistance;
        dofFocusRange_ = focusRange;
        dofMaxCoCRadiusPx_ = maxCoCRadiusPx;
    }
    // Kronos ("Critical Visual Fixes" -- "High Quality Graphics
    // Blurriness"): real, independent gate -- drawCinematicPass() used to
    // force `push.dofEnabled = 1.0f` unconditionally whenever Cinematic
    // Mode was on at all, so RuntimeShell's own "High" quality preset
    // (which just calls setCinematicMode(true) for its other real
    // effects -- SSAO, motion blur, auto-exposure, vignette/god-rays)
    // inherited depthOfFieldParams' *class defaults* (15/10/6), tuned for
    // a completely different context (trailer-scene distances), never for
    // an ordinary close-up third-person gameplay camera. At typical
    // avatar-following camera distances (a few units), that meant almost
    // the entire visible frame sat outside the in-focus range and blurred
    // at close to the max 6px disk radius -- not a subtle bokeh effect, a
    // pervasively blurry screen. Real DOF-specific callers (the mining-sim
    // RTX scene, the render-showcase trailer camera, Studio's
    // LightingToolsPlugin dev panel) already call setDepthOfFieldParams()
    // with their own real, scene-appropriate tuning -- they now also call
    // this to explicitly opt in, rather than DOF being an unconditional,
    // un-tunable side effect of Cinematic Mode as a whole. Defaults to
    // false so ordinary gameplay (including the "High" quality preset)
    // gets Cinematic Mode's other effects without an unwanted blur nobody
    // asked for.
    void setDepthOfFieldEnabled(bool enabled) { depthOfFieldEnabled_ = enabled; }
    [[nodiscard]] bool isDepthOfFieldEnabled() const { return depthOfFieldEnabled_; }
    // `degrees`: a real photographic shutter angle (0-360; 180 is the
    // classic "natural"-looking film default) -- converted to a real
    // [0,1]-ish UV-space blur strength as degrees/360, the standard
    // shutter-angle-to-blur-fraction relationship (0 degrees = shutter
    // never open = no blur; 360 degrees = shutter open the whole frame
    // interval = maximum blur).
    void setMotionBlurShutterAngle(float degrees) { motionBlurStrength_ = std::clamp(degrees, 0.0f, 360.0f) / 360.0f; }
    void setSsaoParams(float radius, float strength) {
        ssaoRadius_ = radius;
        ssaoStrength_ = strength;
    }
    void setVignetteAndChromaticAberration(float vignetteStrength, float chromaticAberrationStrength) {
        vignetteStrength_ = vignetteStrength;
        chromaticAberrationStrength_ = chromaticAberrationStrength;
    }
    void setSaturation(float saturation) { saturation_ = saturation; }
    void setGodRayStrength(float strength) { godRayStrength_ = strength; }

    // Kronos ("Four RTX Maps" Phase 5b, Volcano Map): real heat-haze
    // color-buffer shimmer -- see shaders/cinematic.frag's own comment for
    // the technique. Lives inside the existing Cinematic Mode pass (not a
    // new image-chain stage), so it only has a visible effect when
    // Cinematic Mode is also on -- the same scoping every other knob on
    // this pass (SSAO/DOF/motion blur strength) already has. Off (0
    // strength) by default; every non-Volcano scene leaves this untouched.
    void setHeatDistortionEnabled(bool enabled) { heatDistortionEnabled_ = enabled; }
    [[nodiscard]] bool isHeatDistortionEnabled() const { return heatDistortionEnabled_; }
    void setHeatDistortionStrength(float strength) { heatDistortionStrength_ = strength; }

    // Kronos ("Four RTX Maps" Phase 5c, Underwater Map): real caustic-
    // light dapple pattern on upward-facing (sea-floor-like) geometry --
    // see shaders/scene.frag's own matching comment for the technique and
    // why it's a static (not time-animated) pattern. Off by default; every
    // non-Underwater scene leaves SceneUBO::renderFlags.w at its real 0
    // identity.
    void setUnderwaterCausticsEnabled(bool enabled) { underwaterCausticsEnabled_ = enabled; }
    [[nodiscard]] bool isUnderwaterCausticsEnabled() const { return underwaterCausticsEnabled_; }

    // The real scene-draw path: updates the current frame's SceneUBO from
    // `camera`/`lighting_`, then iterates every entity in `ecs` with
    // Transform+Renderable, pushing per-object constants and issuing an
    // indexed draw per mesh (resolved through `meshLibrary`). Self-
    // contained -- transitions both the color and depth targets from
    // UNDEFINED to *_ATTACHMENT_OPTIMAL itself, clears both, and leaves
    // color in COLOR_ATTACHMENT_OPTIMAL on return (the caller transitions
    // onward to whatever final layout it needs -- PRESENT_SRC_KHR for the
    // swapchain, SHADER_READ_ONLY_OPTIMAL for Studio's offscreen texture --
    // via the public transitionImage() below, since only the caller knows
    // which).
    //
    // Deliberately takes ecs/meshLibrary as explicit parameters rather
    // than reading the setScene() pointers below: Studio calls this
    // directly against its own offscreen viewport target (see
    // studio/StudioApp.cpp) while *never* calling setScene() on its
    // Renderer (that would make its swapchain background also render the
    // full 3D scene behind the docked panels, which is not what Studio
    // wants -- see setScene()'s doc comment). Keeping this function's
    // inputs fully explicit is what makes that split possible without
    // duplicating the draw logic.
    //
    // TODO(frame graph, §4.1): clustered light culling, CSM shadow pass,
    // hybrid RT reflections/AO, a transparent sub-pass, and post
    // (bloom/tonemap/TAA/DLSS/FSR2) all attach around this single opaque
    // pass -- this function *is* the opaque (Forward+) pass the rest of
    // the frame graph schedules relative to, not a placeholder for it.
    // `riggedMeshLibrary`: optional, defaults to nullptr -- every
    // existing caller (engine_runtime's setScene() path, Studio's main
    // Viewport) keeps compiling and behaving identically without
    // passing one. Only a caller with real GPU-skinned content (a
    // SkinnedRenderable-having entity in `ecs`) needs to pass a real
    // RiggedMeshLibrary& so drawSkinnedEntities() can resolve
    // SkinnedRenderable::riggedMeshHandle; with it left null, any
    // SkinnedRenderable entities in `ecs` are simply not drawn (logged
    // once, not per-frame-spammed -- see drawSkinnedEntities()).
    void drawSceneInto(VkCommandBuffer cmd, VkImage colorImage, VkImageView colorView, VkImage depthImage,
                        VkImageView depthView, VkExtent2D extent, const Camera& camera, ECS& ecs,
                        MeshLibrary& meshLibrary, const ParticleSystem& particleSystem, TextureLibrary& textureLibrary,
                        RiggedMeshLibrary* riggedMeshLibrary = nullptr);

    // A real bug this overload set fixes: the plain drawSceneInto() above
    // always renders into frames_[currentFrame_] -- one shared UBO
    // buffer, one shared shadow-cascade image, one shared HDR/bloom
    // pair, all sized/written for whatever *one* call happens per frame.
    // studio::PreviewScene (Avatar Previewer, Catalogue item detail,
    // Upload thumbnail) needs additional, independent scene renders
    // within that *same* frame's command buffer -- calling the plain
    // overload a second time was a same-frame resource collision on two
    // levels: its std::memcpy into frame.sceneUboMapped silently
    // overwrote the first call's camera/lighting data before the GPU had
    // executed either call's commands (both draws would have sampled
    // whichever call went last), and if the second call's `extent`
    // differed from the first's, ensurePostProcessTargets() would
    // destroy-and-recreate frame.hdrImage/hdrView out from under the
    // first call's *already-recorded* vkCmdBeginRendering reference to
    // the old view -- a real, reproducible VK_ERROR_DEVICE_LOST, caught
    // by actually launching Studio with a preview panel open, not by
    // inspection. createAuxiliaryScene() allocates a fully independent
    // FrameSync-shaped resource set (own UBO buffer + descriptor set,
    // own shadow-cascade image, own instance buffers, own HDR/bloom
    // targets sized to whatever extent *that* scene asks for) so each
    // concurrently-open preview gets a real, separate slot instead of
    // fighting the main viewport (or each other) for one. Not tied to
    // swapchain frame-in-flight sync (no semaphore/fence/command buffer
    // of its own) -- the caller records into whichever command buffer is
    // already in flight, same as the plain overload's caller does.
    using AuxiliarySceneHandle = size_t;
    static constexpr AuxiliarySceneHandle kInvalidAuxiliaryScene = static_cast<AuxiliarySceneHandle>(-1);
    // At most kMaxAuxiliaryScenes may exist at once -- sceneDescriptorPool_
    // is sized for framesInFlight_ + kMaxAuxiliaryScenes descriptor sets
    // up front (Vulkan descriptor pools can't grow), not an unbounded
    // pool. Returns kInvalidAuxiliaryScene (logged) if that cap or any
    // GPU resource allocation is exceeded/fails. Raised from 4 to 5 in
    // Sprint 13 ("Publishing & Game Packaging"): live-testing
    // studio::plugins::PublishingPanel's new studio::ThumbnailCameraRig
    // (a real, additional AuxiliarySceneHandle consumer, default-open
    // like every other first-party preview panel) found the pool already
    // exhausted by the existing default-open preview plugins before this
    // one even got a chance -- caught by actually launching Studio, not
    // by inspection, the same discipline that already found the
    // AvatarPreviewer VK_ERROR_DEVICE_LOST earlier in this project's
    // history. Raised again, 5 to 7, in Sprint 15 ("TNT-Wars Trailer
    // Production"): studio::plugins::TrailerPanel is a real *two*-consumer
    // plugin (its own live-preview studio::ThumbnailCameraRig, plus its
    // owned trailer::TrailerDirector's own separate, real
    // trailer::CaptureRig for real file-sequence output) -- found the
    // exact same way, by actually launching Studio and reading
    // "createAuxiliaryScene() failed" in the log, not by counting
    // consumers ahead of time.
    static constexpr size_t kMaxAuxiliaryScenes = 7;
    [[nodiscard]] AuxiliarySceneHandle createAuxiliaryScene();
    // Frees this slot's GPU resources. Per MeshLibrary/TextureLibrary's
    // own "index-based handle, no true removal" precedent, the slot
    // itself stays reserved in auxiliaryScenes_ (never physically erased,
    // so no other live handle's index shifts) -- just zeroed out and
    // unusable; there is no handle-reuse/free-list.
    void destroyAuxiliaryScene(AuxiliarySceneHandle handle);
    void drawSceneInto(AuxiliarySceneHandle handle, VkCommandBuffer cmd, VkImage colorImage, VkImageView colorView,
                        VkImage depthImage, VkImageView depthView, VkExtent2D extent, const Camera& camera, ECS& ecs,
                        MeshLibrary& meshLibrary, const ParticleSystem& particleSystem, TextureLibrary& textureLibrary,
                        RiggedMeshLibrary* riggedMeshLibrary = nullptr);

    // Invoked (if set) right after vkBeginCommandBuffer, before the main
    // pass's swapchain-image transition -- Studio uses this to record its
    // own drawSceneInto() call against its offscreen viewport target
    // first, in the same command buffer, before anything touches the
    // swapchain image this frame.
    using PrePassCallback = std::function<void(VkCommandBuffer cmd)>;
    void setPrePassCallback(PrePassCallback callback) { prePassCallback_ = std::move(callback); }

    // The overlay/gizmo-pass seam: invoked (if set) in its own
    // vkCmdBeginRendering/EndRendering block on the swapchain image,
    // immediately after the main pass and before the present transition --
    // with loadOp=LOAD, so it draws on top of whatever the main pass
    // produced. This is how Studio (§4.2/§5) composites ImGui onto the
    // exact same render graph the runtime uses, instead of running a
    // second, separate renderer.
    using OverlayCallback = std::function<void(VkCommandBuffer cmd, VkImageView targetView, VkExtent2D extent)>;
    void setOverlayCallback(OverlayCallback callback) { overlayCallback_ = std::move(callback); }

    // Public (unlike the rest of Renderer's Vulkan plumbing) because
    // Studio's offscreen viewport target -- owned by studio/, not core/ --
    // needs to transition its own image in/out of the layouts
    // drawSceneInto() and ImGui's texture sampling expect. See
    // studio/panels/ViewportPanel.cpp for the caller.
    void transitionImage(VkCommandBuffer cmd, VkImage image,
                          VkImageLayout oldLayout, VkImageLayout newLayout,
                          VkAccessFlags2 srcAccess, VkAccessFlags2 dstAccess,
                          VkPipelineStageFlags2 srcStage, VkPipelineStageFlags2 dstStage,
                          VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t layerCount = 1) const;

private:
    // kCascadeCount itself is declared in the public section above (see
    // its own comment on why) -- FrameSync/CascadeData below still size
    // their arrays with it, unequal-to-SceneTypes.hpp's kShadowCascadeCount
    // caveat unchanged (both are compile-time constants; nothing enforces
    // the equality automatically, so if either changes, change both --
    // documented here and there).

    // GPU skinning limits -- kMaxJointsPerSkeleton must match
    // shaders/scene_skinned.vert's MAX_JOINTS exactly (nothing enforces
    // the equality automatically, same caveat as kCascadeCount above).
    // kMaxSkinnedDrawsPerFrame bounds how many *independent* bone-matrix
    // UBO slots each FrameSync gets (see FrameSync's skinning fields
    // below) -- a real, small, fixed pool, not an unbounded one, sized
    // for "a handful of simultaneously-visible rigged characters." One
    // real avatar/demo body (core::spawnRiggedAvatar(), see
    // RiggedAvatar.hpp) costs kHumanoidBodySegmentCount (10) slots, not 1
    // -- it's split into one SkinnedRenderable per body segment so each
    // segment can carry its own flat color (a shirt tinted differently
    // from skin), not a single merged mesh. 40 covers four such
    // avatars/demo bodies fully visible at once (one player + a few NPCs,
    // or Studio's Animation Previewer demo body alongside a live runtime
    // session's own avatar) -- still a real, small, fixed pool, not a
    // general skeletal-crowd renderer. Found undersized once already (at
    // the old value of 4 -- barely half of one avatar's then-6 segments)
    // by live-running Studio's Animation Previewer for the first time and
    // seeing "skipped an entity" spam in the log every frame; see
    // README's Known Issues for that write-up. Kronos ("Multi-Region
    // Clothing Shader & Palette System"): real, scaled proportionally
    // (24 -> 40) alongside kHumanoidBodySegmentCount's own 6 -> 10 change
    // -- silently leaving this at 24 would have quietly cut
    // simultaneously-visible-avatar capacity from 4 to 2.4, reintroducing
    // that exact same bug this comment already documents being found and
    // fixed once before.
    static constexpr uint32_t kMaxJointsPerSkeleton = 64;
    static constexpr uint32_t kMaxSkinnedDrawsPerFrame = 40;

    struct QueueFamilyIndices {
        std::optional<uint32_t> graphics;
        std::optional<uint32_t> present;
        [[nodiscard]] bool isComplete() const { return graphics.has_value() && present.has_value(); }
    };

    // One light-view-proj matrix + far-split depth + light-space depth
    // range per cascade -- see computeCascades().
    struct CascadeData {
        std::array<glm::mat4, kCascadeCount> lightViewProj{};
        std::array<float, kCascadeCount> splitDepths{};  // view-space far distance of each cascade
        std::array<float, kCascadeCount> depthRanges{};  // light-space (orthoFar - orthoNear) of each cascade -- see scene.frag's bias scaling
    };

    struct FrameSync {
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

        VkBuffer sceneUboBuffer = VK_NULL_HANDLE;
        VmaAllocation sceneUboAllocation = nullptr;
        void* sceneUboMapped = nullptr; // persistently mapped -- see createSceneDescriptorResources()
        VkDescriptorSet sceneDescriptorSet = VK_NULL_HANDLE;

        // One shadow map *per frame-in-flight*, not one shared map --
        // with framesInFlight_ >= 2, frame N+1's GPU work can start before
        // frame N's finishes (that's the entire point of multiple frames
        // in flight), so a single shared shadow image would let frame
        // N+1's shadow-pass write race frame N's main-pass read of the
        // same image with no barrier between them (different command
        // buffer submissions on the same queue don't implicitly
        // serialize). Giving each frame-in-flight slot its own image
        // means the existing per-slot fence wait (renderFrame()'s
        // vkWaitForFences) already guarantees the previous use of *this*
        // slot's shadow map is done before it's reused -- no new
        // synchronization primitive needed, just reusing the one that's
        // already there for exactly this reason.
        //
        // One VkImage with kCascadeCount array layers (not kCascadeCount
        // separate images) -- shadowArrayView is a VK_IMAGE_VIEW_TYPE_2D_ARRAY
        // covering all layers, bound to the descriptor set for sampling
        // (scene.frag indexes it by cascade); shadowCascadeViews are
        // kCascadeCount separate VK_IMAGE_VIEW_TYPE_2D views (one layer
        // each), used as the depth-attachment render targets during
        // drawShadowPass() -- dynamic rendering needs a plain 2D view per
        // render target, not an array view, so both kinds of view exist
        // for the two different roles the same underlying image plays.
        VkImage shadowImage = VK_NULL_HANDLE;
        VmaAllocation shadowAllocation = nullptr;
        VkImageView shadowArrayView = VK_NULL_HANDLE;
        std::array<VkImageView, kCascadeCount> shadowCascadeViews{};

        // Per-frame-in-flight instance buffer for the GPU-instanced draw
        // path (see drawInstancedBatches()) -- same "why per-frame-in-
        // flight" reasoning as the shadow map above: frame N+1's CPU-side
        // write into this buffer must not race frame N's GPU read of it,
        // and giving each frame-in-flight slot its own buffer means the
        // existing per-slot fence wait already guarantees that.
        // Persistently mapped, like sceneUboBuffer.
        VkBuffer instanceBuffer = VK_NULL_HANDLE;
        VmaAllocation instanceAllocation = nullptr;
        void* instanceMapped = nullptr;

        // Same per-frame-in-flight reasoning again, for particle billboard
        // instances -- see drawParticles().
        VkBuffer particleInstanceBuffer = VK_NULL_HANDLE;
        VmaAllocation particleInstanceAllocation = nullptr;
        void* particleInstanceMapped = nullptr;

        // GPU-skinning bone-matrix slots -- kMaxSkinnedDrawsPerFrame
        // independent UBO buffers + descriptor sets *per FrameSync*
        // (frames_[] slot or auxiliaryScenes_[] slot alike), so
        // drawSkinnedEntities() can draw that many skinned entities in
        // one drawSceneInto() call without any of them sharing a bone
        // buffer -- the same same-frame-collision class
        // AuxiliarySceneHandle's own doc comment describes, one level
        // deeper (per-draw, not per-scene). Persistently mapped, like
        // sceneUboBuffer.
        std::array<VkBuffer, kMaxSkinnedDrawsPerFrame> skinningUboBuffers{};
        std::array<VmaAllocation, kMaxSkinnedDrawsPerFrame> skinningUboAllocations{};
        std::array<void*, kMaxSkinnedDrawsPerFrame> skinningUboMapped{};
        std::array<VkDescriptorSet, kMaxSkinnedDrawsPerFrame> skinningDescriptorSets{};

        // Post-process intermediate targets -- per-frame-in-flight for the
        // same reason as the shadow map: these are GPU-written (the opaque/
        // instanced/particle passes) then GPU-read (bloom extract, then
        // composite) entirely within one frame's command buffer, but with
        // 2 frames in flight, frame N+1's writes could otherwise race
        // frame N's still-in-flight reads of a *shared* image with no
        // barrier between separate queue submissions to stop it.
        //
        // postProcessExtent tracks what size these are currently allocated
        // at, defaulting to {0,0} so ensurePostProcessTargets() always
        // (re)creates them on a frame slot's first use -- same lazy-resize
        // pattern studio/OffscreenTarget.hpp already uses for Studio's
        // viewport target, applied here per-frame-in-flight instead of once.
        VkExtent2D postProcessExtent{0, 0};

        VkImage hdrImage = VK_NULL_HANDLE; // full resolution, kHDRFormat -- what the opaque/instanced/particle passes render into
        VmaAllocation hdrAllocation = nullptr;
        VkImageView hdrView = VK_NULL_HANDLE;

        VkImage bloomImage = VK_NULL_HANDLE; // half resolution, kHDRFormat -- bright-pass + blur output (see drawBloomAndComposite)
        VmaAllocation bloomAllocation = nullptr;
        VkImageView bloomView = VK_NULL_HANDLE;

        // Descriptor sets pointing at hdrView/bloomView above -- rewritten
        // by ensurePostProcessTargets() whenever those views are recreated.
        // binding 0 of *both* is repointed at cinematicView (below)
        // instead of hdrView whenever Cinematic Mode is on -- see
        // ensurePostProcessTargets()'s own comment on `cinematicSourceBound`.
        VkDescriptorSet bloomExtractDescriptorSet = VK_NULL_HANDLE; // binding 0: hdrView or cinematicView
        VkDescriptorSet compositeDescriptorSet = VK_NULL_HANDLE;    // binding 0: hdrView or cinematicView, binding 1: bloomView

        // Sprint 16 ("Cinematic Graphics"): the consolidated SSAO/DOF/
        // motion-blur pass's own output target -- same size/format as
        // hdrImage above (a real ping buffer; the pass reads hdrImage and
        // writes here, since no hardware lets a pass read and write the
        // same attachment within one draw). Only ever (re)created when
        // Cinematic Mode is actually on at least once for this frame slot
        // -- see ensurePostProcessTargets()'s lazy-allocation comment.
        VkImage cinematicImage = VK_NULL_HANDLE;
        VmaAllocation cinematicAllocation = nullptr;
        VkImageView cinematicView = VK_NULL_HANDLE;
        // set=1 for cinematicPipelineLayout_: binding 0 hdrView, binding 1
        // this frame's real sampled depth view.
        VkDescriptorSet cinematicDescriptorSet = VK_NULL_HANDLE;
        // Sprint 16: set=1 for particlePipelineLayout_ -- this frame's
        // real sampled depth view, for soft-particle depth fade (see
        // shaders/particle.frag). Always kept current (unlike
        // cinematicDescriptorSet/luminanceDescriptorSet, not gated on
        // isCinematicModeEnabled() -- soft-particle fade is a real, always-
        // on quality fix, not a Cinematic-Mode-only effect).
        VkDescriptorSet particleDepthDescriptorSet = VK_NULL_HANDLE;
        // Whether bloomExtractDescriptorSet/compositeDescriptorSet's
        // binding 0 currently points at cinematicView (true) or hdrView
        // (false) -- compared against isCinematicModeEnabled() once per
        // frame in ensurePostProcessTargets() so a runtime toggle (the
        // Studio checkbox, the F9 keybind) takes effect the very next
        // frame via a cheap descriptor rewrite, not a full pipeline/
        // target rebuild. Kronos Phase 1.2: this pair's real "final
        // source" is now a 3-way choice (hdr/fog/cinematic, see
        // ensureCinematicTarget()'s own updated comment) -- this bool
        // still tracks isCinematicModeEnabled() specifically,
        // fogSourceBoundForComposite (below) tracks isVolumetricFogEnabled()
        // specifically, and both are checked together at the one real call
        // site that repoints bloomExtractDescriptorSet/compositeDescriptorSet.
        bool cinematicSourceBound = false;

        // Kronos ("Rendering Fidelity Foundation" Phase 1.2): real
        // raymarched volumetric fog's own output target -- full
        // resolution, same kHDRFormat/shape as cinematicImage above (a
        // real ping buffer: shaders/volumetric_fog.frag reads hdrImage and
        // writes here, the same "can't read and write one attachment"
        // reason cinematicImage exists). Only ever (re)created the first
        // time isVolumetricFogEnabled() is actually on for this frame
        // slot -- same lazy-allocation convention as cinematicImage.
        VkImage fogImage = VK_NULL_HANDLE;
        VmaAllocation fogAllocation = nullptr;
        VkImageView fogView = VK_NULL_HANDLE;
        // set=1 for volumetricFogPipelineLayout_ (reuses
        // cinematicDescriptorSetLayout_'s shape, see that field's own
        // Renderer.hpp comment): binding 0 hdrView, binding 1 this frame's
        // real sampled depth view. Deliberately a *separate* descriptor
        // set instance from frame.cinematicDescriptorSet even though both
        // currently hold the same layout and, often, the same binding 0 --
        // the fog pass must always read the *raw* scene color (it can
        // never legally read its own output), whereas cinematicDescriptorSet's
        // own binding 0 becomes conditional on fog (see
        // ensureCinematicTarget()'s own updated comment) the moment
        // volumetric fog is enabled -- sharing one descriptor set between
        // the two would make that conditional rewrite corrupt fog's own
        // input the next time fog itself ran.
        VkDescriptorSet fogInputDescriptorSet = VK_NULL_HANDLE;
        // Same real tracking role as cinematicSourceBound above, for
        // isVolumetricFogEnabled() specifically -- see that field's own
        // updated comment for how the two combine.
        bool fogSourceBoundForComposite = false;

        // Kronos ("Rendering Fidelity" -- SSR fallback pass): real
        // screen-space-reflection target -- same real ping-buffer shape
        // as fogImage above (shaders/ssr.frag reads hdrImage + depth,
        // writes here), and the same lazy-allocation convention (only
        // ever created the first time isSSREnabled() is actually on for
        // this frame slot). Runs *before* volumetric fog in the real
        // per-frame chain (see Renderer::drawSSRPass()'s own comment for
        // why), so ensureVolumetricFogTargets()'s own input becomes
        // conditional on this the same way ensureCinematicTarget()'s
        // input is already conditional on fog.
        VkImage ssrImage = VK_NULL_HANDLE;
        VmaAllocation ssrAllocation = nullptr;
        VkImageView ssrView = VK_NULL_HANDLE;
        // set=1 for ssrPipelineLayout_ (reuses cinematicDescriptorSetLayout_'s
        // shape, same real "2-binding hdrColor+sceneDepth" reasoning
        // fogInputDescriptorSet's own comment documents): binding 0 always
        // frame.hdrView (SSR, like fog, can never legally read its own
        // output), binding 1 this frame's real sampled depth view.
        VkDescriptorSet ssrInputDescriptorSet = VK_NULL_HANDLE;
        // Same real tracking role as cinematicSourceBound/fogSourceBoundForComposite
        // above, for isSSREnabled() specifically.
        bool ssrSourceBoundForComposite = false;

        // Sprint 16 auto-exposure: a real 1x1 render target + HOST_VISIBLE
        // readback buffer measuring this frame's average scene luminance
        // (drawCinematicPass() -- shaders/luminance.frag), read back
        // *next* time this frame slot comes around (stall-free -- see
        // drawCinematicPass()'s own comment for why that's safe under the
        // exact same per-slot fence-wait guarantee sceneUboMapped/
        // instanceMapped etc. already rely on). Allocated lazily, once,
        // the first time Cinematic Mode is on for this slot -- a 1x1
        // target never needs resizing, unlike hdrImage/bloomImage/
        // cinematicImage above, so this is a one-time allocation, not
        // part of the extent-driven resize path.
        VkImage luminanceImage = VK_NULL_HANDLE;
        VmaAllocation luminanceAllocation = nullptr;
        VkImageView luminanceView = VK_NULL_HANDLE;
        VkDescriptorSet luminanceDescriptorSet = VK_NULL_HANDLE; // postProcessSingleSetLayout_, binding 0: hdrView
        VkBuffer luminanceReadbackBuffer = VK_NULL_HANDLE;
        VmaAllocation luminanceReadbackAllocation = nullptr;
        void* luminanceReadbackMapped = nullptr; // persistently mapped, 1 float
        // Temporally-adapted exposure value this slot's own auto-exposure
        // has converged to -- per-slot (not Renderer-wide) for the same
        // reason previousViewProj above is: the main viewport and each
        // Studio preview scene light entirely different content and must
        // never share one adapted value.
        float autoExposureValue = 1.0f;

        // Previous frame's view-projection matrix, for the cinematic
        // pass's camera-based motion blur (shaders/cinematic.frag) --
        // per-FrameSync-slot (not one Renderer-wide value) since distinct
        // logical camera streams (the main swapchain frames_[] slots vs.
        // each auxiliaryScenes_[] Studio-preview slot) must never mix each
        // other's camera history. With framesInFlight_ > 1 this is "N
        // frames back", not literally "1 frame back" -- a real, honest,
        // minor imprecision (slightly stronger blur than a single-buffered
        // renderer would show under fast camera motion), not a
        // correctness bug -- see drawSceneIntoImpl()'s own comment.
        glm::mat4 previousViewProj{1.0f};
        bool hasPreviousViewProj = false;
    };

    bool createInstance();
    bool createSurface();
    bool pickPhysicalDevice();
    [[nodiscard]] bool checkRayTracingSupport(VkPhysicalDevice device) const;
    [[nodiscard]] bool checkBindlessSupport(VkPhysicalDevice device) const;
    [[nodiscard]] bool createBindlessResources();
    void destroyBindlessResources();
    // Returns the slot for `handle`, writing the descriptor on first use.
    // Falls back to `defaultSlot` when the handle is unusable or the table
    // is exhausted, so an overflow degrades visually rather than reading a
    // stale descriptor.
    [[nodiscard]] uint32_t bindlessSlotFor(uint32_t handle, TextureLibrary& textureLibrary, uint32_t defaultSlot);
    // Packs a Renderable's five texture slots into ObjectPushConstants::
    // textureIndices / InstanceData::textureIndices. Returns all-zero when
    // bindless is inactive, which the non-bindless shader variant ignores.
    [[nodiscard]] glm::uvec4 packTextureIndices(const Renderable& renderable, TextureLibrary& textureLibrary);
    [[nodiscard]] bool createPerImageSemaphores();
    void destroyPerImageSemaphores();
    bool createLogicalDevice();
    bool createAllocator();
    bool createSwapchain();
    void destroySwapchain();
    bool createDepthResources();
    void destroyDepthResources();
    bool createCommandPoolAndBuffers();
    bool createSyncObjects();
    bool createSceneDescriptorResources();
    void destroySceneDescriptorResources();
    bool createScenePipeline();
    void destroyScenePipeline();
    // Kronos ("Real-Time Rendering Evolved" trailer): real glass/water
    // transmission pipeline -- see shaders/glass.frag's own header
    // comment. Own pipeline layout (set=0 scene UBO only, a small
    // GlassPushConstants range -- no material-texture set=1, unlike
    // scenePipelineLayout_), created right after createScenePipeline()
    // since it needs nothing beyond sceneDescriptorSetLayout_.
    bool createGlassPipeline();
    void destroyGlassPipeline();
    bool createInstancedScenePipeline(); // reuses scenePipelineLayout_ -- see its .cpp comment
    void destroyInstancedScenePipeline();
    bool createInstanceBuffers();
    void destroyInstanceBuffers();
    bool createParticleResources(); // the shared quad mesh + per-frame particle instance buffers
    void destroyParticleResources();
    bool createParticlePipeline(); // reuses scenePipelineLayout_, additive-blended, depth-tested/no-write
    void destroyParticlePipeline();

    // GPU skinning -- its own descriptor set layout/pool (set 2 of
    // skinnedScenePipelineLayout_; sets 0/1 are the *same*
    // sceneDescriptorSetLayout_/materialDescriptorSetLayout_ handles
    // scenePipelineLayout_ already uses, reused as-is, not duplicated)
    // and its own pipeline (shaders/scene_skinned.vert + the same
    // shaders/scene.frag every other opaque pipeline shares). See
    // FrameSync's skinning fields and drawSkinnedEntities()'s doc
    // comment for the full picture.
    bool createSkinningDescriptorResources();
    void destroySkinningDescriptorResources();
    bool initSkinningResourcesFor(FrameSync& frame);
    void destroySkinningResourcesFor(FrameSync& frame);
    bool createSkinnedScenePipeline();
    void destroySkinnedScenePipeline();

    // Material textures (set=1 of scenePipelineLayout_) -- sampler,
    // descriptor set layout/pool, and the default solid-color fallback
    // textures every unassigned slot binds (see Components.hpp's
    // Renderable texture-handle fields). Not per-frame-sized: texture
    // VkImageViews are long-lived once loaded (unlike the per-frame HDR/
    // shadow targets), so their descriptor sets are created once and
    // cached, not rebuilt every frame -- see materialDescriptorCache_.
    bool createMaterialResources();
    void destroyMaterialResources();
    // Returns a cached descriptor set for this exact combination of
    // texture handles, creating (and caching) one on first use. Returns
    // VK_NULL_HANDLE if materialDescriptorPool_ is exhausted
    // (kMaxMaterialDescriptorSets) -- the caller skips binding for that
    // entity rather than crashing, see its call site's comment.
    [[nodiscard]] VkDescriptorSet getOrCreateMaterialDescriptorSet(const Renderable& renderable,
                                                                    TextureLibrary& textureLibrary);

    bool createPostProcessResources(); // sampler + descriptor set layouts/pool -- everything NOT per-frame-sized
    void destroyPostProcessResources();
    bool createPostProcessPipelines(); // bloom extract + composite, both full-screen-triangle passes
    void destroyPostProcessPipelines();
    // Real, basic procedural sky (task category 3) -- a third full-
    // screen-triangle pass, drawn first into frame.hdrView (depth test/
    // write both disabled) so scene geometry naturally overdraws it --
    // see shaders/sky.frag's own header comment. Reuses
    // sceneDescriptorSetLayout_ (the same set every scene/shadow
    // pipeline already binds) rather than a bespoke layout, since all
    // this pass needs is already in SceneUBO.
    bool createSkyPipeline();
    void destroySkyPipeline();
    // (Re)creates frame's hdrImage/bloomImage/cinematicImage/fogImage (and
    // their descriptor sets) if `extent` differs from frame.postProcessExtent,
    // or nothing exists yet; also repoints bloomExtractDescriptorSet's/
    // compositeDescriptorSet's binding 0 at whichever of hdrView/fogView/
    // cinematicView is currently the real "front" of the chain (see
    // ensureCinematicTarget()'s own updated comment) whenever
    // isCinematicModeEnabled()/isVolumetricFogEnabled() have changed since
    // the last call for this frame slot -- a cheap descriptor rewrite, not
    // a target rebuild. `depthView`: this frame's real sampled depth view,
    // wired into frame.cinematicDescriptorSet/frame.fogInputDescriptorSet
    // (binding 1) whenever those targets themselves (re)allocate. No-op
    // otherwise -- same contract as OffscreenTarget::ensureSize().
    bool ensurePostProcessTargets(FrameSync& frame, VkExtent2D extent, VkImageView depthView);
    void destroyPostProcessTargets(FrameSync& frame);
    // Kronos ("Rendering Fidelity Foundation" Phase 1.2): lazily allocates
    // frame.fogImage/View/fogInputDescriptorSet the first time
    // isVolumetricFogEnabled() is actually on for this frame slot (never
    // eagerly), and keeps frame.fogInputDescriptorSet's own bindings
    // (hdrView + depthView, always -- see that field's own FrameSync
    // comment on why it never conditionally repoints itself) current.
    // Must run *before* ensureCinematicTarget() in ensurePostProcessTargets()
    // -- cinematic's own input, and the final bloom/composite source, both
    // need frame.fogView to already exist by the time they might reference
    // it.
    bool ensureVolumetricFogTargets(FrameSync& frame, VkExtent2D extent, VkImageView depthView);
    // Kronos ("Rendering Fidelity" -- SSR fallback pass): lazily allocates
    // frame.ssrImage/View/ssrInputDescriptorSet the first time
    // isSSREnabled() is actually on for this frame slot (never eagerly),
    // and keeps frame.ssrInputDescriptorSet's own bindings (hdrView +
    // depthView, always -- SSR, like fog, can never legally read its own
    // output) current. Must run *before* ensureVolumetricFogTargets() in
    // ensurePostProcessTargets() -- fog's own input, and the final
    // bloom/composite source, both need frame.ssrView to already exist by
    // the time they might reference it (SSR runs first in the real
    // per-frame chain: reflections should themselves be hazed by
    // volumetric fog, not the other way around).
    bool ensureSSRTargets(FrameSync& frame, VkExtent2D extent, VkImageView depthView);
    // Lazily allocates frame.cinematicImage/View/DescriptorSet and keeps
    // bloomExtractDescriptorSet/compositeDescriptorSet's binding 0
    // pointed at the right source -- factored out of
    // ensurePostProcessTargets() since it needs to run even on the common
    // "extent unchanged" early-out path (a runtime Cinematic Mode toggle
    // shouldn't require a full post-process target resize to take
    // effect). Kronos Phase 1.2: also now repoints frame.cinematicDescriptorSet's
    // *own* binding 0 between frame.hdrView and frame.fogView (unconditionally,
    // every call, same as it already always did for hdrView+depth) so
    // Cinematic Mode's own pass reads fog's output when both are on -- the
    // real hdr -> fog -> cinematic -> bloom/composite pass order this
    // engine now has. See its own .cpp doc comment.
    bool ensureCinematicTarget(FrameSync& frame, VkExtent2D extent, VkImageView depthView);
    // Sprint 16 auto-exposure: lazily allocates frame's 1x1 luminance
    // target + readback buffer (see FrameSync's own comment) the first
    // time Cinematic Mode is on for this slot. No `extent` parameter --
    // unlike ensureCinematicTarget(), this target's size never changes.
    bool ensureLuminanceTarget(FrameSync& frame);
    // Reads back frame.luminanceReadbackMapped (this slot's *previous*
    // measurement -- stall-free, see drawCinematicPass()'s own comment),
    // temporally adapts frame.autoExposureValue toward it, then renders
    // *this* frame's new measurement (shaders/luminance.frag, sourced
    // from frame.hdrImage) for the *next* time this slot comes around.
    void drawLuminancePass(VkCommandBuffer cmd, FrameSync& frame);
    // Sprint 16 soft-particle depth fade: allocates (once) and rewrites
    // (every call -- depthView can legitimately change on a caller-side
    // resize) frame.particleDepthDescriptorSet. Unlike ensureCinematicTarget()/
    // ensureLuminanceTarget(), always runs regardless of
    // isCinematicModeEnabled() -- see that field's own FrameSync comment.
    bool ensureParticleDepthDescriptor(FrameSync& frame, VkImageView depthView);

    // Kronos ("Rendering Fidelity Foundation" Phase 1.2): the real
    // raymarched volumetric fog + light-shaft pass (shaders/volumetric_fog.frag)
    // -- frame.hdrImage + this frame's depth (read, already bound into
    // frame.fogInputDescriptorSet by ensureVolumetricFogTargets()) ->
    // frame.fogImage (write). Real, direct bypass: not recorded at all
    // when isVolumetricFogEnabled() is false, matching drawCinematicPass()'s
    // own bypass convention exactly. Runs *before* drawCinematicPass() --
    // see ensureCinematicTarget()'s own comment for why.
    void drawVolumetricFogPass(VkCommandBuffer cmd, FrameSync& frame, VkExtent2D extent);

    // Kronos ("Rendering Fidelity" -- SSR fallback pass): real screen-
    // space reflections -- frame.hdrImage + this frame's depth (read,
    // already bound into frame.ssrInputDescriptorSet by
    // ensureSSRTargets()) -> frame.ssrImage (write). Real, direct bypass:
    // not recorded at all when isSSREnabled() is false, same convention
    // as drawVolumetricFogPass()'s own bypass. Runs *before*
    // drawVolumetricFogPass() -- see this class's own ensureSSRTargets()
    // comment for why.
    void drawSSRPass(VkCommandBuffer cmd, FrameSync& frame, VkExtent2D extent);

    // Sprint 16 ("Cinematic Graphics"): the consolidated SSAO/DOF/motion-
    // blur pass (shaders/cinematic.frag) -- frame.hdrImage + this frame's
    // depth (read, already bound into frame.cinematicDescriptorSet by
    // ensureCinematicTarget()) -> frame.cinematicImage (write). Real,
    // direct bypass: not recorded at all when isCinematicModeEnabled() is
    // false, so the pre-Sprint-16 frame.hdrImage -> bloom_extract path is
    // byte-for-byte unchanged cost-wise with Cinematic Mode off.
    // `previousViewProj`: this frame slot's own camera history, see
    // FrameSync's own comment.
    void drawCinematicPass(VkCommandBuffer cmd, FrameSync& frame, VkExtent2D extent,
                            const glm::mat4& previousViewProj);

    // Bright-pass+blur (bloom_extract.frag) into frame.bloomImage, then
    // composite (composite.frag: HDR + bloom -> exposure -> ACES tonemap
    // -> gamma, plus Sprint 16's vignette/chromatic aberration/saturation/
    // god rays) into the caller-provided `colorImage`/`colorView` -- the
    // final step of drawSceneInto(), after the opaque/instanced/particle
    // passes (and, when Cinematic Mode is on, drawCinematicPass()) have
    // filled frame.hdrImage. Two full-screen-triangle passes, no vertex/
    // index buffer bound for either (see shaders/fullscreen.vert).
    // `sunScreenUV`/`sunVisible`: this frame's real sun screen-space
    // position, for the god-ray radial scatter in shaders/composite.frag
    // -- computed in drawSceneIntoImpl() (needs the camera/lighting_
    // state this function doesn't otherwise take), see that call site's
    // own comment for exactly how.
    void drawBloomAndComposite(VkCommandBuffer cmd, FrameSync& frame, VkImage colorImage, VkImageView colorView,
                                VkExtent2D extent, glm::vec2 sunScreenUV, bool sunVisible, bool applyBloom = true);

    // Buckets every Renderable+Transform entity with `instanced == true`
    // by meshHandle, uploads each bucket's InstanceData into
    // frame.instanceBuffer, and issues one vkCmdDrawIndexed(instanceCount
    // = bucket size) per mesh -- see Components.hpp's Renderable::instanced
    // and SceneTypes.hpp's InstanceData. Called from within drawSceneInto()'s
    // main pass, after the individual (push-constant) draw loop, in the
    // same vkCmdBeginRendering block (pipelines can be switched mid-pass
    // freely). Does NOT affect drawShadowPass(), which still draws every
    // caster individually regardless of this flag -- see its comment.
    void drawInstancedBatches(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, MeshLibrary& meshLibrary,
                               TextureLibrary& textureLibrary);

    // Uploads one ParticleInstanceData per live particle and draws them
    // all in one instanced call against the shared unit quad
    // (particleQuadMesh_) -- see shaders/particle.vert/.frag and
    // core::ParticleSystem. Drawn after drawInstancedBatches() in the same
    // render pass, so particles composite on top of opaque geometry. No
    // Camera parameter -- particle.vert derives its billboard right/up
    // axes from the SceneUBO's view matrix (already written this frame),
    // not a value this function would need to pass separately.
    void drawParticles(VkCommandBuffer cmd, FrameSync& frame, const ParticleSystem& particleSystem);
    bool createShadowResources();  // per-frame cascade array images/views + the one shared sampler
    void destroyShadowResources();
    // Per-FrameSync halves of the four create/destroy*Resources() pairs
    // above and below -- extracted so a FrameSync instance that ISN'T one
    // of frames_[] (see createAuxiliaryScene()) can get the exact same
    // real resources any other frame slot gets, instead of a second,
    // parallel (and likely diverging) implementation. Each shared,
    // one-time piece (shadowSampler_, sceneDescriptorPool_/Layout_,
    // particleQuadMesh_) stays owned by the aggregate function; only the
    // per-frame image/buffer/descriptor-set creation is here.
    bool initShadowResourcesFor(FrameSync& frame);
    void destroyShadowResourcesFor(FrameSync& frame);
    bool initSceneDescriptorResourcesFor(FrameSync& frame);
    void destroySceneDescriptorResourcesFor(FrameSync& frame);
    // Sprint 14 ("RTX Upgrade" Phase 2): (re)writes just binding 2 (the
    // real TLAS) of `frame`'s already-allocated scene descriptor set --
    // called once from initSceneDescriptorResourcesFor() and again every
    // time the real TLAS handle changes (see drawSceneIntoImpl()'s own
    // comment). A real, honest no-op if !rayTracingSupported_ (binding 2
    // doesn't exist in that case at all).
    void updateRayTracedShadowDescriptor(FrameSync& frame);
    bool initInstanceBufferFor(FrameSync& frame);
    void destroyInstanceBufferFor(FrameSync& frame);
    bool initParticleInstanceBufferFor(FrameSync& frame);
    void destroyParticleInstanceBufferFor(FrameSync& frame);
    bool createShadowPipeline();   // owns shadowPipelineLayout_ -- see its .cpp comment for why not scenePipelineLayout_
    void destroyShadowPipeline();

    // Real cascaded shadow maps: splits [camera.nearPlane, kShadowMaxDistance]
    // into kCascadeCount sub-frusta via a practical (log/uniform blend)
    // split scheme, fits a tight camera-following ortho volume to each
    // sub-frustum's corners in light space, and snaps that volume's X/Y
    // origin to whole shadow-texel increments ("stable splits" -- without
    // this, panning the camera by a fraction of a texel shifts the whole
    // cascade by a fraction of a texel too, which reads as shadow edges
    // shimmering/crawling even though nothing moved). Recomputed every
    // frame rather than cached -- cheap relative to the shadow draw calls
    // it feeds, and correctness (always matching the current camera)
    // matters more here than the CPU cost of ~24 matrix multiplies.
    //
    // Deliberate simplifications, stated plainly: no cross-cascade blend
    // band (a fragment right at a split boundary hard-switches cascades,
    // which can show as a faint seam -- real engines often blend the last
    // few percent of each cascade into the next); cascades cover
    // [nearPlane, kShadowMaxDistance], not the camera's full farPlane (500
    // world units) -- shadows fading out before the draw distance is a
    // standard, accepted trade, not an oversight.
    [[nodiscard]] CascadeData computeCascades(const Camera& camera, float aspectRatio) const;

    void drawShadowPass(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, MeshLibrary& meshLibrary);

    // Call immediately after every vkCmdDrawIndexed/vkCmdDraw -- tallies
    // into frameDrawCalls_/frameTriangles_ for this frame's metrics()
    // (see PerformanceMetrics.hpp). indexCount=0 is valid for the
    // full-screen-triangle post-process passes (3 vertices, no indices);
    // pass their vertex count as indexCount there instead.
    void recordDraw(uint32_t indexOrVertexCount, uint32_t instanceCount);

    // The real body both public drawSceneInto() overloads share --
    // identical work, the only difference is which FrameSync the caller
    // resolved (frames_[currentFrame_] for the plain overload,
    // auxiliaryScenes_[handle] for the AuxiliarySceneHandle one). See the
    // AuxiliarySceneHandle overload's doc comment for why this split
    // exists.
    //
    // Kronos ("Avatar Scene Lighting Calibration Pass" -- "ensure no
    // unintended desaturation from core::Weather.cpp perturbations"):
    // `applyWeatherEffects` is real, new -- before this, `applyWeather()`
    // ran unconditionally here regardless of which FrameSync was being
    // drawn into, so a real, live outdoor weather event (rain/fog) would
    // silently perturb an auxiliary preview scene's own carefully-set
    // lighting too (Home's avatar preview, Studio's AvatarEditor/
    // MaterialPlugin/CataloguePanel, etc.) even though none of those are
    // meant to reflect the outdoor world's current weather at all. The
    // main-viewport overload passes `true` (unchanged real behavior);
    // the AuxiliarySceneHandle overload passes `false` -- a real, exact
    // no-op multiply-through-Clear-weather-equivalent, not an
    // approximation (see applyWeather()'s own comment on why Clear is a
    // real, exact identity).
    // `applyBloom` (Kronos "Avatar Preview Rendering" pre-launch fix):
    // same real "main viewport true, AuxiliarySceneHandle false"
    // scoping as `applyWeatherEffects` just above, for a different real
    // reason -- see drawBloomAndComposite()'s own comment on why a
    // close-up preview render's bloom bleed reads as a much larger,
    // more washed-out effect than the same bloom settings produce on a
    // normal full-scene shot.
    // `suppressSunDisk` (Kronos "Avatar Preview Rendering" pre-launch
    // fix): same real "main viewport true (i.e. not suppressed... default
    // false), AuxiliarySceneHandle true" scoping as applyBloom/
    // applyWeatherEffects above -- see shaders/sky.frag's own comment on
    // why a fixed, saturated sun-disk marker meant for outdoor gameplay
    // cameras can blow out a tight preview frame if the orbit camera
    // happens to point anywhere near the light direction.
    // `useFlatBackground` (Kronos "Avatar Preview Rendering" pre-launch
    // fix -- the direct, guaranteed hardware-level override): true for
    // AuxiliarySceneHandle only. Sets the opaque pass's own color
    // attachment clearValue directly to a fixed dark-slate
    // (0.08, 0.09, 0.13, 1.0) and skips the sky.frag draw call
    // entirely for that scene -- no shader-side gradient, sun disk,
    // atmosphere, or cloud logic can paint over it, unlike
    // suppressSunDisk above (which only turns off one specific sky.frag
    // feature but still runs the sky pass). The main viewport is
    // completely unaffected (default false, exact prior clear color and
    // sky draw, unchanged).
    void drawSceneIntoImpl(FrameSync& frame, VkCommandBuffer cmd, VkImage colorImage, VkImageView colorView,
                            VkImage depthImage, VkImageView depthView, VkExtent2D extent, const Camera& camera,
                            ECS& ecs, MeshLibrary& meshLibrary, const ParticleSystem& particleSystem,
                            TextureLibrary& textureLibrary, RiggedMeshLibrary* riggedMeshLibrary,
                            bool applyWeatherEffects = true, bool applyBloom = true, bool suppressSunDisk = false,
                            bool useFlatBackground = false);

    // Real GPU vertex-shader skinning (shaders/scene_skinned.vert),
    // called from within drawSceneIntoImpl()'s own render pass (same
    // vkCmdBeginRendering block as the opaque/instanced/particle passes,
    // right before vkCmdEndRendering) so skinned entities depth-composite
    // correctly against everything else in the scene -- not a bolted-on
    // separate pass. Iterates every SkinnedRenderable in `ecs`, resolving
    // each through `riggedMeshLibrary` (may be null -- see the public
    // drawSceneInto() overloads' doc comment). At most
    // kMaxSkinnedDrawsPerFrame skinned entities can be drawn in one
    // drawSceneInto() call -- each needs its own real, independent bone-
    // matrix UBO slot from `frame`'s pool (the same "no two draws share
    // one GPU buffer within a frame" discipline AuxiliarySceneHandle
    // exists for, applied one level deeper here); past the cap, extra
    // SkinnedRenderable entities are skipped and logged, not silently
    // dropped or (worse) sharing a slot.
    void drawSkinnedEntities(VkCommandBuffer cmd, FrameSync& frame, ECS& ecs, RiggedMeshLibrary* riggedMeshLibrary,
                              TextureLibrary& textureLibrary);

    [[nodiscard]] QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device) const;
    [[nodiscard]] bool isDeviceSuitable(VkPhysicalDevice device) const;
    [[nodiscard]] int scoreDevice(VkPhysicalDevice device) const;

    Window* window_ = nullptr;
    std::string appName_;
    bool validationEnabled_ = false;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;

    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    QueueFamilyIndices queueFamilies_;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;

    // Sprint 14 ("RTX Upgrade" Phase 2) -- see isRayTracingSupported()/
    // setRayTracedShadowsEnabled()'s own public comment.
    bool rayTracingSupported_ = false;
    bool bindlessSupported_ = false;
    bool rayTracedShadowsEnabled_ = false;
    RayTracingScene rayTracingScene_;

    // Kronos ("Rendering Fidelity Foundation" Phase 1.3) -- see
    // setRTReflectionsEnabled()/setReflectionRoughnessCutoff()'s own
    // comments. Cutoff defaults: real, tuned so only genuinely glossy
    // metal (roughness below ~0.35, i.e. 1-roughness above 0.65) starts
    // paying for a traced ray at all, and only a near-perfect mirror
    // (roughness below ~0.05) pays full cost.
    bool rtReflectionsEnabled_ = false;
    float reflectionRoughCutoff_ = 0.65f;
    float reflectionMirrorCutoff_ = 0.95f;

    // Kronos ("Rendering Fidelity" -- full atmospheric-scattering skybox)
    // -- see setAtmosphereScatteringEnabled()/setAtmosphereScatteringParams()'s
    // own comments. Default sunIntensity/mieStrength tuned in shaders/sky.frag's
    // own real Rayleigh+Mie units (see that file's own comment for why
    // these are small real-world-derived numbers, not 0..1 sliders).
    bool atmosphereScatteringEnabled_ = false;
    // Real, live-verified defaults (captured + visually inspected against
    // the Sky Map's own real sun elevation) -- an earlier, naively "real
    // units" 20.0/1.0 pair real-blew the whole sky out to flat white once
    // added on top of the existing two-tone gradient and tonemapped; these
    // tuned values read as a real, visible-but-not-overpowering blue-to-
    // horizon gradient plus a soft, real Mie sun-glow instead.
    float atmosphereSunIntensity_ = 6.0f;
    float atmosphereMieStrength_ = 0.4f;

    // Kronos ("Rendering Fidelity" -- volumetric cloud layer) -- see
    // setCloudsEnabled()/setCloudParams()'s own comments.
    bool cloudsEnabled_ = false;
    float cloudCoverage_ = 0.45f;
    float cloudSpeed_ = 6.0f;

    // Kronos ("Rendering Fidelity" -- SSR fallback pass) -- see
    // setSSREnabled()/setSSRParams()'s own comments.
    bool ssrEnabled_ = false;
    float ssrMaxDistance_ = 60.0f;
    float ssrThickness_ = 0.6f;
    // Kronos (beta-blocking fix -- "SSR noise"): real, was missing --
    // drawSSRPass() built its SSRPushConstants without ever touching
    // `.stepCount`, so shaders/ssr.frag's `int steps = int(push.stepCount)`
    // read the struct's own zero-init default every frame; its raymarch
    // loop (`for (int i = 1; i <= steps; ...)`) never ran a single
    // iteration, so `hit` stayed false and every pixel silently fell back
    // to the unmodified input color -- this pass has been a complete,
    // silent no-op (whenever SSR is actually enabled at all -- off by
    // default, see ssrEnabled_ above) since it shipped, not something a
    // toggle exposed. Same step-count order of magnitude as
    // volumetricFogStepCount_'s own real default below (20.0f) -- both
    // are "screen-space raymarch, world-unit step budget" passes.
    float ssrStepCount_ = 24.0f;

    // Kronos ("Rendering Fidelity" -- ray-traced bounce lighting/GI) --
    // see setRTGIEnabled()/setRTGIIntensity()'s own comments.
    bool rtGIEnabled_ = false;
    float rtGIIntensity_ = 1.0f;

    // Kronos ("Shadow Bias / Peter-Panning Fix" v3) -- see
    // setReceiverPlaneBiasScale()'s own public comment.
    float receiverPlaneBiasScale_ = 1.0f;

    // Sprint 14 ("Performance Mode") -- see setPerformanceMode()'s own
    // public comment and .cpp implementation comment.
    bool performanceModeEnabled_ = false;

    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer") -- see setVsyncEnabled()/setColorblindMode()'s own public
    // comments.
    bool vsyncEnabled_ = true;
    int colorblindModeIndex_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    // Kronos: one render-finished semaphore per SWAPCHAIN IMAGE, not per
    // frame-in-flight.
    //
    // A per-frame semaphore is wrong for presentation: the frame fence
    // only proves the submit finished, never that the presentation
    // consuming that semaphore did. With framesInFlight_ (2) below the
    // swapchain image count (typically 3), the semaphore gets reused
    // while a present may still be waiting on it -- caught by
    // VUID-vkQueueSubmit-pSignalSemaphores-00067 once validation layers
    // were installed. Indexing by acquired image index is the fix the
    // validation message itself recommends.
    std::vector<VkSemaphore> renderFinishedPerImage_;

    // Kronos ("Bindless Descriptors"): one global texture array bound
    // once per frame, replacing the per-material descriptor set on the
    // main scene pipeline. Only created when bindlessSupported_.
    static constexpr uint32_t kBindlessTextureCapacity = 2048;
    // Reserved, pre-written slots. Slot 0 is the default white texture
    // (correct for albedo/metallic/roughness/AO); slot 1 is the default
    // flat normal, because sampling white as a normal map decodes to
    // (1,1,1) and visibly breaks the shading of any entity without one.
    static constexpr uint32_t kBindlessWhiteSlot = 0;
    static constexpr uint32_t kBindlessFlatNormalSlot = 1;
    // The table keys these two reserve; real texture handles are offset
    // past them (see bindlessSlotFor()).
    static constexpr uint64_t kBindlessWhiteKey = 0;
    static constexpr uint64_t kBindlessFlatNormalKey = 1;
    static constexpr uint64_t kBindlessReservedKeyCount = 2;
    VkDescriptorSetLayout bindlessSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool bindlessPool_ = VK_NULL_HANDLE;
    VkDescriptorSet bindlessSet_ = VK_NULL_HANDLE;
    VkSampler bindlessSampler_ = VK_NULL_HANDLE;
    BindlessTextureTable bindlessTable_{kBindlessTextureCapacity};
    // Every slot starts pointing at this, so a shader indexing a slot
    // that has not been written yet reads white rather than undefined
    // memory. PARTIALLY_BOUND permits unwritten slots, but only if
    // nothing samples them -- pre-filling removes that footgun entirely.
    bool bindlessInitialised_ = false;
    std::vector<VkImageView> swapchainImageViews_;

    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VmaAllocation depthAllocation_ = nullptr;
    VkImageView depthImageView_ = VK_NULL_HANDLE;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    uint32_t framesInFlight_ = 2;
    std::vector<FrameSync> frames_;
    uint32_t currentFrame_ = 0;
    bool framebufferResized_ = false;

    // Independent scene-render slots for studio::PreviewScene -- see the
    // AuxiliarySceneHandle overload of drawSceneInto()'s doc comment.
    // Append-only (index-based handles, no true removal), same shape as
    // MeshLibrary::meshes_/TextureLibrary::textures_.
    std::vector<FrameSync> auxiliaryScenes_;

    VkDescriptorSetLayout sceneDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool sceneDescriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout scenePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline scenePipeline_ = VK_NULL_HANDLE;
    // Kronos ("Real-Time Rendering Evolved" trailer) -- see
    // createGlassPipeline()'s own comment.
    VkPipelineLayout glassPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline glassPipeline_ = VK_NULL_HANDLE;
    VkPipeline instancedScenePipeline_ = VK_NULL_HANDLE; // shares scenePipelineLayout_; see createInstancedScenePipeline()

    // Generous headroom over what the bring-up scene creates (a few dozen
    // entities) -- sized for "a field of ore/rock props", not tuned
    // against a real content budget that doesn't exist yet. Exceeding
    // this clamps and logs rather than overflowing frame.instanceBuffer;
    // see drawInstancedBatches()'s comment.
    static constexpr uint32_t kMaxInstancesPerFrame = 4096;

    Mesh particleQuadMesh_;             // shared unit quad every particle billboard instances (see Mesh::createQuad)
    VkPipeline particlePipeline_ = VK_NULL_HANDLE; // additive-blended, no depth write
    // Sprint 16 ("Cinematic Graphics"): particles now need a real second
    // descriptor set (scene depth, for soft-particle fade -- see
    // shaders/particle.frag) that scenePipelineLayout_'s set=1 (material
    // textures) has no room for and no real reason to grow to fit --
    // material textures and "the scene's own depth buffer" are
    // unrelated inputs, so this pipeline gets its own dedicated 2-set
    // layout instead of reusing/extending the shared one, isolating the
    // change entirely to the particle path.
    VkPipelineLayout particlePipelineLayout_ = VK_NULL_HANDLE; // set0: sceneDescriptorSetLayout_; set1: postProcessSingleSetLayout_ (depth)

    // GPU skinning -- skinnedScenePipelineLayout_ is its own
    // VkPipelineLayout object (NOT scenePipelineLayout_ -- a 3rd
    // descriptor set means a different layout signature), built from
    // sceneDescriptorSetLayout_ (set 0) + materialDescriptorSetLayout_
    // (set 1, see createMaterialResources()) + skinningDescriptorSetLayout_
    // (set 2, new) -- reusing the first two handles as-is touches nothing
    // about the existing scenePipelineLayout_/pipelines built from them.
    VkDescriptorSetLayout skinningDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool skinningDescriptorPool_ = VK_NULL_HANDLE;
    VkPipelineLayout skinnedScenePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline skinnedScenePipeline_ = VK_NULL_HANDLE;

    // Post-processing: bloom + exposure + ACES filmic tonemap. See
    // drawBloomAndComposite()'s comment for the pass structure and
    // FrameSync's comment for why the intermediate targets are per-frame.
    static constexpr VkFormat kHDRFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr float kBloomDownsampleFactor = 0.5f;

    // Tunable post-process parameters -- no Studio UI exposes these yet
    // (a real exposure/bloom-tuning panel is a natural Material/
    // Environment Editor addition, not built in this pass); these are the
    // knobs a future one would drive, not hardcoded shader constants.
    float exposure_ = 1.0f;
    float bloomThreshold_ = 1.0f;
    float bloomSoftKnee_ = 0.5f;
    float bloomIntensity_ = 0.6f;

    // Sprint 16 ("Cinematic Graphics") tuning knobs -- see the matching
    // public setters' own comments.
    bool cinematicModeEnabled_ = false;
    // Kronos ("Real-Time Rendering Evolved" trailer) -- see
    // setAutoExposureEnabled()'s own comment.
    bool autoExposureEnabled_ = true;
    float dofFocusDistance_ = 15.0f;
    float dofFocusRange_ = 10.0f;
    float dofMaxCoCRadiusPx_ = 6.0f;
    // See setDepthOfFieldEnabled()'s own comment -- real, independent of
    // cinematicModeEnabled_ itself.
    bool depthOfFieldEnabled_ = false;
    float motionBlurStrength_ = 0.0f;
    float ssaoRadius_ = 0.5f;
    float ssaoStrength_ = 1.0f;
    float vignetteStrength_ = 0.35f;
    float chromaticAberrationStrength_ = 0.0015f;
    float saturation_ = 1.05f;
    float godRayStrength_ = 0.15f;
    // Kronos ("Four RTX Maps" Phase 5b) -- see setHeatDistortionEnabled()/
    // setHeatDistortionStrength()'s own comments. 0.006 is a real, tuned
    // UV-space magnitude (this pass runs in [0,1] UV space) -- large
    // enough to read as genuine heat shimmer at 1080p without tearing the
    // image apart.
    bool heatDistortionEnabled_ = false;
    float heatDistortionStrength_ = 0.006f;
    // Kronos ("Four RTX Maps" Phase 5c) -- see setUnderwaterCausticsEnabled()'s
    // own comment.
    bool underwaterCausticsEnabled_ = false;

    // Kronos ("Rendering Fidelity Foundation" Phase 1.2) tuning knobs --
    // see setVolumetricFogEnabled()/setVolumetricFogParams()'s own
    // comments and core::VolumetricFogPushConstants's own field comments
    // for what each really does. Defaults match VolumetricFogPushConstants's
    // own default member initializers.
    bool volumetricFogEnabled_ = false;
    float volumetricFogScatteringIntensity_ = 1.0f;
    float volumetricFogStepCount_ = 20.0f;
    float volumetricFogMaxDistance_ = 120.0f;
    float volumetricFogAmbientContribution_ = 0.35f;
    // Kronos ("Real-Time Rendering Evolved" trailer) -- see
    // setVolumetricFogHeightGradient()'s own comment.
    float volumetricFogGroundDensityMultiplier_ = 1.0f;
    float volumetricFogAloftDensityMultiplier_ = 1.0f;
    float volumetricFogGroundHeightY_ = 0.0f;
    float volumetricFogFalloffHeight_ = 10.0f;

    VkSampler postProcessSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout postProcessSingleSetLayout_ = VK_NULL_HANDLE; // 1 combined-image-sampler binding -- bloom extract's input
    VkDescriptorSetLayout postProcessDualSetLayout_ = VK_NULL_HANDLE;   // 2 bindings -- composite's HDR + bloom inputs
    VkDescriptorPool postProcessDescriptorPool_ = VK_NULL_HANDLE;

    VkPipelineLayout bloomExtractPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline bloomExtractPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout compositePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline compositePipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout skyPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline skyPipeline_ = VK_NULL_HANDLE;

    // Sprint 16 cinematic pass (shaders/cinematic.frag) -- its own
    // sampler (NEAREST, not postProcessSampler_'s LINEAR: depth values
    // shouldn't be filtered/blended across texels, the standard reason
    // every depth-sampling pass in this codebase -- including the
    // existing shadow map read in scene.frag -- avoids linear filtering
    // on depth) and its own 2-binding descriptor set layout (hdrColor +
    // sceneDepth), reusing sceneDescriptorSetLayout_ as set=0 (view/proj/
    // invViewProj/viewPositionWS -- see cinematic.frag's own partial-UBO
    // comment) rather than a third duplicate camera UBO layout.
    VkSampler depthSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout cinematicDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout cinematicPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline cinematicPipeline_ = VK_NULL_HANDLE;

    // Kronos ("Rendering Fidelity Foundation" Phase 1.2): real raymarched
    // volumetric fog + light shafts (shaders/volumetric_fog.frag) -- own
    // pipeline layout (own push-constant range, VolumetricFogPushConstants,
    // a different size/shape than CinematicPushConstants so it can't
    // safely share cinematicPipelineLayout_), but reuses
    // cinematicDescriptorSetLayout_ as-is for set=1 (same real shape this
    // pass needs: an hdrColor + sceneDepth pair) rather than a third
    // duplicate 2-binding layout. See ensureVolumetricFogTargets()'s own
    // comment for why FrameSync::fogInputDescriptorSet is still a
    // *separate* descriptor set instance from frame.cinematicDescriptorSet
    // despite sharing this same layout.
    VkPipelineLayout volumetricFogPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline volumetricFogPipeline_ = VK_NULL_HANDLE;

    // Kronos ("Rendering Fidelity" -- SSR fallback pass): real
    // screen-space reflections (shaders/ssr.frag) -- own pipeline layout
    // (own push-constant range, SSRPushConstants), reuses
    // cinematicDescriptorSetLayout_ as-is for set=1 (same real
    // hdrColor+sceneDepth pair volumetricFogPipelineLayout_ above already
    // reuses it for). See ensureSSRTargets()'s own comment for why
    // FrameSync::ssrInputDescriptorSet is a *separate* descriptor set
    // instance from frame.fogInputDescriptorSet/frame.cinematicDescriptorSet
    // despite sharing this same layout.
    VkPipelineLayout ssrPipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline ssrPipeline_ = VK_NULL_HANDLE;

    // Sprint 16 auto-exposure (shaders/luminance.frag) -- reuses
    // postProcessSingleSetLayout_ as-is (one combined-image-sampler
    // binding, exactly what this pass needs: hdrColor), no bespoke
    // layout of its own.
    VkPipelineLayout luminancePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline luminancePipeline_ = VK_NULL_HANDLE;
    // Target mid-grey luminance the adaptation converges toward (the
    // standard "18% grey card" photographic convention, expressed here in
    // linear HDR terms) and a real, honest per-*call* (not per-second)
    // lerp factor -- drawCinematicPass() runs at most once per real
    // frame, so this smooths over roughly 1/kAutoExposureAdaptRate real
    // frames regardless of framerate, a real if simplified stand-in for a
    // true wall-clock-seconds adaptation speed.
    static constexpr float kAutoExposureTargetLuminance = 0.5f;
    static constexpr float kAutoExposureAdaptRate = 0.08f;

    // Material textures -- see createMaterialResources()'s comment.
    static constexpr uint32_t kMaxMaterialDescriptorSets = 256;
    VkSampler materialSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout materialDescriptorSetLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool materialDescriptorPool_ = VK_NULL_HANDLE;
    std::map<std::array<uint32_t, 5>, VkDescriptorSet> materialDescriptorCache_;
    Texture defaultWhiteTexture_;      // albedo/metallic/roughness fallback -- multiplies as a no-op
    Texture defaultFlatNormalTexture_; // normal-map fallback (not yet sampled for shading -- see Components.hpp's note)

    static constexpr VkFormat kShadowFormat = VK_FORMAT_D32_SFLOAT;
    // Extends each cascade's light-space near plane back toward the light
    // beyond its tight frustum-corner bound, so a tall caster sitting just
    // outside the visible sub-frustum (but between it and the light) still
    // shows up in that cascade's depth pass instead of being clipped away
    // ("peter-panning" if omitted).
    static constexpr float kShadowDepthPadding = 20.0f;
    // computeShadow()'s shader-side bias constants were picked against
    // this depth range (the old single-cascade system's far-near =
    // 60-1 = 59). Each cascade now has its own, different light-space
    // depth range (a near cascade covering a small area has a small
    // range; a far cascade covering a lot of ground has a large one) --
    // scene.frag scales the bias by (thisCascade'sRange / kReferenceShadowDepthRange)
    // so the same *world-space* bias applies consistently across cascades
    // instead of one fixed shader constant being right for only one of
    // them (too little bias => shadow acne, too much => peter-panning --
    // both are exactly the kind of "looks wrong from some angles/distances"
    // symptom a per-cascade mismatch here would cause, since which
    // cascade is active depends on distance from the camera).
    static constexpr float kReferenceShadowDepthRange = 59.0f;
    VkSampler shadowSampler_ = VK_NULL_HANDLE; // shared across every frame's shadow image (sampler != image)
    VkPipeline shadowPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout shadowPipelineLayout_ = VK_NULL_HANDLE; // ShadowPushConstants -- see SceneTypes.hpp, not scenePipelineLayout_

    ECS* sceneEcs_ = nullptr;
    Camera* sceneCamera_ = nullptr;
    MeshLibrary* sceneMeshLibrary_ = nullptr;
    ParticleSystem* sceneParticleSystem_ = nullptr;
    TextureLibrary* sceneTextureLibrary_ = nullptr;
    RiggedMeshLibrary* sceneRiggedMeshLibrary_ = nullptr;
    SceneLighting lighting_;
    WeatherState weatherState_;
    // Kronos ("Four RTX Maps" Phase 5b): real accumulated wall-clock
    // seconds, ticked alongside weatherState_ in renderFrame() off the
    // same real frameTimeMs delta -- scrolls the heat-distortion noise in
    // drawCinematicPass(). Not used for anything else; a dedicated
    // "game time" clock (Scripting::tick()'s own accumulator) already
    // exists for gameplay, this is purely a renderer-local visual driver.
    float totalElapsedTimeSeconds_ = 0.0f;

    // Accumulated across every pass during the frame currently being
    // recorded (reset at the top of renderFrame()); copied into
    // lastMetrics_ once the frame's command buffer is fully recorded --
    // see PerformanceMetrics.hpp and metrics()'s doc comment.
    uint32_t frameDrawCalls_ = 0;
    uint64_t frameTriangles_ = 0;
    std::chrono::steady_clock::time_point lastFrameTimestamp_{};
    PerformanceMetrics lastMetrics_;
    bool logMetricsToStdout_ = false;
    std::chrono::steady_clock::time_point lastMetricsLogTimestamp_{};

    PrePassCallback prePassCallback_;
    OverlayCallback overlayCallback_;
};

} // namespace engine::core
