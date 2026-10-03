#ifndef KRONOS_RAYTRACING_GLSL
#define KRONOS_RAYTRACING_GLSL

// Ray-query lighting for scene_rt.frag. Every visible mesh and skinned
// character is in the TLAS; each instance carries an RtInstance record
// (RayTracingScene.hpp) with its material and geometry buffer addresses,
// so hits get interpolated normals, UVs and textures. The including file
// enables GL_EXT_ray_query and GL_EXT_buffer_reference(_uvec2), and
// includes scene_resources, material, sky and ibl first.

layout(set = 0, binding = 2) uniform accelerationStructureEXT topLevelAS;

struct RtInstance {
    vec4 baseColor;
    vec4 surface;  // x metallic, y perceptual roughness
    vec4 emissive; // rgb radiance
    uvec4 geometry; // xy vertex buffer address, zw index buffer address
    uvec4 textures; // x albedo bindless slot
};

layout(std430, set = 0, binding = 3) readonly buffer RtInstances {
    RtInstance instances[];
} rtInstances;

layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RtVertices {
    float v[];
};
layout(buffer_reference, std430, buffer_reference_align = 4) readonly buffer RtIndices {
    uint i[];
};

const uint kRtMaskShadow = 0x01u;
const uint kRtMaskVisible = 0x02u;
const uint kRtVertexStride = 16u;
const float kRtFar = 2000.0;
const int kRtMaxHitLights = 32;

struct RtSurface {
    vec3 position;
    vec3 normal;
    vec3 albedo;
    float metallic;
    float perceptualRoughness;
    vec3 emissive;
    float t;
};

// Self-intersection offset that grows with distance from the camera (float precision).
vec3 rtOffset(vec3 position, vec3 normal) {
    float d = length(position - scene.viewPositionWS.xyz);
    return position + normal * (0.01 + 4e-4 * d);
}

bool rtOccluded(vec3 origin, vec3 direction, float tMax, uint mask) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, topLevelAS, gl_RayFlagsTerminateOnFirstHitEXT | gl_RayFlagsOpaqueEXT, mask, origin, 0.0,
                          direction, tMax);
    while (rayQueryProceedEXT(rq)) {
    }
    return rayQueryGetIntersectionTypeEXT(rq, true) != gl_RayQueryCommittedIntersectionNoneEXT;
}

vec3 rtVec3(RtVertices vb, uint base) {
    return vec3(vb.v[base], vb.v[base + 1u], vb.v[base + 2u]);
}

bool rtTraceSurface(vec3 origin, vec3 direction, float tMax, out RtSurface hit) {
    rayQueryEXT rq;
    rayQueryInitializeEXT(rq, topLevelAS, gl_RayFlagsOpaqueEXT, kRtMaskVisible, origin, 0.0, direction, tMax);
    while (rayQueryProceedEXT(rq)) {
    }
    if (rayQueryGetIntersectionTypeEXT(rq, true) == gl_RayQueryCommittedIntersectionNoneEXT) return false;

    RtInstance inst = rtInstances.instances[rayQueryGetIntersectionInstanceCustomIndexEXT(rq, true)];
    uint prim = uint(rayQueryGetIntersectionPrimitiveIndexEXT(rq, true));
    vec2 bary = rayQueryGetIntersectionBarycentricsEXT(rq, true);
    mat3 normalMatrix = transpose(mat3(rayQueryGetIntersectionWorldToObjectEXT(rq, true)));
    hit.t = rayQueryGetIntersectionTEXT(rq, true);
    hit.position = origin + direction * hit.t;

    RtIndices ib = RtIndices(inst.geometry.zw);
    RtVertices vb = RtVertices(inst.geometry.xy);
    uvec3 tri = uvec3(ib.i[prim * 3u], ib.i[prim * 3u + 1u], ib.i[prim * 3u + 2u]) * kRtVertexStride;
    vec3 w = vec3(1.0 - bary.x - bary.y, bary.x, bary.y);

    vec3 p0 = rtVec3(vb, tri.x);
    vec3 Ng = normalize(normalMatrix * cross(rtVec3(vb, tri.y) - p0, rtVec3(vb, tri.z) - p0));
    vec3 N = rtVec3(vb, tri.x + 3u) * w.x + rtVec3(vb, tri.y + 3u) * w.y + rtVec3(vb, tri.z + 3u) * w.z;
    N = normalize(normalMatrix * N);
    if (dot(Ng, direction) > 0.0) {
        Ng = -Ng;
        N = -N;
    }
    if (dot(N, Ng) < 0.0) N = Ng;
    hit.normal = N;

    vec4 vertexColor = vec4(vb.v[tri.x + 12u], vb.v[tri.x + 13u], vb.v[tri.x + 14u], vb.v[tri.x + 15u]) * w.x +
                       vec4(vb.v[tri.y + 12u], vb.v[tri.y + 13u], vb.v[tri.y + 14u], vb.v[tri.y + 15u]) * w.y +
                       vec4(vb.v[tri.z + 12u], vb.v[tri.z + 13u], vb.v[tri.z + 14u], vb.v[tri.z + 15u]) * w.z;
    vec3 albedo = inst.baseColor.rgb * vertexColor.rgb;
#ifdef KRONOS_BINDLESS
    if (inst.textures.x != 0u) {
        vec2 uv = vec2(vb.v[tri.x + 6u], vb.v[tri.x + 7u]) * w.x + vec2(vb.v[tri.y + 6u], vb.v[tri.y + 7u]) * w.y +
                  vec2(vb.v[tri.z + 6u], vb.v[tri.z + 7u]) * w.z;
        // Rays have no screen derivatives; a distance-based mip keeps distant hits from aliasing.
        float lod = clamp(log2(max(hit.t, 1e-3)) + 2.0, 0.0, 12.0);
        albedo *= textureLod(bindlessTextures[nonuniformEXT(inst.textures.x)], uv, lod).rgb;
    }
#endif
    hit.albedo = albedo;
    hit.metallic = inst.surface.x;
    hit.perceptualRoughness = clamp(inst.surface.y, kMinPerceptualRoughness, 1.0);
    hit.emissive = inst.emissive.rgb;
    return true;
}

