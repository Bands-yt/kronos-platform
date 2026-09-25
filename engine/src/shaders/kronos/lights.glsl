#ifndef KRONOS_LIGHTS_GLSL
#define KRONOS_LIGHTS_GLSL

// Clustered punctual lights. The view frustum is split into a
// clusterDims.x * clusterDims.y screen tiles times clusterDims.z
// exponentially-distributed depth slices; cluster_build.comp writes, per
// cluster, the indices of the lights whose range sphere touches it.
// Requires scene_resources.glsl + material.glsl.

uint clusterSlice(float viewDepth) {
    float slice = log(max(viewDepth, 1e-4)) * scene.clusterParams.x - scene.clusterParams.y;
    return uint(clamp(slice, 0.0, float(scene.clusterDims.z - 1u)));
}

uint clusterIndexAt(vec2 fragCoord, float viewDepth) {
    uvec2 tile = min(uvec2(fragCoord / scene.clusterParams.z), scene.clusterDims.xy - 1u);
    uint slice = clusterSlice(viewDepth);
    return (slice * scene.clusterDims.y + tile.y) * scene.clusterDims.x + tile.x;
}

// Windowed inverse-square falloff: the (1 - (d/r)^2)^2 window reaches
// exactly zero at the range (so clusters can cull by it) and the
// softening radius keeps the peak finite. Softening radius 1 reproduces
// the engine's original point-light falloff exactly.
float lightAttenuation(float distanceSq, float range, float softeningSq) {
    float ratio = distanceSq / (range * range);
    float window = saturate(1.0 - ratio);
    return (window * window) / (distanceSq + softeningSq);
}

float spotAttenuation(GpuLight light, vec3 L) {
    float cd = dot(light.directionType.xyz, -L);
    float t = saturate((cd - light.spotParams.x) * light.spotParams.y);
    return t * t;
}

vec3 evaluatePunctualLight(ShadingContext s, PixelParams p, GpuLight light) {
    vec3 toLight = light.positionRange.xyz - s.worldPos;
    float distanceSq = dot(toLight, toLight);
    float range = light.positionRange.w;
    if (distanceSq >= range * range) return vec3(0.0);
    vec3 L = toLight * inversesqrt(max(distanceSq, 1e-8));
    float attenuation = lightAttenuation(distanceSq, range, light.spotParams.z);
    if (light.directionType.w > 0.5) attenuation *= spotAttenuation(light, L);
    if (attenuation <= 0.0) return vec3(0.0);
    vec3 radiance = light.colorIntensity.rgb * light.colorIntensity.a * attenuation;
    return surfaceShading(s, p, L, radiance, 1.0);
}

vec3 evaluateClusteredLights(ShadingContext s, PixelParams p, vec2 fragCoord, float viewDepth) {
    if (scene.clusterParams.w < 0.5) return vec3(0.0);
    uint cluster = clusterIndexAt(fragCoord, viewDepth);
    uint count = min(clusterCounts.counts[cluster], scene.clusterDims.w);
    uint base = cluster * scene.clusterDims.w;
    vec3 result = vec3(0.0);
    for (uint i = 0u; i < count; ++i) {
        result += evaluatePunctualLight(s, p, lightBuffer.lights[clusterIndices.indices[base + i]]);
    }
    return result;
}

#endif
