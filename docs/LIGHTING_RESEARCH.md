# Lighting research notes

## Emissive materials and indirect light
Right now an emissive surface glows but does not light its neighbours. Options, cheapest first:
1. Place a real light next to the surface (works today, manual).
2. Bake: sample emissive surfaces into a light probe volume ([probeGrid / probeBlendWeights](../engine/src/core/GfxAnimTools.hpp)).
3. Screen-space: only lights what is on screen, so it flickers at the edges.
4. Ray-traced GI: needs a GPU with ray tracing (`engine_runtime --gpu-info` shows if yours has it). Parked for 5.0.

Recommendation: do option 2 for static scenes after the baked lightmap pipeline exists.

## Volumetric fog and objects
Fog density is a single value per scene today. Moving objects could add or remove fog by writing into a
low-resolution 3D texture in front of the camera. The cost is one extra pass; transmittance follows
`exp(-density * distance)` ([fogTransmittance](../engine/src/core/GfxAnimTools.hpp)). Not built.
