#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>

namespace engine::core {

// Number of cascade splits the shadow system uses -- shared between
// SceneUBO's array size, Renderer's cascade math, and every shader that
// reads lightViewProj[]/cascadeSplitsView, so it's declared exactly once
// here rather than as a magic "3" repeated in four places.
inline constexpr uint32_t kShadowCascadeCount = 4;

// Clustered forward+ limits. Must match cluster_build.comp / lights.glsl.
inline constexpr uint32_t kMaxGpuLights = 1024;
inline constexpr uint32_t kMaxLightsPerCluster = 128;
inline constexpr uint32_t kClusterTilesX = 16;
inline constexpr uint32_t kClusterTilesY = 9;
inline constexpr uint32_t kClusterSlices = 24;
inline constexpr uint32_t kMaxObjectRecords = 8192;
inline constexpr uint32_t kMaxShadowedSpotLights = 4;

// Retained for API compatibility; point lights are no longer capped by the UBO.
inline constexpr uint32_t kMaxPointLights = 4;

// Must exactly match the `push_constant` block in shaders/scene.vert and
// shaders/scene.frag. Every member is already a multiple of 16 bytes, so
// this layout is unambiguous between GLSL's push-constant rules and C++
// struct layout without any manual padding.
struct ObjectPushConstants {
    glm::mat4 model;
    glm::vec4 baseColor;
    glm::vec4 metallicRoughness; // x: metallic, y: roughness, z: normal map intensity (see Components.hpp's Renderable::normalIntensity), w: unused
    glm::vec4 emissive;          // rgb: emissive color, a: intensity multiplier -- see Components.hpp's Renderable
    // Kronos ("Bindless Descriptors"): indices into the global texture
    // array, replacing the per-material descriptor set for this pipeline.
    //
    // Packed two-per-uint because push constants are guaranteed only 128
    // bytes and the block above already uses 112. Sixteen bits each still
    // addresses 65535 textures, far beyond BindlessTextureTable's real
    // capacity, so nothing is lost by packing.
    //   x = albedo | (normal   << 16)
    //   y = metallic | (roughness << 16)
    //   z = ao
    //   w = unlitSilhouette flag (0/1) -- see Components.hpp's
    //       Renderable::unlitSilhouette. Set directly by the draw loop,
    //       not by packTextureIndices() (which returns an all-zero uvec4
    //       whenever bindless isn't initialised, since none of its other
    //       three fields mean anything without it) -- this flag has to
    //       keep working on non-bindless devices too.
    glm::uvec4 textureIndices{0u, 0u, 0u, 0u};
};

// Must exactly match the `push_constant` block in shaders/glass.vert and
// shaders/glass.frag. Kronos ("Real-Time Rendering Evolved" trailer) --
// see shaders/glass.frag's own header comment. Deliberately a separate,
// smaller layout from ObjectPushConstants/scenePipelineLayout_ -- glass
// shading needs no material textures (set=1) and has different
// per-object parameters (tint + IOR, not metallic/roughness/emissive).
struct GlassPushConstants {
    glm::mat4 model;
    glm::vec4 tintColor; // rgb: tint, a: transmission strength (0..1) -- see Components.hpp's Renderable::transmission
    glm::vec4 params;    // x: IOR (Renderable::transmissionIor), y: roughness (reserved), z/w: unused
};

// Must exactly match the `push_constant` block in shaders/shadow.vert.
// Deliberately a separate (smaller) layout from ObjectPushConstants/
// scenePipelineLayout_ -- the shadow pass needs to know which cascade
// it's rendering into (viewIndex, to pick a SceneUBO shadow matrix),
// which the main pass has no use for, and doesn't need baseColor/
// metallicRoughness/emissive at all (shadow.vert never reads them). See
// Renderer.cpp's createShadowPipeline() for why this got its own
// VkPipelineLayout once CSM made the two passes' needs actually diverge
// (before CSM, the shadow pass could get away with reusing the main
// pass's layout since it only needed a subset of the same fields).
struct ShadowPushConstants {
    glm::mat4 model;
    int32_t viewIndex = 0; // cascade, or kShadowCascadeCount + spot shadow slot
};

// Per-instance vertex data for the GPU-driven instanced draw path (see
// Renderer::drawInstancedBatches) -- the batched equivalent of what
// ObjectPushConstants carries per individual draw call. Bound at vertex
// input binding 1 with VK_VERTEX_INPUT_RATE_INSTANCE, alongside the
// regular per-vertex Vertex data at binding 0 -- one instanced draw call
// (vkCmdDrawIndexed with instanceCount > 1) replaces what would otherwise
// be `instanceCount` individual draw calls + push-constant updates, for
// entities that share a mesh and opted into it (Renderable::instanced,
// see Components.hpp). Consumed by shaders/scene_instanced.vert, which
// forwards these same fields onward to the shared shaders/scene.frag --
// see that file's header comment for why the fragment shader doesn't care
// which vertex shader produced its inputs.
struct InstanceData {
    glm::mat4 model;
    glm::vec4 baseColor;
    glm::vec4 metallicRoughness;
    glm::vec4 emissive;
    // Packed bindless texture slots, matching ObjectPushConstants::
    // textureIndices. Instanced draws bind set 0 only -- they never bind a
    // per-material set at all -- so before bindless they had no way to
    // texture themselves. Carrying the slots per-instance is what gives
    // them one.
    glm::uvec4 textureIndices{0u, 0u, 0u, 0u};

