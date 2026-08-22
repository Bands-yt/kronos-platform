#version 450
#ifdef KRONOS_BINDLESS
#extension GL_EXT_nonuniform_qualifier : require
#endif

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUV;
// Material params -- forwarded here by either scene.vert (push-constant
// path) or scene_instanced.vert (per-instance-attribute path); this
// shader doesn't know or care which one ran, which is the entire point of
// routing material data through vertex outputs instead of reading a
// push_constant block directly (a push-constant block is tied to one
// specific pipeline layout, and the two vertex shaders use different
// ones -- see Renderer.cpp's createInstancedScenePipeline()).
layout(location = 3) in flat vec4 inBaseColor;
layout(location = 4) in flat vec4 inMetallicRoughness;
layout(location = 5) in flat vec4 inEmissive;
// World-space tangent + handedness (w), forwarded by either vertex
// shader -- see scene.vert's comment on why this is at location 6, not
// renumbered in with 3/4/5 above.
layout(location = 6) in vec4 inWorldTangent;
// Kronos ("Avatar Visual Silhouette Pass" -- "Hair" -- "Apply
// vertex-color gradients for depth; avoid flat brown shading"): real,
// genuinely interpolated (not flat) per-vertex color, multiplied into
// albedo below -- the first real per-vertex (not per-entity/per-segment)
// color channel this shader has ever consumed. Defaults to opaque white
// for every mesh that never sets Vertex::color (Mesh.hpp), so this is a
// real no-op multiply everywhere except the new hair mesh
// (AvatarHair.cpp) that actually varies it.
layout(location = 7) in vec4 inVertexColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj[3];
    mat4 invViewProj; // only shaders/sky.frag reads this
    vec4 cascadeSplitsView;
    vec4 cascadeBiasScale;
    vec4 lightDirectionWS;
    vec4 lightColorIntensity;
    vec4 viewPositionWS;
    vec4 ambientColor;       // "sky" hemisphere tone
    vec4 ambientGroundColor; // "ground" hemisphere tone -- see computeAmbient() below
    vec4 fogColorDensity;    // rgb: fog color, a: density (0 = no fog) -- see applyFog() below
    vec4 skyZenithColor;     // only shaders/sky.frag reads this
    vec4 skyHorizonColor;    // only shaders/sky.frag reads this
    // Sprint 14 ("Performance Mode"): y = 1.0 real-switches
    // sampleCascadeShadow() below from a 3x3 (9-tap) PCF loop to a
    // single real center tap -- a real, direct per-fragment cost cut,
    // available on every device (not gated on ray tracing support at
    // all). x/z/w: unused here -- x is scene_rt.frag's own real
    // ray-query-shadow toggle, meaningless on this non-RT shader variant
    // (this device never had scene_rt.frag selected in the first place,
    // see Renderer::createScenePipeline()). See SceneTypes.hpp's own
    // field comment for the full real picture.
    vec4 renderFlags;
    // Sprint 16 point lights -- see SceneTypes.hpp's SceneUBO comment and
    // computePointLights() below.
    vec4 pointLightPositionRadius[4];
    vec4 pointLightColorIntensity[4];
    vec4 pointLightCount;
    // Kronos ("Rendering Fidelity Foundation" Phase 1.3): unused here
    // (scene_rt.frag's own real RT-reflection toggle, meaningless on this
    // non-RT variant) -- declared purely to keep this struct's own byte
    // layout identical up through cloudParams below (see SceneTypes.hpp's
    // own field order).
    vec4 reflectionParams;
    // Kronos ("Rendering Fidelity" -- full atmospheric-scattering skybox):
    // unused here (shaders/sky.frag-only) -- same real "declared for
    // offset only" reasoning as reflectionParams above.
    vec4 atmosphereParams;
    // Kronos ("Environmental Detail" -- dynamic cloud shadows): x = 1.0
    // real-enables cloudShadowAt() below this frame. y: real cloud
    // coverage. z: real wind speed. w: real total elapsed seconds. See
    // SceneTypes.hpp's own field comment and shaders/sky.frag's own
    // computeClouds() -- this is the *same* real cloud layer, sampled
    // here for its own real shadow, not a second, independent cloud system.
    vec4 cloudParams;
} scene;