// Sun visibility through a cone the size of the sun disk: one stochastic
// ray per pixel gives contact-hardening penumbrae once TAA integrates it.
float rtSunShadow(vec3 position, vec3 Ngeo, vec3 L, vec2 u) {
    vec3 T, B;
    orthonormalBasis(L, T, B);
    float r = sqrt(u.x) * scene.shadowParams.y;
    float phi = 2.0 * PI * u.y;
    vec3 dir = normalize(L + (T * cos(phi) + B * sin(phi)) * r);
    return rtOccluded(rtOffset(position, Ngeo), dir, kRtFar, kRtMaskShadow) ? 0.0 : 1.0;
}

vec3 rtLocalLights(RtSurface h, vec3 diffuse) {
    vec3 result = vec3(0.0);
    int count = min(int(scene.clusterParams.w), kRtMaxHitLights);
    for (int i = 0; i < count; ++i) {
        GpuLight light = lightBuffer.lights[i];
        vec3 toLight = light.positionRange.xyz - h.position;
        float distanceSq = dot(toLight, toLight);
        float range = light.positionRange.w;
        if (distanceSq >= range * range) continue;
        vec3 L = toLight * inversesqrt(max(distanceSq, 1e-8));
        float ratio = distanceSq / (range * range);
        float window = saturate(1.0 - ratio);
        float attenuation = window * window / (distanceSq + light.spotParams.z);
        if (light.directionType.w > 0.5) {
            float t = saturate((dot(light.directionType.xyz, -L) - light.spotParams.x) * light.spotParams.y);
            attenuation *= t * t;
        }
        result += diffuse * INV_PI * light.colorIntensity.rgb * light.colorIntensity.a * attenuation *
                  saturate(dot(h.normal, L));
    }
    return result;
}

