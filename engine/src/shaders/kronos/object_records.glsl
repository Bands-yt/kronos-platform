#ifndef KRONOS_OBJECT_RECORDS_GLSL
#define KRONOS_OBJECT_RECORDS_GLSL

// Per-object data that does not fit in the 128-byte push constant block:
// previous-frame transform (motion vectors) and the extended material
// layers. Indexed by the high 16 bits of textureIndices.z; record 0 is
// the default (no extensions, no previous transform).

struct ObjectRecord {
    mat4 prevModel;
    vec4 clearcoat; // x strength, y perceptual roughness, z anisotropy [-1, 1], w anisotropy rotation (radians)
    vec4 sheen;     // rgb color, a perceptual roughness
    vec4 misc;      // x specular reflectance (0.5 = 4% F0), y 1 if prevModel is valid, z water waves, w water foam
    vec4 pattern;   // x grid cell size in world units (0 = no grid)
};

layout(std430, set = 0, binding = 11) readonly buffer ObjectRecords {
    ObjectRecord records[];
} objectRecords;

uint objectRecordIndex(uvec4 textureIndices) {
    return textureIndices.z >> 16;
}

// Previous-frame world position for velocity. Objects without a valid
// previous transform only contribute camera motion.
vec4 previousWorldPosition(uint recordIndex, vec4 localPos, vec4 currentWorldPos) {
    ObjectRecord r = objectRecords.records[recordIndex];
    return r.misc.y > 0.5 ? r.prevModel * localPos : currentWorldPos;
}

#endif