    static VkVertexInputBindingDescription bindingDescription();
    static std::vector<VkVertexInputAttributeDescription> attributeDescriptions();
};

// Per-instance vertex data for billboarded particle rendering (see
// Renderer::drawParticles() and shaders/particle.vert) -- one instance
// per live core::Particle, drawn via the same GPU-instancing mechanism as
// InstanceData above, but against a single shared unit quad
// (Mesh::createQuad()) instead of arbitrary meshes: the vertex shader
// rebuilds each quad to face the camera from `positionSize` + the view
// matrix's right/up axes, so there's no per-particle mesh or model matrix
// at all, just a world position, a size, and a color.
struct ParticleInstanceData {
    glm::vec4 positionSize; // xyz: world position, w: current billboard half-size
    glm::vec4 color;

    static VkVertexInputBindingDescription bindingDescription();
    static std::vector<VkVertexInputAttributeDescription> attributeDescriptions();
};

// Must exactly match the `push_constant` block in shaders/bloom_extract.frag.
struct BloomPushConstants {
    float threshold = 1.0f;
    float softKnee = 0.5f;
};

// Must exactly match the `push_constant` block in shaders/composite.frag.
struct CompositePushConstants {
    float exposure = 1.0f;
    float bloomIntensity = 0.6f;
    // Sprint 16 ("Cinematic Graphics"): vignette/chromatic aberration
    // strength + a saturation grading knob, plus the sun's screen-space
    // position for the god-ray radial scatter -- see
    // shaders/composite.frag's own header comment on why these ride
    // along in the *composite* push constants (last-stage lens/film
    // artifacts) rather than the earlier shaders/cinematic.frag pass.
    // Deliberately all plain floats (no trailing vec4) -- GLSL's
    // push_constant block follows std430 rules, where a vec4 member
    // forces 16-byte alignment on whatever follows it; keeping every
    // field a lone float sidesteps that entirely and keeps this struct's
    // C++ layout trivially identical to the shader's, the same reasoning
    // SceneUBO's own fields (all vec4/mat4, never a lone float) apply in
    // the opposite direction.
    float vignetteStrength = 0.0f;
    float chromaticAberrationStrength = 0.0f;
    float saturation = 1.0f;
    float godRayStrength = 0.0f;
    float sunScreenX = 0.0f;
    float sunScreenY = 0.0f;
    float sunVisible = 0.0f; // 1.0 if the sun is in front of the camera this frame, 0.0 otherwise
    // Kronos ("Settings Panel v2 + Input Remapping + Accessibility
    // Layer" -- "Accessibility: Colorblind modes"): real, new -- 0=None/
    // 1=Protanopia/2=Deuteranopia/3=Tritanopia, matching
    // accessibility::ColorblindMode's own real enum order. A plain
    // float, not an int, for the same std430-layout reason every other
    // field in this struct already is one (see this struct's own header
    // comment) -- shaders/composite.frag rounds it back to an integer
    // mode index itself.
    float colorblindMode = 0.0f;
    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline"):
    // 0=ACES filmic (the long-standing default), 1=AgX -- see
    // shaders/composite.frag's own agxTonemap() comment. A plain float,
    // not an int, for the same std430-layout reason every other field
    // in this struct already is one.
    float tonemapOperator = 0.0f;
    // Kronos ("Cinematic Camera Physics & Post-Processing Pipeline" --
    // real 3D LUT color grading): see Renderer::setColorGradingLutStrength()'s
    // own comment. Also a plain float, consistent with every other field.
    float lutStrength = 1.0f;