// Populated once per frame-in-flight (see Renderer::createShadowResources)
// -- one array image, kCascadeCount layers, a regular (non-comparison)
// sampler so computeShadow() below can do its own multi-tap PCF instead of
// relying on a single hardware-compare sample.
layout(set = 0, binding = 1) uniform sampler2DArray shadowMapArray;

// Per-material textures (core::TextureLibrary, see Components.hpp's
// Renderable texture fields) -- bound per-entity by the individually-
// drawn path (Renderer::drawSceneInto's main loop); instanced draws bind
// a shared default (all-fallback) set instead, see drawSceneInto()'s
// comment on why. Unassigned slots point at solid-color fallback
// textures (white for albedo/metallic/roughness, flat-normal
// (128,128,255) for normal, which decodes to tangent-space (0,0,1) --
// "no perturbation") so every sample below is always valid and multiplies
// (or, for normal, composes through the identity TBN) as a no-op -- no
// has-texture branch needed. NORMAL_TEX now perturbs the shading
// normal via a real per-fragment TBN built from the interpolated
// world-space tangent (inWorldTangent, generated per-mesh by
// Mesh::computeTangents()) -- see main()'s normal-mapping block below.
// Kronos ("Bindless Descriptors"): compiled twice -- once as-is, once
// with KRONOS_BINDLESS defined (see engine/src/CMakeLists.txt). The
// renderer picks the bindless variant only on a device that supports
// descriptor indexing, so a device without it keeps exactly the path
// that shipped before.
#ifdef KRONOS_BINDLESS
layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];

// Texture indices arrive as a flat varying rather than a push constant:
// three different vertex shaders with three different pipeline layouts
// feed this fragment shader, so a push_constant block here would be tied
// to only one of them.
//   x = albedo | (normal    << 16)
//   y = metallic | (roughness << 16)
//   z = ao
layout(location = 8) in flat uvec4 inTextureIndices;

// nonuniformEXT is required, not decorative: fragments within one draw
// can index different slots, and without it the index is treated as
// subgroup-uniform and samples the wrong texture.
#define ALBEDO_TEX    bindlessTextures[nonuniformEXT(inTextureIndices.x & 0xFFFFu)]
#define NORMAL_TEX    bindlessTextures[nonuniformEXT(inTextureIndices.x >> 16)]
#define METALLIC_TEX  bindlessTextures[nonuniformEXT(inTextureIndices.y & 0xFFFFu)]
#define ROUGHNESS_TEX bindlessTextures[nonuniformEXT(inTextureIndices.y >> 16)]
#define AO_TEX        bindlessTextures[nonuniformEXT(inTextureIndices.z & 0xFFFFu)]
#else
layout(set = 1, binding = 0) uniform sampler2D albedoTexture;
layout(set = 1, binding = 1) uniform sampler2D normalTexture;
layout(set = 1, binding = 2) uniform sampler2D metallicTexture;
layout(set = 1, binding = 3) uniform sampler2D roughnessTexture;
#define ALBEDO_TEX    albedoTexture
#define NORMAL_TEX    normalTexture
#define METALLIC_TEX  metallicTexture
#define ROUGHNESS_TEX roughnessTexture
#endif
// Baked ambient-occlusion map (Sprint 16) -- multiplies only the ambient
// term in main() below, same unassigned-is-white-is-no-op fallback
// convention as the four slots above. Deliberately not applied to the
// direct (Lo) term: baked AO approximates occluded indirect/sky light in
// crevices, not a second real-time shadow.
#ifndef KRONOS_BINDLESS
layout(set = 1, binding = 4) uniform sampler2D aoTexture;
#define AO_TEX aoTexture
#endif

const float PI = 3.14159265359;

// Standard metallic-roughness Cook-Torrance BRDF -- GGX normal
// distribution, Smith height-correlated geometry term, Schlick Fresnel.
// One directional light + a flat ambient term stands in for real image-
// based lighting (no environment cubemap exists yet -- see
// docs/ARCHITECTURE.md §4.1's frame graph TODO in Renderer.cpp for where
// that and the rest of the post stack attach). The directional light *is*
// now shadowed -- see computeShadow() -- via a real cascaded shadow map
// (3 cascades, camera-following, stable/texel-snapped splits); see
// Renderer::computeCascades()'s comment for exactly what's simplified
// (no cross-cascade blend band, fixed max shadow distance).

