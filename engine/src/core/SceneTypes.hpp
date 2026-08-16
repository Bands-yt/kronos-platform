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
inline constexpr uint32_t kShadowCascadeCount = 3;

// Sprint 16 ("Cinematic Graphics") -- small fixed array of additional
// point lights (key/rim/fill, per-scene lighting presets) layered on top
// of the single directional sun light that already existed. Fixed-size
// (not a real dynamic/bindless array) so SceneUBO stays a plain, fully
// CPU-mirrorable std140 struct like every other field in it -- 4 is
// comfortably enough for "key + rim + fill + one extra" per scene, the
// brief's own vocabulary (Phase 3: "key lights, rim lights, fill lights
// per scene").
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
// it's rendering into (cascadeIndex, to index SceneUBO.lightViewProj[]),
// which the main pass has no use for, and doesn't need baseColor/
// metallicRoughness/emissive at all (shadow.vert never reads them). See
// Renderer.cpp's createShadowPipeline() for why this got its own
// VkPipelineLayout once CSM made the two passes' needs actually diverge
// (before CSM, the shadow pass could get away with reusing the main
// pass's layout since it only needed a subset of the same fields).
struct ShadowPushConstants {
    glm::mat4 model;
    int32_t cascadeIndex = 0;
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

// Must exactly match the `SceneUBO` uniform block in shaders/scene.vert,
// shaders/scene.frag, and shaders/shadow.vert (std140 layout -- again,
// every member here is already 16-byte-aligned by construction, including
// the mat4 array: std140's array stride for mat4 is 64 bytes with no
// interior padding, identical to a plain C array of glm::mat4).
struct SceneUBO {
    glm::mat4 view;
    glm::mat4 proj;
    glm::mat4 lightViewProj[kShadowCascadeCount]; // world -> light clip space, one per cascade (see Renderer::computeCascades)
    // World-space ray reconstruction for shaders/sky.frag -- see that
    // file's own comment for why a full-screen pass needs this instead
    // of the per-vertex model/view/proj chain every other shader uses.
    // Computed once per frame on the CPU side (Renderer::drawSceneIntoImpl())
    // from the same view/proj above, not per-fragment (a 4x4 inverse per
    // pixel would be real but wasteful when one CPU-side inverse per
    // frame is exactly equivalent).
    glm::mat4 invViewProj;
    glm::vec4 cascadeSplitsView; // x/y/z: view-space far distance of cascades 0/1/2; w: unused (std140 padding)
    glm::vec4 cascadeBiasScale;  // x/y/z: per-cascade shadow-bias scale (see Renderer::kReferenceShadowDepthRange); w: unused
    glm::vec4 lightDirectionWS;    // xyz: direction the light travels
    glm::vec4 lightColorIntensity; // rgb: color, a: intensity
    glm::vec4 viewPositionWS;
    glm::vec4 ambientColor;       // "sky" hemisphere tone -- see scene.frag's hemisphere ambient blend
    glm::vec4 ambientGroundColor; // "ground" hemisphere tone
    // Sprint 6 ("World Systems & Environment") -- real fog + a real basic
    // procedural skybox, both driven by the same core::SceneLighting a
    // caller already fills in via Renderer::setLighting() (see that
    // struct's own comment on the new fields below).
    glm::vec4 fogColorDensity;  // rgb: fog color, a: density (0 = no fog)
    glm::vec4 skyZenithColor;   // rgb: straight-up sky color; a: unused
    glm::vec4 skyHorizonColor;  // rgb: horizon sky color; a: unused
    // Sprint 14 ("RTX Upgrade" Phase 2 / "Performance Mode") -- two real,
    // independent per-frame toggles packed into one vec4 (std140 already
    // pads a lone scalar out to 16 bytes regardless, so a second real
    // scalar rides along for free instead of needing its own field):
    //   x: 1.0 if scene_rt.frag should real-trace a ray-query shadow this
    //      frame (both the user's own real toggle AND a real, currently-
    //      valid non-empty TLAS -- see Renderer::drawSceneIntoImpl()'s
    //      own comment on why it's never just the raw toggle), 0.0
    //      otherwise (real, honest CSM-only fallback, identical to
    //      scene.frag's own unmodified behavior).
    //   y: 1.0 if Performance Mode is on -- real-switches the CSM path
    //      (both scene.frag and scene_rt.frag's fallback) from a 3x3 (9-tap)
    //      PCF loop to a single real center tap, a real, direct per-
    //      fragment shadow-sampling cost cut. See Renderer::setPerformanceMode().
    //   z: real weather wetness (0..1, Renderer::setWeather()), reduces
    //      roughness on upward-facing geometry (see scene.frag).
    //   w: Kronos ("Four RTX Maps" Phase 5c) real underwater caustic-light
    //      strength (Renderer::setUnderwaterCausticsEnabled()) -- was
    //      documented std140 padding before this.
    glm::vec4 renderFlags{0.0f};

