#ifndef KRONOS_CUBEMAP_GLSL
#define KRONOS_CUBEMAP_GLSL

// Direction through the centre of texel `texel` on cube face `face`
// (Vulkan face order +X -X +Y -Y +Z -Z, matching samplerCube lookups).
vec3 cubeTexelDirection(uint face, uvec2 texel, uint size) {
    vec2 st = (vec2(texel) + 0.5) / float(size) * 2.0 - 1.0;
    float s = st.x;
    float t = st.y;
    vec3 d;
    if (face == 0u) d = vec3(1.0, -t, -s);
    else if (face == 1u) d = vec3(-1.0, -t, s);
    else if (face == 2u) d = vec3(s, 1.0, t);
    else if (face == 3u) d = vec3(s, -1.0, -t);
    else if (face == 4u) d = vec3(s, -t, 1.0);
    else d = vec3(-s, -t, -1.0);
    return normalize(d);
}

// Solid angle of a cube texel, up to a constant (normalised by callers).
float cubeTexelSolidAngle(uvec2 texel, uint size) {
    vec2 st = (vec2(texel) + 0.5) / float(size) * 2.0 - 1.0;
    float r2 = 1.0 + dot(st, st);
    return 1.0 / (r2 * sqrt(r2));
}

#endif