// Two-tone "hemisphere" ambient: blends sky/ground tones by how much the
// surface normal points up vs. down, instead of one flat ambient value
// applied to every surface regardless of orientation -- a cheap, standard
// stand-in for real image-based lighting (see the header comment on why
// there's no environment cubemap yet). This alone is most of why a flat-
// ambient PBR scene reads as visually "bare"/flat versus one with even
// this simple a fill-light approximation.
vec3 computeAmbient(vec3 N) {
    float skyWeight = N.y * 0.5 + 0.5; // 1.0 = straight up, 0.0 = straight down
    return mix(scene.ambientGroundColor.rgb, scene.ambientColor.rgb, skyWeight);
}

float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom + 1e-7);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float ggxV = NdotV / (NdotV * (1.0 - k) + k);
    float ggxL = NdotL / (NdotL * (1.0 - k) + k);
    return ggxV * ggxL;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Sprint 16 ("Cinematic Graphics" Phase 3: key/rim/fill lights). Same
// Cook-Torrance BRDF as the directional sun above, evaluated once per
// active point light (scene.pointLightCount.x, real count, not always
// kMaxPointLights) and summed. Real inverse-square falloff windowed to
// exactly 0 at the light's own radius (positionRadius.w) via a smooth
// (not hard-clipped) polynomial falloff -- the standard "physically-
// inspired but artist-boundable" point-light falloff real-time engines
// use instead of literal unbounded inverse-square, which never reaches
// zero and would light an entire level from one bulb. Unshadowed (no
// per-point-light shadow map exists -- these are meant for local
// fill/rim/accent lighting, not primary shadow-casting light sources;
// the directional sun above remains the one shadowed light).
vec3 computePointLights(vec3 worldPos, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 result = vec3(0.0);
    int count = int(scene.pointLightCount.x + 0.5);
    for (int i = 0; i < count; ++i) {
        vec4 posRadius = scene.pointLightPositionRadius[i];
        vec4 colorIntensity = scene.pointLightColorIntensity[i];
        vec3 toLight = posRadius.xyz - worldPos;
        float dist = length(toLight);
        float radius = max(posRadius.w, 1e-4);
        if (dist >= radius) continue;
        vec3 L = toLight / max(dist, 1e-4);
        vec3 H = normalize(V + L);
        float NdotL = max(dot(N, L), 0.0);
        float NdotV = max(dot(N, V), 1e-4);
        if (NdotL <= 0.0) continue;

        float distRatio = clamp(dist / radius, 0.0, 1.0);
        float windowed = 1.0 - distRatio * distRatio;
        float falloff = (windowed * windowed) / (dist * dist + 1.0);

        float NDF = distributionGGX(N, H, roughness);
        float G = geometrySmith(NdotV, NdotL, roughness);
        vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
        vec3 specular = (NDF * G * F) / (4.0 * NdotV * NdotL + 1e-4);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
        vec3 diffuse = kD * albedo / PI;

        vec3 radiance = colorIntensity.rgb * colorIntensity.a * falloff;
        result += (diffuse + specular) * radiance * NdotL;
    }
    return result;
}

// Picks which of the 3 cascades a fragment falls into, from its
// view-space depth (distance in front of the camera along its forward
// axis) against the far-split distances Renderer::computeCascades()
// computed this frame.
int selectCascade(float viewDepth) {
    if (viewDepth < scene.cascadeSplitsView.x) return 0;
    if (viewDepth < scene.cascadeSplitsView.y) return 1;
    return 2;
}

