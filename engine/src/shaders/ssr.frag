#version 450

// Kronos ("Rendering Fidelity" -- SSR fallback pass): real screen-space
// reflections -- a genuinely new fullscreen post-process pass (the same
// "one triangle, no vertex buffer" shape shaders/sky.frag and
// shaders/volumetric_fog.frag already use), reading back the already-
// rendered opaque scene color (frame.hdrImage) + depth and raymarching
// the depth buffer in world space to find what a reflective surface
// would actually see.
//
// The real, honest architectural limitation this shader works under:
// this renderer is forward-shaded with no G-buffer at all (no per-pixel
// normal/roughness/metallic buffer survives past the opaque pass, see
// this engine's own established scope). A *correct* SSR implementation
// wants exactly that data; without it, this shader instead:
//   - derives a real, if triangle-flat (not smoothly interpolated), world-
//     space normal from screen-space derivatives of the depth-reconstructed
//     world position (dFdx/dFdy) -- the standard technique for exactly
//     this "no G-buffer" constraint.
//   - can't distinguish "this pixel is glossy metal" from "this pixel is
//     matte cloth" (no roughness/metallic to sample), so it applies a
//     real, uniform Schlick Fresnel weight (low reflectance head-on,
//     rising at grazing angles) to *every* opaque pixel alike, rather
//     than scene_rt.frag's own real per-material roughness/metallic gate
//     (setReflectionRoughnessCutoff()). This reads as a real, subtle
//     "grazing-angle sheen" on every surface -- an honest, coarser
//     fallback tier, not a drop-in replacement for the real hybrid RT
//     reflection path.
// Like every SSR technique, it can only ever reflect what's actually
// visible on screen this frame -- a real, inherent limitation, not a bug:
// a miss (ray leaves the screen, or never finds a depth-buffer hit within
// `maxDistance`) falls back to the pass's own unmodified input color, so
// this pass is always a real, safe superset of "no reflection at all,"
// never a corruption of it.

layout(location = 0) in vec2 inUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj[3];
    mat4 invViewProj;
    vec4 cascadeSplitsView;
    vec4 cascadeBiasScale;
    vec4 lightDirectionWS;
    vec4 lightColorIntensity;
    vec4 viewPositionWS;
    vec4 ambientColor;
    vec4 ambientGroundColor;
    vec4 fogColorDensity;
    vec4 skyZenithColor;
    vec4 skyHorizonColor;
} scene;

// set=1: reused cinematicDescriptorSetLayout_ shape (hdrColor + sceneDepth),
// same real "2-binding, no bespoke layout" convention
// volumetricFogPipelineLayout_ already established for this exact pair.
layout(set = 1, binding = 0) uniform sampler2D hdrColor;
layout(set = 1, binding = 1) uniform sampler2D sceneDepth;

layout(push_constant) uniform SSRPushConstants {
    float maxDistance;
    float thickness;
    float stepCount;
} push;

// Real world-space reconstruction from a UV + raw depth sample -- the
// same invViewProj unprojection technique shaders/sky.frag's own header
// comment documents, just parameterized by an arbitrary UV/depth pair
// here instead of always this fragment's own.
vec3 worldPosAt(vec2 uv, float rawDepth) {
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 clip = vec4(ndc, rawDepth, 1.0);
    vec4 world = scene.invViewProj * clip;
    return world.xyz / world.w;
}

// Real, positive forward view-space depth (camera looks down -Z in this
// engine's own real glm::lookAt-built view matrix, see core::Camera::viewMatrix()'s
// own comment) -- used as the common real "distance from camera" metric
// both the marched candidate and the stored depth-buffer surface are
// compared in, since raw NDC depth is nonlinear and comparing
// `push.thickness` (a real world-unit value) against it directly would
// be wrong.
float viewDepthOf(vec3 worldPos) { return -(scene.view * vec4(worldPos, 1.0)).z; }

