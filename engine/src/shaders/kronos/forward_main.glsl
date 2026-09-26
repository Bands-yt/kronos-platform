// Shared forward-shading fragment body for scene.frag and scene_rt.frag.
// The including file sets #version/#extensions and, for the ray-traced
// variant, defines KRONOS_RAY_TRACING first.
//
// Lighting model: sun (cascaded + cloud shadows) and clustered punctual
// lights through the layered BRDF in material.glsl, plus split-sum IBL
// with multiple-scattering compensation. Outputs linear HDR and
// screen-space motion (for TAA) in UV units.

#include "scene_resources.glsl"
#include "material.glsl"
#include "shadows.glsl"
#include "sky.glsl"
#include "ibl.glsl"
#include "lights.glsl"

layout(location = 0) in vec3 inWorldPos;
layout(location = 1) in vec3 inWorldNormal;
layout(location = 2) in vec2 inUV;
// Material params come through varyings, not push constants, because
// three vertex shaders with different pipeline layouts feed this body.
layout(location = 3) in flat vec4 inBaseColor;
layout(location = 4) in flat vec4 inMetallicRoughness; // x metal, y rough, z normal intensity, w triplanar
layout(location = 5) in flat vec4 inEmissive;
layout(location = 6) in vec4 inWorldTangent;
layout(location = 7) in vec4 inVertexColor;
//   x = albedo | normal << 16, y = metallic | roughness << 16,
//   z = ao | objectRecord << 16, w = unlit silhouette flag
layout(location = 8) in flat uvec4 inTextureIndices;
layout(location = 9) in vec4 inClipPos;     // unjittered current clip position
layout(location = 10) in vec4 inPrevClipPos; // unjittered previous-frame clip position

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec2 outVelocity;

#ifdef KRONOS_BINDLESS
layout(set = 2, binding = 0) uniform sampler2D bindlessTextures[];
// nonuniformEXT is required: fragments of one draw can index different slots.
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
layout(set = 1, binding = 4) uniform sampler2D aoTexture;
#define ALBEDO_TEX    albedoTexture
#define NORMAL_TEX    normalTexture
#define METALLIC_TEX  metallicTexture
#define ROUGHNESS_TEX roughnessTexture
#define AO_TEX        aoTexture
#endif

#ifdef KRONOS_RAY_TRACING
layout(set = 0, binding = 2) uniform accelerationStructureEXT topLevelAS;
// Two vec4 per TLAS instance: baseColor, (metallic, roughness, 0, 0).
layout(set = 0, binding = 3) readonly buffer InstanceMaterials {
    vec4 data[];
} instanceMaterials;
#endif

const float kTriplanarScale = 0.12;
const float kMicroDetailScale = 0.9;

vec3 triplanarWeights(vec3 geometricNormal) {
    vec3 blend = pow(abs(geometricNormal), vec3(4.0));
    return blend / max(blend.x + blend.y + blend.z, 1e-5);
}

vec4 sampleTriplanar(sampler2D tex, vec3 worldPos, vec3 weights, float scale) {
    return texture(tex, worldPos.yz * scale) * weights.x + texture(tex, worldPos.xz * scale) * weights.y +
           texture(tex, worldPos.xy * scale) * weights.z;
}

// Whiteout blend (Golus): each projection's tangent-space normal is
// reoriented into world space before blending, so the three axes agree.
vec3 triplanarWorldNormal(sampler2D tex, vec3 worldPos, vec3 Ng, vec3 weights, float scale, float intensity) {
    vec3 nx = texture(tex, worldPos.yz * scale).xyz * 2.0 - 1.0;
    vec3 ny = texture(tex, worldPos.xz * scale).xyz * 2.0 - 1.0;
    vec3 nz = texture(tex, worldPos.xy * scale).xyz * 2.0 - 1.0;
    nx.xy *= intensity;
    ny.xy *= intensity;
    nz.xy *= intensity;
    nx = vec3(nx.xy + Ng.zy, nx.z * Ng.x);
    ny = vec3(ny.xy + Ng.xz, ny.z * Ng.y);
    nz = vec3(nz.xy + Ng.xy, nz.z * Ng.z);
    return normalize(nx.zyx * weights.x + ny.xzy * weights.y + nz.xyz * weights.z);
}

vec3 applyFog(vec3 color, float viewDepth) {
    float density = scene.fogColorDensity.a;
    float fogFactor = clamp(exp(-pow(density * viewDepth, 2.0)), 0.0, 1.0);
    return mix(scene.fogColorDensity.rgb, color, fogFactor);
}

vec2 screenVelocity() {
    vec2 cur = inClipPos.xy / inClipPos.w;
    vec2 prev = inPrevClipPos.xy / inPrevClipPos.w;
    return (cur - prev) * 0.5;
}

// Per-pixel noise for stochastic filters; animated only when TAA is there
// to integrate it, otherwise a stable dither.
float shadingNoise() {
    vec2 pixel = gl_FragCoord.xy;
    if (scene.frameParams.y > 0.5) pixel += 5.588238 * mod(scene.frameParams.x, 64.0);
    return interleavedGradientNoise(pixel);
}