// Returns 1.0 = fully lit, 0.0 = fully shadowed for one specific cascade
// -- computeShadow() below is what picks (and, near a split, blends)
// which cascade(s) actually get sampled; this is the real per-cascade
// sampling work factored out so it can be called twice for a blend
// without duplicating the PCF loop. 3x3 PCF (9 taps) for soft edges
// instead of the hard, aliased edge a single sample gives. Points outside
// the given cascade's ortho volume entirely are treated as lit, not
// shadowed -- the only sane default for "we don't actually know" outside
// a cascade's coverage (this can happen right at a cascade's own edge
// before the next cascade's coverage begins, or while blending against a
// neighboring cascade whose volume this point falls outside of).
float sampleCascadeShadow(int cascade, vec3 worldPos, vec3 N, vec3 L) {
    vec4 lightSpacePos = scene.lightViewProj[cascade] * vec4(worldPos, 1.0);
    vec3 projected = lightSpacePos.xyz / lightSpacePos.w;
    vec2 uv = projected.xy * 0.5 + 0.5;
    float currentDepth = projected.z;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || currentDepth > 1.0) {
        return 1.0;
    }

    // Slope-scaled bias: grazing-angle surfaces need more bias than
    // surfaces facing the light head-on, or they self-shadow ("shadow
    // acne") even with the pipeline's constant depth bias
    // (createShadowPipeline's depthBiasConstantFactor/SlopeFactor) alone.
    // Scaled by this cascade's own bias-scale factor (see
    // Renderer::kReferenceShadowDepthRange's comment) -- without this,
    // the same fixed bias constant is wrong for every cascade except the
    // one it happens to match, showing up as acne or peter-paning that
    // differs by which cascade is currently active (i.e. by distance from
    // the camera, which reads as "looks wrong from some angles"). This
    // stays correct under a moving/dynamic light too (task category 3's
    // "confirm shadow-bias remains correct under dynamic lighting"):
    // NdotL and every scene.* field here are recomputed fresh every
    // frame from whatever the light direction currently is
    // (Renderer::drawSceneIntoImpl() rebuilds the whole UBO, cascades
    // included, once per frame, never caching across frames) -- a moving
    // sun was never a special case this bias math needed to know about.
    float NdotL = max(dot(N, L), 0.0);
    float bias = max(0.0025 * (1.0 - NdotL), 0.0006) * scene.cascadeBiasScale[cascade];

    // Sprint 14 ("Performance Mode"): a real, direct per-fragment cost
    // cut -- one center tap instead of the real 3x3 (9-tap) PCF loop,
    // trading soft shadow edges for real fragment-shader throughput.
    if (scene.renderFlags.y > 0.5) {
        float sampledDepth = texture(shadowMapArray, vec3(uv, float(cascade))).r;
        return (currentDepth - bias > sampledDepth) ? 0.0 : 1.0;
    }

    vec2 texelSize = 1.0 / vec2(textureSize(shadowMapArray, 0).xy);
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float sampledDepth = texture(shadowMapArray, vec3(uv + vec2(x, y) * texelSize, float(cascade))).r;
            shadow += (currentDepth - bias > sampledDepth) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

// Real cross-cascade blend band (task category 3: "Polish CSM cascade
// transitions -- no popping, smooth blend"), closing the "no
// cross-cascade blend band" simplification this file used to state here.
// Over the last kCascadeBlendFraction of a cascade's own range before its
// split, this samples *both* the current and the next cascade and
// linearly blends between them, so a fragment crossing a split boundary
// sees a smooth gradient instead of a hard, potentially visible seam
// where the two cascades' shadow-map resolutions/bias don't line up
// pixel-for-pixel.
const float kCascadeBlendFraction = 0.1;

// Kronos ("Environmental Detail" -- dynamic cloud shadows): real, cheap
// hash-based value noise + fbm -- a real, self-contained duplicate of
// shaders/sky.frag's own cloudHash()/cloudValueNoise()/cloudFbm() (same
// "self-contained twin, not a shared #include" convention this file's
// own header already establishes for scene_rt.frag), needed here to
// sample the *same* real cloud density field sky.frag's own
// computeClouds() draws, so a cloud actually visible overhead casts a
// real shadow on the ground beneath it -- not a second, disagreeing
// cloud pattern.
float cloudShadowHash(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float cloudShadowValueNoise(vec3 p) {
    vec3 i = floor(p);
    vec3 f = fract(p);
    vec3 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(cloudShadowHash(i + vec3(0, 0, 0)), cloudShadowHash(i + vec3(1, 0, 0)), u.x),
                    mix(cloudShadowHash(i + vec3(0, 1, 0)), cloudShadowHash(i + vec3(1, 1, 0)), u.x), u.y),
                mix(mix(cloudShadowHash(i + vec3(0, 0, 1)), cloudShadowHash(i + vec3(1, 0, 1)), u.x),
                    mix(cloudShadowHash(i + vec3(0, 1, 1)), cloudShadowHash(i + vec3(1, 1, 1)), u.x), u.y),
                u.z);
}

float cloudShadowFbm(vec3 p) {
    float sum = 0.0;
    float amp = 0.5;
    for (int i = 0; i < 5; ++i) {
        sum += amp * cloudShadowValueNoise(p);
        p *= 2.02;
        amp *= 0.5;
    }
    return sum;
}