// Outgoing radiance from a hit toward the ray origin: shadowed sun through
// the full Cook-Torrance lobe, hemisphere + environment ambient, emission
// and (for reflections) unshadowed local lights.
vec3 rtHitRadiance(RtSurface h, vec3 rayDir, bool localLights) {
    vec3 diffuse = h.albedo * (1.0 - h.metallic);
    vec3 f0 = mix(vec3(0.04), h.albedo, h.metallic);
    float a = h.perceptualRoughness * h.perceptualRoughness;
    vec3 V = -rayDir;
    float NoV = max(dot(h.normal, V), 1e-4);
    vec3 color = h.emissive;

    vec3 L = normalize(-scene.lightDirectionWS.xyz);
    vec3 sun = scene.lightColorIntensity.rgb * scene.lightColorIntensity.a;
    float NoL = dot(h.normal, L);
    if (NoL > 0.0 && dot(sun, sun) > 0.0 && !rtOccluded(rtOffset(h.position, h.normal), L, kRtFar, kRtMaskShadow)) {
        if (scene.cloudParams.x > 0.5) {
            sun *= cloudShadowAt(h.position, L, scene.cloudParams.y, scene.cloudParams.z, scene.cloudParams.w);
        }
        vec3 H = normalize(V + L);
        float NoH = saturate(dot(h.normal, H));
        float LoH = saturate(dot(L, H));
        vec3 F = F_Schlick(f0, 1.0, LoH);
        vec3 specular = D_GGX(NoH, a) * V_SmithGGXCorrelated(NoV, NoL, a) * F;
        color += (diffuse * INV_PI * (1.0 - F) + specular) * sun * NoL;
    }

    color += diffuse * hemisphereAmbient(h.normal);
    vec3 R = reflect(rayDir, h.normal);
    vec3 env = iblAvailable() ? prefilteredRadiance(R, h.perceptualRoughness) * reflectionNormalization(h.normal) *
                                    scene.iblParams.x
                              : hemisphereAmbient(R);
    color += F_Schlick(f0, 1.0, NoV) * env * (1.0 - 0.5 * a);
    if (localLights) color += rtLocalLights(h, diffuse);
    return color;
}

// Radiance arriving along a ray; misses return `missRadiance`.
vec3 rtTraceRadiance(vec3 origin, vec3 direction, vec3 missRadiance, bool localLights, float bounceScale) {
    RtSurface hit;
    if (!rtTraceSurface(origin, direction, kRtFar, hit)) return missRadiance;
    return rtHitRadiance(hit, direction, localLights) * bounceScale;
}

vec3 rtCosineDirection(vec3 N, vec2 u) {
    vec3 T, B;
    orthonormalBasis(N, T, B);
    float r = sqrt(u.x);
    float phi = 2.0 * PI * u.y;
    return normalize(T * (r * cos(phi)) + B * (r * sin(phi)) + N * sqrt(max(0.0, 1.0 - u.x)));
}

// One-bounce diffuse irradiance (radiance-equivalent, E / PI). With
// cosine-weighted sampling the estimator is the mean incoming radiance;
// sky misses make it replace the hemisphere ambient rather than add to it,
// so occluded areas darken naturally.
vec3 rtIndirectIrradiance(vec3 position, vec3 N, vec3 Ngeo, vec2 noise, int samples, float bounceScale) {
    vec3 origin = rtOffset(position, Ngeo);
    vec3 accum = vec3(0.0);
    for (int i = 0; i < samples; ++i) {
        vec2 u = fract(hammersley(uint(i), uint(samples)) + noise);
        vec3 dir = rtCosineDirection(N, u);
        if (dot(dir, Ngeo) <= 0.0) dir = reflect(dir, Ngeo);
        accum += rtTraceRadiance(origin, dir, hemisphereAmbient(dir), false, bounceScale);
    }
    return accum / float(samples);
}

// Fraction of short cosine-weighted rays that escape within `radius`.
float rtAmbientOcclusion(vec3 position, vec3 N, vec3 Ngeo, vec2 noise, float radius, int samples) {
    vec3 origin = rtOffset(position, Ngeo);
    float visible = 0.0;
    for (int i = 0; i < samples; ++i) {
        vec2 u = fract(hammersley(uint(i), uint(samples)) + noise);
        vec3 dir = rtCosineDirection(N, u);
        if (dot(dir, Ngeo) <= 0.0) dir = reflect(dir, Ngeo);
        visible += rtOccluded(origin, dir, radius, kRtMaskVisible) ? 0.0 : 1.0;
    }
    return visible / float(samples);
}

// GGX-distributed reflection direction around the mirror direction, so
// glossy surfaces get blurred traced reflections once TAA accumulates.
vec3 rtGlossyDirection(vec3 N, vec3 V, float roughness, vec2 u) {
    vec3 T, B;
    orthonormalBasis(N, T, B);
    vec3 Hl = importanceSampleGGX(vec2(u.x, u.y * 0.85), roughness);
    vec3 H = normalize(T * Hl.x + B * Hl.y + N * Hl.z);
    vec3 R = reflect(-V, H);
    return dot(R, N) > 0.0 ? R : reflect(-V, N);
}

#endif
