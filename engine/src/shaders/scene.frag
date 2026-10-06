#version 450
#ifdef KRONOS_SHADER_CLOCK
#extension GL_ARB_shader_clock : require
#endif
#ifdef KRONOS_BINDLESS
#extension GL_EXT_nonuniform_qualifier : require
#endif

// Forward opaque shading. The body lives in kronos/forward_main.glsl so the
// rasterized and ray-traced variants cannot drift apart.
#include "kronos/forward_main.glsl"
