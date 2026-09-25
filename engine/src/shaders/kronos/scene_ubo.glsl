#ifndef KRONOS_SCENE_UBO_GLSL
#define KRONOS_SCENE_UBO_GLSL

// Single source of truth for set 0 / binding 0. Must match
// engine::core::SceneUBO (SceneTypes.hpp) byte-for-byte (std140; every
// member is a vec4/uvec4/mat4 so the C++ layout is identical).

#define KRONOS_CASCADE_COUNT 4

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;                  // includes the TAA sub-pixel jitter when TAA is on
    mat4 invViewProj;           // inverse of the jittered proj * view
    mat4 viewProjNoJitter;
    mat4 prevViewProjNoJitter;
    mat4 lightViewProj[KRONOS_CASCADE_COUNT];
    vec4 cascadeSplitsView;     // view-space far distance of each cascade
    vec4 cascadeTexelWorld;     // world-space footprint of one shadow texel, per cascade
    vec4 cascadeDepthRange;     // world-space depth range covered by each cascade's ortho projection
    vec4 shadowParams;          // x receiver-plane bias scale, y tan(sun angular radius), z map resolution, w filter (0 hard, 1 PCF, 2 PCSS)
    vec4 lightDirectionWS;      // xyz: direction light travels
    vec4 lightColorIntensity;
    vec4 viewPositionWS;
    vec4 ambientColor;          // sky hemisphere radiance
    vec4 ambientGroundColor;    // ground hemisphere radiance
    vec4 fogColorDensity;
    vec4 skyZenithColor;
    vec4 skyHorizonColor;
    vec4 renderFlags;           // x RT shadows, y performance mode, z wetness, w caustics
    vec4 reflectionParams;      // x RT reflections, y rough cutoff, z mirror cutoff
    vec4 atmosphereParams;      // x enabled, y sun intensity, z mie strength, w suppress sun disk
    vec4 cloudParams;           // x enabled, y coverage, z speed, w time (s)
    vec4 giParams;              // x RT GI, y intensity
    vec4 iblParams;             // x specular intensity, y reflection normalization, z prefiltered max mip, w IBL valid
    vec4 taaJitter;             // xy current jitter (NDC), zw previous
    vec4 screenSize;            // xy pixels, zw reciprocal
    vec4 clusterParams;         // x slice scale, y slice bias, z tile size (px), w light count
    uvec4 clusterDims;          // xyz grid dims, w max lights per cluster
    vec4 frameParams;           // x frame index (wraps), y 1 when TAA resolves this view, zw reserved
} scene;

#endif
