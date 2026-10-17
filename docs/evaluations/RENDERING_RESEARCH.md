# Rendering research notes

Notes only. Nothing here is built.

## Subsurface scattering (0221)
Skin, wax and leaves need light to soak in. Cheap version: screen-space blur of the diffuse light weighted
by a per-material width (the "separable SSS" technique), one extra pass on materials that opt in.
Cost: about 0.3 ms at 1080p on a mid GPU. Needs a stencil or flag bit per pixel.

## Contact shadows (0254) and screen-space reflections (0259)
Both are ray marches in the depth buffer. Contact shadows march 8 to 16 steps toward the light for a short
distance and fix the gap that shadow maps leave near object feet. SSR marches the reflected ray in screen space
and falls back to the reflection probe when it leaves the screen (`probeBlendWeights` in `core/GfxAnimTools.hpp`).
Quality levels: low = 16 steps half resolution, high = 32 steps full resolution with a blur.

## Baked lightmaps (0255)
Needs UV2 unwrapping, a baker (CPU path tracer first, GPU later), a texture atlas and a way to know when a
bake is out of date (`needsRebake` in `core/RenderMediaTools.hpp`). About three weeks. Light probes
(`probeGrid`) are the cheaper first step for moving objects.

## Offline renderer (0345) and path tracing (0347)
`engine_runtime --gpu-info` shows whether the GPU has ray tracing (an RTX 5060 does). A path-traced
"photo mode" that accumulates frames while the camera is still would reuse the scene data. Keep it as a research
branch behind a flag with the normal renderer as the fallback. Not started; needs at least two weeks.