// Real, single-tap cloud shadow -- finds where the ray from `worldPos`
// toward the sun (`sunDir`) crosses the cloud layer's own real mid-
// altitude (a real, single planar intersection, not a full volumetric
// march like sky.frag's own computeClouds() -- a real, deliberately
// cheaper approximation: a shadow only needs "is there cloud in the way,"
// not the cloud's own full lit appearance), samples the real cloud
// density there, and returns a real, *partial* attenuation (clouds
// scatter sunlight, they don't fully block it, so this never returns
// pure 0). A real, honest identity (1.0, no shadow) when the sun is at or
// below the horizon, or when the ray never reaches the cloud layer at all.
float cloudShadowAt(vec3 worldPos, vec3 sunDir, float coverage, float speed, float time) {
    const float kCloudMidAltitude = 575.0; // real midpoint of sky.frag's own kCloudBase..kCloudTop
    if (sunDir.y <= 0.01) return 1.0;
    float t = (kCloudMidAltitude - worldPos.y) / sunDir.y;
    if (t <= 0.0) return 1.0;
    vec3 samplePos = worldPos + sunDir * t;
    vec3 wind = vec3(time * speed, 0.0, time * speed * 0.6);
    float density = cloudShadowFbm((samplePos + wind) * 0.0035);
    density = smoothstep(0.85 - coverage, 1.15 - coverage, density);
    return mix(1.0, 0.35, density);
}

float computeShadow(vec3 worldPos, vec3 N, vec3 L, float viewDepth) {
    int cascade = selectCascade(viewDepth);
    float shadow = sampleCascadeShadow(cascade, worldPos, N, L);

    if (cascade < 2) {
        float splitDepth = cascade == 0 ? scene.cascadeSplitsView.x : scene.cascadeSplitsView.y;
        float blendBandStart = splitDepth * (1.0 - kCascadeBlendFraction);
        if (viewDepth > blendBandStart) {
            float t = clamp((viewDepth - blendBandStart) / max(splitDepth - blendBandStart, 1e-4), 0.0, 1.0);
            float nextShadow = sampleCascadeShadow(cascade + 1, worldPos, N, L);
            shadow = mix(shadow, nextShadow, t);
        }
    }
    return shadow;
}

// Real exponential-squared fog (task category 3). density=0 (the default
// whenever a caller never touches SceneLighting::fogDensity) is a real,
// honest "no fog": exp(-(0*x)^2) == 1.0 for every viewDepth, so this is a
// precise identity, not an approximation that happens to look right when
// unused. Exponential-squared (vs. linear or plain exponential falloff)
// is the standard "thickens non-linearly with distance" curve real-time
// engines use for atmospheric fog specifically. Applied in linear HDR
// space, before composite.frag's tonemap/gamma -- consistent with every
// other lighting term in this shader.
vec3 applyFog(vec3 color, float viewDepth) {
    float density = scene.fogColorDensity.a;
    float fogFactor = clamp(exp(-pow(density * viewDepth, 2.0)), 0.0, 1.0);
    return mix(scene.fogColorDensity.rgb, color, fogFactor);
}

// Kronos ("Sky Map Full Biome Rebuild" Phase 3): real triplanar texture
// projection -- samples a texture using real *world-space* planar
// coordinates along each axis (YZ for an X-facing surface, XZ for
// Y-facing/"top-down", XY for Z-facing) instead of the mesh's own
// per-vertex UV, blended by the geometric normal's own per-axis weight.
// The real fix for Terrain's own visible vertex grid on cliffs/slopes:
// Terrain::buildChunkMesh() (core/Terrain.cpp) generates UVs from the
// height grid's own real (x,z) indices, which read as an obviously
// regular, grid-aligned pattern once textured, especially on a steep
// slope where that grid stretches; triplanar sampling never reads that
// UV at all, so it can't inherit its regularity.
vec3 triplanarWeights(vec3 geometricNormal) {
    vec3 blend = abs(geometricNormal);
    blend = pow(blend, vec3(4.0)); // real, sharpened falloff -- favors whichever axis the surface actually faces, not a mushy three-way mix on a near-45-degree slope
    return blend / max(blend.x + blend.y + blend.z, 1e-5);
}