    // Kronos ("VHS / Analog Bodycam"): PROJECT: DESPAIR's found-footage
    // look -- see shaders/composite.frag's own header comment for why
    // these three ride at the very end of this already-existing lens/
    // film-artifact push-constant block rather than a second pass.
    // Applied unconditionally (not gated behind cinematicModeEnabled_ like
    // vignette/CA/god-rays above): this is DESPAIR's own visual identity,
    // not a graphics-quality toggle, and stays at its real zero-effect
    // default for every scene that never calls
    // Renderer::setVhsBodycamSettings()/setVhsStaticNoiseIntensity().
    float fisheyeStrength = 0.0f;
    float scanlineIntensity = 0.0f;
    // Real, honest proximity-driven burst -- see
    // despair::computeVhsStaticNoiseIntensity()'s own header comment for
    // the pure distance-to-threat mapping that feeds this every tick.
    float staticNoiseIntensity = 0.0f;
    // Real elapsed seconds (Renderer::totalElapsedTimeSeconds_), the same
    // clock shaders/scene.frag's heat-shimmer already scrolls by -- scrolls
    // the scanline roll and reseeds the static-noise hash so both actually
    // animate instead of being a fixed per-pixel pattern.
    float time = 0.0f;
};

// Must exactly match the `push_constant` block in shaders/cinematic.frag.
struct CinematicPushConstants {
    glm::mat4 previousViewProj{1.0f};
    float focusDistance = 15.0f;
    float focusRange = 10.0f;
    float maxCoCRadiusPx = 6.0f;
    float motionBlurStrength = 0.0f;
    float ssaoRadius = 0.5f;
    float ssaoStrength = 1.0f;
    float dofEnabled = 1.0f;
    float ssaoEnabled = 1.0f;
    // Kronos ("Four RTX Maps" Phase 5b, Volcano Map): real heat-haze
    // color-buffer UV shimmer -- see shaders/cinematic.frag's own comment
    // for why this lives here (an extra step in the existing single
    // post-process pass) rather than as its own new image-chain stage.
    // 0 is a real, exact identity (no distortion at all), matching
    // motionBlurStrength's own "magnitude doubles as the enable flag"
    // convention just above.
    float heatDistortionStrength = 0.0f;
    float time = 0.0f; // real elapsed seconds, scrolls the heat-shimmer noise
};

// Kronos ("Rendering Fidelity Foundation" Phase 1.2) -- must exactly match
// the `push_constant` block in shaders/volumetric_fog.frag. See
// Renderer::setVolumetricFogParams()'s own comment for the real, tuned
// default values these mirror.
struct VolumetricFogPushConstants {
    float scatteringIntensity = 1.0f; // real in-scattering brightness multiplier
    float maxDistance = 120.0f;       // real raymarch cutoff, world units -- beyond this the march just stops, not an error
    float stepCount = 20.0f;          // real step count -- a float (not int) so it rides the same std140-free push-constant packing as every other field here, GLSL truncates it for the loop bound
    float ambientFogContribution = 0.35f; // real, even-when-fully-lit floor so fog reads as atmospheric haze, not "pure light shaft or nothing"
    // Kronos ("Real-Time Rendering Evolved" trailer): real height-based
    // density gradient -- every march sample's own real extinction now
    // uses scene.fogColorDensity.a (the real, existing per-scene base
    // density every caller already sets via Renderer::setLighting())
    // *multiplied* by a real height-blended factor between
    // groundDensityMultiplier (at/below groundHeightY) and
    // aloftDensityMultiplier (falloffHeight world units above it), instead
    // of applying that base density flatly along the whole ray.
    // groundDensityMultiplier == aloftDensityMultiplier == 1.0 (this
    // struct's own real default) is an exact, honest no-op reproducing
    // the old flat-density look for every existing caller that never
    // touches these four new fields.
    float groundDensityMultiplier = 1.0f;
    float aloftDensityMultiplier = 1.0f;
    float groundHeightY = 0.0f;
    float falloffHeight = 10.0f;
};

// Must exactly match shaders/ssr.frag's own push_constant block. See
// Renderer::setSSRParams()'s own comment for maxDistance/thickness;
// stepCount mirrors VolumetricFogPushConstants::stepCount's own real
// "float, not int, for push-constant packing simplicity" reasoning.
struct SSRPushConstants {
    float maxDistance = 60.0f;
    float thickness = 0.6f;
    float stepCount = 32.0f;
};

// Mirrors shaders/kronos/scene_ubo.glsl byte-for-byte (std140; every
// member is 16-byte sized so the C++ layout is identical).
struct SceneUBO {
    glm::mat4 view;
    glm::mat4 proj; // jittered when TAA is on
    glm::mat4 invViewProj;
    glm::mat4 viewProjNoJitter;
    glm::mat4 prevViewProjNoJitter;
    glm::mat4 lightViewProj[kShadowCascadeCount];
    glm::vec4 cascadeSplitsView;
    glm::vec4 cascadeTexelWorld;
    glm::vec4 cascadeDepthRange;
    glm::vec4 shadowParams; // x receiver-plane bias scale, y tan(sun angular radius), z map resolution, w filter 0 hard / 1 PCF / 2 PCSS
    glm::vec4 lightDirectionWS;
    glm::vec4 lightColorIntensity;
    glm::vec4 viewPositionWS;
    glm::vec4 ambientColor;
    glm::vec4 ambientGroundColor;
    glm::vec4 fogColorDensity;
    glm::vec4 skyZenithColor;
    glm::vec4 skyHorizonColor;
    glm::vec4 renderFlags{0.0f};      // x RT shadows, y performance mode, z wetness, w caustics
    glm::vec4 reflectionParams{0.0f}; // x RT reflections, y rough cutoff, z metallic cutoff
    glm::vec4 atmosphereParams{0.0f}; // x enabled, y sun radiance, z mie strength, w suppress sun disk
    glm::vec4 cloudParams{0.0f};      // x enabled, y coverage, z wind speed, w time
    glm::vec4 giParams{0.0f};         // x RT GI, y intensity
    glm::vec4 iblParams{0.0f};        // x specular intensity, y reflection normalization, z prefiltered max mip, w valid
    glm::vec4 taaJitter{0.0f};        // xy current jitter (NDC), zw previous
    glm::vec4 screenSize{0.0f};       // xy pixels, zw reciprocal
    glm::vec4 clusterParams{0.0f};    // x slice scale, y slice bias, z tile size (px), w light count
    glm::uvec4 clusterDims{0u};       // xyz grid dims, w max lights per cluster
    glm::vec4 frameParams{0.0f};      // x frame index, y 1 when TAA resolves this view
    glm::mat4 spotShadowViewProj[kMaxShadowedSpotLights];
    glm::vec4 spotShadowTexelScale{0.0f}; // per slot: world size of one shadow texel per metre from the light
};
static_assert(sizeof(SceneUBO) == 64 * (9 + kMaxShadowedSpotLights) + 16 * 24,
              "SceneUBO must match kronos/scene_ubo.glsl");

// Mirrors GpuLight in shaders/kronos/scene_resources.glsl (std430).
struct GpuLight {
    glm::vec4 positionRange;
    glm::vec4 colorIntensity;
    glm::vec4 directionType; // xyz direction the light travels, w 0 point / 1 spot
    glm::vec4 spotParams;    // x cos(outer), y 1/(cos(inner) - cos(outer)), z softening radius^2, w shadow slot
};
inline constexpr float kGpuLightNoShadow = -1.0f;
// Set by gatherLights; render::assignSpotShadows replaces it with a slot or kGpuLightNoShadow.
inline constexpr float kGpuLightShadowRequested = -2.0f;

// Mirrors ObjectRecord in shaders/kronos/object_records.glsl (std430).
struct GpuObjectRecord {
    glm::mat4 prevModel{1.0f};
    glm::vec4 clearcoat{0.0f, 0.1f, 0.0f, 0.0f}; // x strength, y perceptual roughness, z anisotropy, w anisotropy rotation
    glm::vec4 sheen{0.0f, 0.0f, 0.0f, 0.5f};     // rgb color, a perceptual roughness
    glm::vec4 misc{0.5f, 0.0f, 0.0f, 0.0f};      // x specular reflectance, y prevModel valid
};

// Plain data a caller of Renderer::setLighting() fills in -- kept
// separate from SceneUBO so callers don't have to know the UBO's exact
// packing (e.g. that direction/color need a padding component for std140).
//
// ambient/ambientGround are a two-tone hemisphere approximation of indirect
// light (sky-facing surfaces catch more blue-ish sky light, ground-facing
// surfaces catch dimmer, warmer bounce light off the ground) -- a standard,
// cheap stand-in for real image-based lighting (no environment cubemap
// exists yet, see scene.frag's header comment). A single flat ambient
// value reads as visually "bare"/flat because every surface, lit or
// shadowed, gets the exact same fill regardless of which way it faces;
// blending between two tones by surface normal is most of the perceptual
// benefit of real IBL at a fraction of the cost.
struct SceneLighting {
    glm::vec3 directionWS{-0.4f, -1.0f, -0.3f};
    glm::vec3 color{1.0f, 0.97f, 0.92f};
    float intensity = 3.0f;
    glm::vec3 ambient{0.12f, 0.15f, 0.22f};      // sky tone -- cool, brighter (up-facing surfaces)
    glm::vec3 ambientGround{0.09f, 0.08f, 0.07f}; // ground tone -- warm, dimmer (down-facing surfaces)