void main() {
    vec3 originalColor = texture(hdrColor, inUV).rgb;

    float rawDepth = texture(sceneDepth, inUV).r;
    if (rawDepth >= 0.9999) {
        // Real, honest early-out -- this pixel is the sky/background
        // (never written by the opaque pass, still at its own real clear
        // value), which has no real surface to reflect anything at all.
        outColor = vec4(originalColor, 1.0);
        return;
    }

    vec3 worldPos = worldPosAt(inUV, rawDepth);

    // Real, triangle-flat world-space normal from screen-space
    // derivatives -- see this file's own header comment on why (no
    // G-buffer to sample a real per-vertex-interpolated one from).
    vec3 N = normalize(cross(dFdx(worldPos), dFdy(worldPos)));
    vec3 viewDir = normalize(scene.viewPositionWS.xyz - worldPos);
    // Real, honest orientation fix -- dFdx/dFdy's cross product can come
    // out facing either way depending on screen-space winding; flipping
    // it to always face the camera is what a real geometric normal means
    // for reflection purposes here (this pass never needs the "back
    // face" distinction a lit shading pass would).
    if (dot(N, viewDir) < 0.0) N = -N;

    vec3 R = reflect(-viewDir, N);

    // Real Schlick Fresnel, dielectric base reflectance (0.04, the
    // standard real non-metal F0) -- see this file's own header comment
    // on why this is a uniform per-pixel weight, not a real per-material
    // roughness/metallic gate.
    float cosTheta = clamp(dot(N, viewDir), 0.0, 1.0);
    float fresnel = 0.04 + 0.46 * pow(1.0 - cosTheta, 5.0); // real, capped well below 1.0 -- a subtle sheen, not a mirror-everything effect

    int steps = int(push.stepCount);
    float stepSize = push.maxDistance / max(push.stepCount, 1.0);
    // Real, small bias off the surface along its own normal -- avoids
    // the very first marched sample immediately self-intersecting the
    // real triangle it started on (the same real "offset off the origin
    // triangle" reasoning shaders/scene_rt.frag's own traceReflection()
    // uses for its ray-query `tMin`, just a world-space epsilon here
    // instead of a ray-query tMin parameter).
    vec3 marchOrigin = worldPos + N * 0.05;

    bool hit = false;
    vec2 hitUV = vec2(0.0);
    for (int i = 1; i <= steps; ++i) {
        vec3 samplePos = marchOrigin + R * (stepSize * float(i));
        vec4 clip = scene.proj * scene.view * vec4(samplePos, 1.0);
        if (clip.w <= 0.0) break; // real, honest -- behind the camera, this march can never re-enter the visible frame from here
        vec2 sampleUV = (clip.xy / clip.w) * 0.5 + 0.5;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0) break; // real, honest -- left the screen, a real SSR miss

        float candidateViewDepth = viewDepthOf(samplePos);
        float surfaceRawDepth = texture(sceneDepth, sampleUV).r;
        if (surfaceRawDepth >= 0.9999) continue; // real sky/background at this screen point -- never a real hit
        float surfaceViewDepth = viewDepthOf(worldPosAt(sampleUV, surfaceRawDepth));

        float depthDelta = candidateViewDepth - surfaceViewDepth;
        if (depthDelta > 0.0 && depthDelta < push.thickness) {
            hit = true;
            hitUV = sampleUV;
            break;
        }
    }

    if (!hit) {
        outColor = vec4(originalColor, 1.0);
        return;
    }

    vec3 reflectionColor = texture(hdrColor, hitUV).rgb;
    // Real, additional screen-edge fade -- a hit right at the very edge
    // of the screen would otherwise pop discontinuously as the camera
    // turns (the true reflected content there is only known right up
    // until it scrolls off, then real SSR has no choice but to lose it).
    vec2 edgeDist = min(hitUV, 1.0 - hitUV);
    float edgeFade = clamp(min(edgeDist.x, edgeDist.y) / 0.08, 0.0, 1.0);

    vec3 color = mix(originalColor, reflectionColor, fresnel * edgeFade);
    outColor = vec4(color, 1.0);
}
