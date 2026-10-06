#version 460
#ifdef KRONOS_SHADER_CLOCK
#extension GL_ARB_shader_clock : require
#endif
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_buffer_reference_uvec2 : require
#ifdef KRONOS_BINDLESS
#extension GL_EXT_nonuniform_qualifier : require
#endif

// Ray-query variant of scene.frag (selected only on devices with
// VK_KHR_ray_query): soft RT sun shadows, glossy reflections, one-bounce GI
// and ambient occlusion, each behind its own SceneUBO toggle.
#define KRONOS_RAY_TRACING
#include "kronos/forward_main.glsl"