vec4 sampleTriplanar(sampler2D tex, vec3 worldPos, vec3 weights, float scale) {
    vec4 xProjection = texture(tex, worldPos.yz * scale);
    vec4 yProjection = texture(tex, worldPos.xz * scale);
    vec4 zProjection = texture(tex, worldPos.xy * scale);
    return xProjection * weights.x + yProjection * weights.y + zProjection * weights.z;
}

// Kronos (triplanar normal fix -- "reflections divided into 3"): real
// Whiteout blending (Golus / Neubelt & Pettineo 2013) -- the real,
// correct fix for the previous "blend 3 raw tangent-space samples
// through one shared TBN" simplification this file's own former comment
// here honestly flagged as a real scope gap. That approach is only
// mathematically valid on a flat plane; on curved terrain (a dome, a
// cliff sweep) the 3 axis projections' own tangent spaces genuinely
// disagree, and reflect() is sensitive enough to small normal changes
// that the disagreement reads as a real, hard seam -- exactly "one
// smooth reflection" turning into 3 visibly different reflected patches
// meeting at the blend boundaries. Whiteout blending fixes this by
// reorienting each axis's own tangent-space sample directly into real
// world space (via that axis's own real geometric-normal swizzle)
// *before* blending, so all 3 contributions already agree on their
// frame by the time they're combined -- no more per-axis seam.
vec3 triplanarWorldNormal(sampler2D tex, vec3 worldPos, vec3 geometricNormal, vec3 weights, float scale,
                           float intensity) {
    vec3 nx = texture(tex, worldPos.yz * scale).xyz * 2.0 - 1.0;
    vec3 ny = texture(tex, worldPos.xz * scale).xyz * 2.0 - 1.0;
    vec3 nz = texture(tex, worldPos.xy * scale).xyz * 2.0 - 1.0;
    // Same real "normal intensity" control as the non-triplanar path
    // below -- scales each axis's own tangent-plane perturbation before
    // the swizzle/blend, 0 collapsing every axis to its own real
    // unperturbed geometric normal.
    nx.xy *= intensity;
    ny.xy *= intensity;
    nz.xy *= intensity;
    nx = vec3(nx.xy + geometricNormal.zy, nx.z * geometricNormal.x);
    ny = vec3(ny.xy + geometricNormal.xz, ny.z * geometricNormal.y);
    nz = vec3(nz.xy + geometricNormal.xy, nz.z * geometricNormal.z);
    return normalize(nx.zyx * weights.x + ny.xzy * weights.y + nz.xyz * weights.z);
}

