#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec4 inTangent; // xyz: tangent, w: handedness -- see Mesh.hpp's Vertex::tangent
// Kronos ("Avatar Visual Silhouette Pass" -- real per-vertex color):
// location 11 -- see Vertex::attributeDescriptions()'s own comment
// (Mesh.cpp) for why this specific location.
layout(location = 11) in vec4 inColor;

layout(location = 0) out vec3 outWorldPos;
layout(location = 1) out vec3 outWorldNormal;
layout(location = 2) out vec2 outUV;
// Material params forwarded to the shared shaders/scene.frag, which no
// longer reads push constants at all -- see that file's header comment.
// This vertex shader is the "one draw call per object" path (per-object
// data via push constants); shaders/scene_instanced.vert is the batched
// equivalent (per-object data via per-instance vertex attributes instead)
// -- both produce identical outputs at these same locations so the
// fragment shader genuinely doesn't care which one ran.
layout(location = 3) out flat vec4 outBaseColor;
layout(location = 4) out flat vec4 outMetallicRoughness;
layout(location = 5) out flat vec4 outEmissive;
layout(location = 8) out flat uvec4 outTextureIndices;
// World-space tangent + handedness -- appended at 6 rather than
// renumbering the outputs above (keeps this diff to an addition, not a
// renumbering, everywhere that already reads locations 0-5).
layout(location = 6) out vec4 outWorldTangent;
// Kronos ("Avatar Visual Silhouette Pass" -- real per-vertex color):
// deliberately NOT flat -- this is the one real, smoothly-interpolated
// varying in this whole shader, the entire point being a genuine
// per-fragment gradient across a triangle, not one flat value per
// primitive the way outBaseColor/outMetallicRoughness/outEmissive are.
layout(location = 7) out vec4 outVertexColor;

// Per-frame data -- one instance per frame-in-flight, bound once per
// drawSceneInto() call (see Renderer.cpp). Not rebound per-object; only
// its *contents* change frame to frame.
layout(set = 0, binding = 0) uniform SceneUBO {
    mat4 view;
    mat4 proj;
    mat4 lightViewProj[3];    // world -> light clip space, one per cascade -- consumed in scene.frag for shadow sampling
    mat4 invViewProj;         // world-space ray reconstruction -- only shaders/sky.frag reads this
    vec4 cascadeSplitsView;   // x/y/z: view-space far distance of cascades 0/1/2
    vec4 cascadeBiasScale;    // x/y/z: per-cascade shadow-bias scale
    vec4 lightDirectionWS;    // xyz: direction the light travels (surface-facing = -lightDirectionWS)
    vec4 lightColorIntensity; // rgb: color, a: intensity
    vec4 viewPositionWS;
    vec4 ambientColor;
    vec4 ambientGroundColor;
    vec4 fogColorDensity;     // rgb: fog color, a: density (0 = no fog) -- consumed in scene.frag
    vec4 skyZenithColor;      // only shaders/sky.frag reads this
    vec4 skyHorizonColor;     // only shaders/sky.frag reads this
} scene;

// Per-object data -- matches core::ObjectPushConstants exactly (see
// SceneTypes.hpp). Cheap enough to push per draw call at this entity
// count; entities that opt into GPU instancing (Renderable::instanced)
// skip this entirely and go through scene_instanced.vert instead.
layout(push_constant) uniform ObjectPushConstants {
    mat4 model;
    vec4 baseColor;
    vec4 metallicRoughness; // x: metallic, y: roughness
    vec4 emissive;          // rgb: emissive color, a: intensity
    // Packed bindless slots, two 16-bit indices per component -- see
    // core::ObjectPushConstants and scene.frag. Present unconditionally so
    // the push-constant block keeps matching the C++ struct byte-for-byte
    // whichever fragment variant is bound.
    uvec4 textureIndices;
} object;

void main() {
    vec4 worldPos = object.model * vec4(inPosition, 1.0);
    outWorldPos = worldPos.xyz;

    // Correct for non-uniform scale; cheap enough at this vertex count to
    // compute per-vertex rather than uploading a separate normal matrix.
    mat3 normalMatrix = transpose(inverse(mat3(object.model)));
    outWorldNormal = normalize(normalMatrix * inNormal);
    // Tangent transforms by the model matrix directly (a surface-embedded
    // direction, not a covector like the normal), handedness unchanged.
    outWorldTangent = vec4(normalize(mat3(object.model) * inTangent.xyz), inTangent.w);

    outUV = inUV;
    outBaseColor = object.baseColor;
    outMetallicRoughness = object.metallicRoughness;
    outEmissive = object.emissive;
    outTextureIndices = object.textureIndices;
    outVertexColor = inColor;
    gl_Position = scene.proj * scene.view * worldPos;
}