#ifdef KRONOS_RAY_TRACING
float rayQueryShadow(vec3 origin, vec3 direction) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, 0xFF, origin,
                          0.02, direction, 500.0);
    while (rayQueryProceedEXT(rq)) {
    }
    return rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT ? 1.0 : 0.0;
}

// Single-bounce hit shading: the hit's albedo under the sun (assuming an
// up-facing surface) plus hemisphere ambient. Misses return `missRadiance`.
vec3 traceRadiance(vec3 origin, vec3 direction, float tMax, vec3 missRadiance) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, topLevelAS, gl_RayFlagsOpaqueEXT, 0xFF, origin, 0.02, direction, tMax);
    while (rayQueryProceedEXT(rq)) {
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) {
        return missRadiance;
    }
    int hitIndex = rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true);
    vec3 hitAlbedo = instanceMaterials.data[hitIndex * 2].rgb;
    float sunUp = clamp(-scene.lightDirectionWS.y, 0.2, 1.0);
    vec3 sun = scene.lightColorIntensity.rgb * scene.lightColorIntensity.a * sunUp * INV_PI;
    return hitAlbedo * (sun + hemisphereAmbient(vec3(0.0, 1.0, 0.0)));
}

// Cosine-weighted one-bounce irradiance (radiance-equivalent, E / PI):
// with pdf = cos/PI the estimator reduces to the mean incoming radiance.
vec3 traceIndirectIrradiance(vec3 origin, vec3 N, float noise) {
    const int kSamples = 16;
    vec3 T, B;
    orthonormalBasis(N, T, B);
    vec3 accum = vec3(0.0);
    for (int i = 0; i < kSamples; ++i) {
        vec2 u = fract(hammersley(uint(i), uint(kSamples)) + vec2(noise, fract(noise * 7.13)));
        float r = sqrt(u.x);
        float phi = 2.0 * PI * u.y;
        vec3 dir = normalize(T * (r * cos(phi)) + B * (r * sin(phi)) + N * sqrt(max(0.0, 1.0 - u.x)));
        accum += traceRadiance(origin, dir, 60.0, hemisphereAmbient(dir));
    }
    return accum / float(kSamples);
}
#endif

float sunVisibility(vec3 Ngeo, vec3 L, float viewDepth, float noise) {
    float NoLgeo = saturate(dot(Ngeo, L));
    float visibility;
#ifdef KRONOS_RAY_TRACING
    if (scene.renderFlags.x > 0.5) {
        visibility = rayQueryShadow(inWorldPos + Ngeo * 0.02, L);
    } else
#endif
    {
        visibility = cascadedShadow(inWorldPos, Ngeo, NoLgeo, viewDepth, noise);
    }
    if (scene.cloudParams.x > 0.5) {
        visibility *= cloudShadowAt(inWorldPos, L, scene.cloudParams.y, scene.cloudParams.z, scene.cloudParams.w);
    }
    return visibility;
}

