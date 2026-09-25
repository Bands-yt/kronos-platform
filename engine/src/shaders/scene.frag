#version 450
#ifdef KRONOS_BINDLESS
#extension GL_EXT_nonuniform_qualifier : require
#endif

// Forward opaque shading. The body lives in kronos/forward_main.glsl so the
// rasterized and ray-traced variants cannot drift apart.
#include "kronos/forward_main.glsl"
