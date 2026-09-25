#version 460
#extension GL_EXT_ray_query : require
#ifdef KRONOS_BINDLESS
#extension GL_EXT_nonuniform_qualifier : require
#endif

// Ray-query variant of scene.frag (selected only on devices with
// VK_KHR_ray_query): RT sun shadows, glossy reflections and one-bounce GI,
// each behind its own SceneUBO toggle.
#define KRONOS_RAY_TRACING
#include "kronos/forward_main.glsl"