void main() {
    outVelocity = screenVelocity();

    if (inTextureIndices.w != 0u) {
        outColor = vec4(0.0, 0.0, 0.0, inBaseColor.a);
        return;
    }

    ObjectRecord record = objectRecords.records[objectRecordIndex(inTextureIndices)];

    vec3 Ngeo = normalize(inWorldNormal);
    bool useTriplanar = inMetallicRoughness.w > 0.5;
    vec3 triWeights = useTriplanar ? triplanarWeights(Ngeo) : vec3(0.0);

    vec3 albedo;
    float metallic;
    float perceptualRoughness;
    float ao;
    if (useTriplanar) {
        vec3 baseAlbedo = sampleTriplanar(ALBEDO_TEX, inWorldPos, triWeights, kTriplanarScale).rgb;
        vec3 detailAlbedo = sampleTriplanar(ALBEDO_TEX, inWorldPos, triWeights, kMicroDetailScale).rgb;
        albedo = inBaseColor.rgb * mix(baseAlbedo, baseAlbedo * detailAlbedo * 1.6, 0.35) * inVertexColor.rgb;
        metallic = inMetallicRoughness.x * sampleTriplanar(METALLIC_TEX, inWorldPos, triWeights, kTriplanarScale).r;
        perceptualRoughness =
            inMetallicRoughness.y * sampleTriplanar(ROUGHNESS_TEX, inWorldPos, triWeights, kTriplanarScale).r;
        ao = sampleTriplanar(AO_TEX, inWorldPos, triWeights, kTriplanarScale).r;
    } else {
        albedo = inBaseColor.rgb * texture(ALBEDO_TEX, inUV).rgb * inVertexColor.rgb;
        metallic = inMetallicRoughness.x * texture(METALLIC_TEX, inUV).r;
        perceptualRoughness = inMetallicRoughness.y * texture(ROUGHNESS_TEX, inUV).r;
        ao = texture(AO_TEX, inUV).r;
    }
    metallic = saturate(metallic);

    vec3 T = normalize(inWorldTangent.xyz - Ngeo * dot(Ngeo, inWorldTangent.xyz));
    vec3 B = cross(Ngeo, T) * inWorldTangent.w;
    vec3 N;
    if (useTriplanar) {
        N = triplanarWorldNormal(NORMAL_TEX, inWorldPos, Ngeo, triWeights, kTriplanarScale, inMetallicRoughness.z);
    } else {
        vec3 tn = texture(NORMAL_TEX, inUV).rgb * 2.0 - 1.0;
        tn.xy *= inMetallicRoughness.z;
        N = normalize(mat3(T, B, Ngeo) * normalize(tn));
    }

    // Rain wetness: water fills micro-surface detail on up-facing surfaces.
    float groundFacing = saturate(N.y * 0.5 + 0.5);
    perceptualRoughness *= mix(1.0, 1.0 - 0.6 * saturate(scene.renderFlags.z), groundFacing);

    ShadingContext s;
    s.worldPos = inWorldPos;
    s.N = N;
    s.Ngeo = Ngeo;
    s.V = normalize(scene.viewPositionWS.xyz - inWorldPos);
    // Back-facing normals (normal maps, interpolation at silhouettes) are
    // bent back toward the viewer rather than clamped, which avoids black rims.
    float rawNoV = dot(s.N, s.V);
    if (rawNoV < 0.0) s.N = normalize(s.N - s.V * (rawNoV - 1e-3));
    s.NoV = max(dot(s.N, s.V), 1e-4);
    s.pixel = gl_FragCoord.xy;

    PixelParams p;
    float reflectance = record.misc.x;
    p.diffuseColor = albedo * (1.0 - metallic);
    p.f0 = mix(vec3(0.16 * reflectance * reflectance), albedo, metallic);
    p.perceptualRoughness = specularAntiAliasing(N, perceptualRoughness);
    p.clearcoat = saturate(record.clearcoat.x);
    p.clearcoatPerceptualRoughness = specularAntiAliasing(Ngeo, record.clearcoat.y);
    p.sheenColor = record.sheen.rgb;
    p.sheenPerceptualRoughness = record.sheen.a;
    p.anisotropy = clamp(record.clearcoat.z, -1.0, 1.0);
    if (p.anisotropy != 0.0) {
        float c = cos(record.clearcoat.w);
        float sn = sin(record.clearcoat.w);
        p.anisotropicT = normalize(T * c + B * sn);
        p.anisotropicT = normalize(p.anisotropicT - N * dot(N, p.anisotropicT));
        p.anisotropicB = cross(N, p.anisotropicT);
    } else {
        p.anisotropicT = T;
        p.anisotropicB = B;
    }
    finalizePixelParams(p, s.NoV);

    float viewDepth = -(scene.view * vec4(inWorldPos, 1.0)).z;
    float noise = shadingNoise();

    vec3 L = normalize(-scene.lightDirectionWS.xyz);
    vec3 sunRadiance = scene.lightColorIntensity.rgb * scene.lightColorIntensity.a;
    vec3 color = vec3(0.0);
    if (dot(Ngeo, L) > -0.2 && dot(sunRadiance, sunRadiance) > 0.0) {
        float visibility = sunVisibility(Ngeo, L, viewDepth, noise);
        color += surfaceShading(s, p, L, sunRadiance, visibility);
    }
    color += evaluateClusteredLights(s, p, gl_FragCoord.xy, viewDepth, noise);

    vec3 R;
    vec3 specularRadiance = iblSpecularRadiance(s, p, R);
    vec3 irradiance = hemisphereAmbient(s.N);

    float causticStrength = saturate(scene.renderFlags.w);
    if (causticStrength > 0.001) {
        float caustic = sin(inWorldPos.x * 0.6) * cos(inWorldPos.z * 0.5 + inWorldPos.x * 0.15) * 0.5 + 0.5;
        irradiance *= mix(1.0, 0.6 + caustic * caustic * 0.9, causticStrength * groundFacing);
    }

#ifdef KRONOS_RAY_TRACING
    if (scene.reflectionParams.x > 0.5) {
        // Traced reflections replace the environment only where the lobe is
        // narrow enough for one ray to be a fair estimate.
        float rtWeight = smoothstep(scene.reflectionParams.y, scene.reflectionParams.z, 1.0 - p.perceptualRoughness);
        if (rtWeight > 0.001) {
            vec3 traced = traceRadiance(inWorldPos + Ngeo * 0.02, R, 200.0, specularRadiance);
            specularRadiance = mix(specularRadiance, traced, rtWeight);
        }
    }
    if (scene.giParams.x > 0.5) {
        irradiance += traceIndirectIrradiance(inWorldPos + Ngeo * 0.02, s.N, noise) * scene.giParams.y;
    }
#endif

    color += evaluateIBL(s, p, ao, irradiance, specularRadiance, R);
    color += inEmissive.rgb * inEmissive.a;
    color = applyFog(color, viewDepth);

    outColor = vec4(color, inBaseColor.a);
}