    // Sprint 16 point lights -- see kMaxPointLights's comment above.
    // positionRadius: xyz world position, w falloff radius (real
    // inverse-square attenuation, clamped to exactly 0 at/beyond this
    // distance -- see scene.frag's computePointLights()).
    // colorIntensity: rgb color, a intensity. Slots at/beyond
    // pointLightCount.x are zero-intensity (a real no-op multiply, not
    // uninitialized garbage) -- see Renderer::drawSceneIntoImpl().
    glm::vec4 pointLightPositionRadius[kMaxPointLights]{};
    glm::vec4 pointLightColorIntensity[kMaxPointLights]{};
    glm::vec4 pointLightCount{0.0f}; // x: real count as a float (0..kMaxPointLights); yzw: unused padding

    // Kronos ("Rendering Fidelity Foundation" Phase 1.3) -- real hybrid RT
    // reflections, see Renderer::setRTReflectionsEnabled()'s own comment.
    // x: 1.0 real-enables scene_rt.frag's traceReflection() this frame
    // (both the user's own toggle AND a real, currently-valid TLAS -- same
    // "never just the raw toggle" reasoning renderFlags.x already
    // documents), 0.0 otherwise. y/z: the real roughness/metallic trace-
    // cost cutoffs (setReflectionRoughnessCutoff()). w: unused padding.
    glm::vec4 reflectionParams{0.0f};

    // Kronos ("Rendering Fidelity" -- full atmospheric-scattering skybox):
    // real single-scattering Rayleigh+Mie atmosphere, see
    // Renderer::setAtmosphereScatteringEnabled()'s own comment and
    // shaders/sky.frag's own computeAtmosphere(). x: 1.0 real-enables it
    // this frame, 0.0 otherwise (the real, honest default -- every
    // existing scene/map/trailer beat that never calls
    // setAtmosphereScatteringEnabled(true) renders its own existing
    // skyZenithColor/skyHorizonColor two-tone gradient exactly as before,
    // untouched). y: real sun radiance multiplier (higher = brighter,
    // more saturated midday blue / sunset orange). z: real Mie-strength
    // multiplier (higher = hazier, more sun-aureole glow, like real
    // atmospheric turbidity). w: unused padding.
    glm::vec4 atmosphereParams{0.0f};

    // Kronos ("Rendering Fidelity" -- volumetric cloud layer): real 3D
    // value-noise fbm clouds, raymarched through a flat altitude shell
    // entirely inside shaders/sky.frag's own existing background pass
    // (see Renderer::setCloudsEnabled()'s own comment). x: 1.0
    // real-enables it this frame, 0.0 otherwise (the real, honest
    // default). y: real cloud coverage, 0..1 (higher = more overcast).
    // z: real wind scroll speed. w: real total elapsed seconds (drives
    // the wind scroll -- same clock Renderer::drawCinematicPass()'s own
    // CinematicPushConstants::time already uses, just delivered through
    // this UBO instead of a push constant since sky.frag's pipeline has
    // none).
    glm::vec4 cloudParams{0.0f};

    // Kronos ("Rendering Fidelity" -- ray-traced bounce lighting/GI): real
    // single-bounce indirect diffuse -- see Renderer::setRTGIEnabled()'s
    // own comment and shaders/scene_rt.frag's own traceIndirectDiffuse().
    // x: 1.0 real-enables it this frame (both the user's own toggle AND a
    // real, currently-valid TLAS -- same "never just the raw toggle"
    // reasoning renderFlags.x/reflectionParams.x already document), 0.0
    // otherwise (the real, honest default -- RT-only, same as reflections;
    // non-RT devices, running scene.frag, never had this term at all).
    // y: real intensity multiplier on the indirect contribution. z/w:
    // unused padding.
    glm::vec4 giParams{0.0f};
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

    // Sprint 16 ("Cinematic Graphics") point lights -- key/rim/fill per
    // scene, see kMaxPointLights's comment. A plain std::vector here (not
    // a fixed array) so callers can push_back() naturally; entries beyond
    // kMaxPointLights are silently dropped (a real, logged-nowhere but
    // harmless cap, not a crash) by Renderer::drawSceneIntoImpl() when it
    // copies this into SceneUBO's fixed-size arrays.
    struct PointLight {
        glm::vec3 position{0.0f};
        float radius = 10.0f;
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
    };
    std::vector<PointLight> pointLights;
};

} // namespace engine::core