    // Real fog (task category 3) -- exponential-squared (the standard,
    // more physically-plausible falloff vs. linear fog -- see
    // scene.frag's applyFog() for the exact formula), applied by
    // view-space depth. density=0 (the default) is a real, honest
    // "no fog", not a degenerate edge case -- every existing scene that
    // never touches this field renders identically to before this field
    // existed.
    glm::vec3 fogColor{0.6f, 0.65f, 0.75f};
    float fogDensity = 0.0f;

    // Real, basic procedural sky gradient (task category 3) -- see
    // shaders/sky.frag. Two colors, blended by view-ray elevation; no
    // clouds, no sun disk, no environment cubemap (none exists in this
    // renderer at all, see scene.frag's own header comment on why the
    // ambient term is a two-tone hemisphere approximation instead) -- a
    // genuinely "basic" skybox, not a mislabeled full one.
    glm::vec3 skyZenithColor{0.25f, 0.45f, 0.85f};
    glm::vec3 skyHorizonColor{0.75f, 0.80f, 0.85f};

    // Scene-level point lights (key/rim/fill presets). Uploaded to the
    // clustered light buffer together with ECS Light components.
    struct PointLight {
        glm::vec3 position{0.0f};
        float radius = 10.0f;
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
    };
    std::vector<PointLight> pointLights;
};

// Kronos ("Avatar Scene Lighting Calibration Pass" -- "avatar renders
// identically in Home, Studio, and gameplay under neutral lighting"):
// the real, single, shared "neutral indoor" preview lighting preset --
// a warm, low-key directional key light plus a cool rim point light for
// silhouette separation. Originally lived only inside
// runtime::HomeAvatarPreview.cpp as its own file-local
// `cinematicPreviewLighting()`; extracted here as the one real source of
// truth so Home's preview and Studio's AvatarEditor preview can share it
// by real reference instead of by two independently-drifting copies of
// the same numbers -- exactly the risk this pass exists to close.
// Deliberately NOT wired in as `studio::PreviewScene`'s own default:
// every *other* PreviewScene consumer (MaterialPlugin, CataloguePanel,
// AnimationPreviewerPlugin, etc.) still gets that class's own flat,
// neutral-white "lightbox" default -- a warm tint here would bias how a
// material's own true color reads, which those panels specifically need
// to avoid. Only avatar-showing previews opt in, explicitly, via
// `PreviewScene::render()`'s own `lightingOverride` parameter.
[[nodiscard]] const SceneLighting& avatarIndoorPreviewLighting();

} // namespace engine::core