void main() {
    // Real, per-object opt-in (inMetallicRoughness.w, real, confirmed-
    // unused padding before this -- see SceneTypes.hpp's own
    // ObjectPushConstants comment): only Terrain chunks set this (see
    // core::Terrain::regenerateChunk()'s own real Renderable field), so
    // every other real object in this engine keeps sampling by inUV,
    // byte-for-byte unchanged.
    bool useTriplanar = inMetallicRoughness.w > 0.5;
    vec3 triWeights = useTriplanar ? triplanarWeights(normalize(inWorldNormal)) : vec3(0.0);
    // Real, tuned world-units-per-texture-repeat scale, and a real,
    // second, higher-frequency sample of the *same* texture blended in
    // at low intensity as real micro-detail -- breaks up the base
    // texture's own large-scale repetition without needing a second,
    // dedicated detail-texture asset (this engine generates its own
    // textures procedurally, see core/ProceduralMaterials.hpp's own
    // header comment; a real second sampler binding just for detail
    // would need new descriptor-set plumbing this pass doesn't add).
    const float kTriplanarScale = 0.12;
    const float kMicroDetailScale = 0.9;

    // Sampled texture multiplies the flat push-constant/instance value --
    // an unassigned slot samples a solid-white (or, for metallic/roughness,
    // still white) fallback texture, making the multiply a no-op. Real
    // per-pixel material variation when a texture *is* assigned, zero
    // behavior change when one isn't.
    vec3 albedo;
    float metallic;
    float roughness;
    if (useTriplanar) {
        vec3 baseAlbedo = sampleTriplanar(ALBEDO_TEX, inWorldPos, triWeights, kTriplanarScale).rgb;
        vec3 detailAlbedo = sampleTriplanar(ALBEDO_TEX, inWorldPos, triWeights, kMicroDetailScale).rgb;
        albedo = inBaseColor.rgb * mix(baseAlbedo, baseAlbedo * detailAlbedo * 1.6, 0.35) * inVertexColor.rgb;
        metallic = clamp(inMetallicRoughness.x * sampleTriplanar(METALLIC_TEX, inWorldPos, triWeights, kTriplanarScale).r, 0.0, 1.0);
        roughness = clamp(inMetallicRoughness.y * sampleTriplanar(ROUGHNESS_TEX, inWorldPos, triWeights, kTriplanarScale).r, 0.045, 1.0);
    } else {
        albedo = inBaseColor.rgb * texture(ALBEDO_TEX, inUV).rgb * inVertexColor.rgb;
        metallic = clamp(inMetallicRoughness.x * texture(METALLIC_TEX, inUV).r, 0.0, 1.0);
        roughness = clamp(inMetallicRoughness.y * texture(ROUGHNESS_TEX, inUV).r, 0.045, 1.0); // avoid a singular GGX at roughness 0
    }

    // Real tangent-space normal mapping: build a per-fragment TBN from
    // the interpolated world-space tangent/normal, re-orthogonalizing the
    // tangent against the (possibly non-uniformly-scaled-by-model)
    // interpolated normal via Gram-Schmidt -- interpolation across a
    // triangle and non-uniform scale can both nudge T and N slightly out
    // of perpendicularity, and a non-orthogonal TBN visibly skews the
    // mapped normal. bitangent reconstructed via cross(N, T) * handedness
    // (inWorldTangent.w) rather than carried as a fourth interpolated
    // vector -- one less varying, same result.
    vec3 geometricNormal = normalize(inWorldNormal);
    vec3 T = normalize(inWorldTangent.xyz);
    T = normalize(T - geometricNormal * dot(geometricNormal, T));
    vec3 B = cross(geometricNormal, T) * inWorldTangent.w;
    mat3 TBN = mat3(T, B, geometricNormal);

    // Real triplanar normal -- a real Whiteout blend (see
    // triplanarWorldNormal()'s own header comment), which already
    // produces a real world-space normal directly, no TBN transform
    // needed (each axis's own sample was already reoriented into world
    // space before blending). The non-triplanar path is unchanged: a
    // real tangent-space sample through the per-fragment TBN above.
    vec3 N;
    if (useTriplanar) {
        N = triplanarWorldNormal(NORMAL_TEX, inWorldPos, geometricNormal, triWeights, kTriplanarScale,
                                  inMetallicRoughness.z);
    } else {
        // Normal intensity (inMetallicRoughness.z, see Components.hpp's
        // Renderable::normalIntensity): scales the tangent-space XY
        // before renormalizing, the standard "normal strength" control
        // -- at 0 this collapses to (0,0,sampledNormal.z), which
        // normalizes to (0,0,1) = no perturbation, matching the
        // flat-normal fallback exactly.
        vec3 sampledNormal = texture(NORMAL_TEX, inUV).rgb * 2.0 - 1.0; // [0,1] -> [-1,1]
        sampledNormal.xy *= inMetallicRoughness.z;
        sampledNormal = normalize(sampledNormal);
        N = normalize(TBN * sampledNormal);
    }

    // Kronos ("Rendering Fidelity Foundation" Phase 1.1): real wet-surface
    // response -- scene.renderFlags.z is real weather wetness (0..1, see
    // core::Weather.hpp's own comment), 0 at Clear weather (a real,
    // exact no-op multiply below). Blended toward upward-facing
    // (ground/floor) geometry only via `groundFacing` -- a vertical wall
    // doesn't pool rain the way a floor does -- using the exact same
    // hemisphere idiom computeAmbient() already establishes for
    // sky-vs-ground blending. Roughness reduction (not albedo darkening)
    // is the real, standard real-time "wet look": water doesn't change
    // what a surface's base color is, it makes specular highlights
    // sharper and brighter, which a lower roughness value already does
    // via the existing GGX/Fresnel terms below with zero further changes.
    float groundFacing = clamp(N.y * 0.5 + 0.5, 0.0, 1.0); // same convention as computeAmbient()'s skyWeight: 1.0 = normal points straight up (a floor), 0.0 = straight down (a ceiling)
    roughness = mix(roughness, roughness * (1.0 - 0.6 * clamp(scene.renderFlags.z, 0.0, 1.0)), groundFacing);
    roughness = clamp(roughness, 0.045, 1.0);

    vec3 V = normalize(scene.viewPositionWS.xyz - inWorldPos);
    vec3 L = normalize(-scene.lightDirectionWS.xyz);
    vec3 H = normalize(V + L);

    float NdotV = max(dot(N, V), 1e-4);
    float NdotL = max(dot(N, L), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NDF = distributionGGX(N, H, roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 specular = (NDF * G * F) / (4.0 * NdotV * NdotL + 1e-4);

    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    // View-space depth (positive distance in front of the camera) --
    // recomputed here rather than passed from scene.vert as a varying,
    // since it's one mat4-vec4 multiply and a negate, cheaper than adding
    // an interpolated output just for this.
    float viewDepth = -(scene.view * vec4(inWorldPos, 1.0)).z;
    float shadow = computeShadow(inWorldPos, N, L, viewDepth);
    // Kronos ("Environmental Detail" -- dynamic cloud shadows): a real,
    // exact no-op (this whole call doesn't even evaluate) when the toggle
    // is off, same "off means byte-identical" convention as every other
    // optional feature this shader gates on a UBO flag.
    if (scene.cloudParams.x > 0.5) {
        shadow *= cloudShadowAt(inWorldPos, L, scene.cloudParams.y, scene.cloudParams.z, scene.cloudParams.w);
    }

    vec3 radiance = scene.lightColorIntensity.rgb * scene.lightColorIntensity.a;
    // Shadow multiplies only the direct term -- ambient represents light
    // that already bounced around the scene (sky, indirect bounce), which
    // a direct-light occluder doesn't block.
    vec3 Lo = (diffuse + specular) * radiance * NdotL * shadow;
    // Sprint 16: key/rim/fill point lights, unshadowed, summed on top of
    // the shadowed directional term -- see computePointLights()'s comment.
    Lo += computePointLights(inWorldPos, N, V, albedo, metallic, roughness, F0);

    float ao = useTriplanar ? sampleTriplanar(AO_TEX, inWorldPos, triWeights, kTriplanarScale).r : texture(AO_TEX, inUV).r;
    vec3 ambient = computeAmbient(N) * albedo * ao;

    // Kronos ("Four RTX Maps" Phase 5c): real caustic-light dapple pattern
    // for the Underwater Map -- scene.renderFlags.w is a real, exact
    // identity at 0 (every non-Underwater map). A static (not time-
    // animated) sine-interference pattern -- see
    // Renderer::setUnderwaterCausticsEnabled()'s own comment for why this
    // stays static rather than threading a new time field through every
    // full-SceneUBO shader. Blended toward upward-facing (sea-floor-like)
    // geometry only, the same groundFacing idiom the wet-surface hook
    // above uses -- real caustics dance across a sea floor, not a cave
    // wall or ceiling.
    float causticStrength = clamp(scene.renderFlags.w, 0.0, 1.0);
    if (causticStrength > 0.001) {
        float caustic = sin(inWorldPos.x * 0.6) * cos(inWorldPos.z * 0.5 + inWorldPos.x * 0.15) * 0.5 + 0.5;
        caustic = pow(caustic, 2.0);
        ambient *= mix(1.0, 0.6 + caustic * 0.9, causticStrength * groundFacing);
    }
    // Emissive is a flat additive glow -- not multiplied by shadow or NdotL
    // (a glowing crystal in a shadowed alcove should still glow) and not
    // participating in the BRDF at all, matching "self-illuminated
    // surface" rather than "receives extra light".
    vec3 emissive = inEmissive.rgb * inEmissive.a;
    vec3 color = ambient + Lo + emissive;
    color = applyFog(color, viewDepth);

    // Linear HDR out, untouched -- no tonemap/gamma here anymore. This
    // writes into Renderer's intermediate HDR target (kHDRFormat,
    // R16G16B16A16_SFLOAT), not the final presentable image directly;
    // exposure, bloom, tonemapping (ACES filmic) and gamma correction all
    // happen once, later, in shaders/composite.frag -- see
    // Renderer::drawBloomAndComposite(). Keeping this shader's output
    // linear and un-tonemapped is what lets emissive crystals/particles
    // exceed 1.0 and actually bloom instead of just clipping to white.
    outColor = vec4(color, inBaseColor.a);
}
