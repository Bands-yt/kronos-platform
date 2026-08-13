#pragma once

#include <cstdint>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "tntwars/MapMaterial.hpp"

namespace engine::core {

struct Renderable;
class TextureLibrary;

// Sprint 16 ("Cinematic Graphics" Phase 2, "Material & Asset Replacement"):
// real, procedurally-*generated* PBR texture sets (albedo/normal/metallic/
// roughness/AO) -- stone, brushed metal, lava, and a baked grass/dirt
// ground blend. No real texture assets (photographed or hand-authored)
// exist anywhere in this repo and none can be downloaded in this
// environment (see README.md's "Real Assets vs. Procedural Content"
// section) -- these are generated directly as RGBA pixel buffers by a
// small deterministic value-noise generator (see ProceduralMaterials.cpp)
// and uploaded straight to the GPU via Texture::createFromPixels(), the
// same real upload path loadFromFile() uses, just skipping a pointless
// write-to-disk-then-read-back round trip through a file format. Every
// generator is a pure function of its (fixed, hardcoded) seed -- the same
// generation call always produces byte-identical pixels, which matters
// for this engine's own determinism guarantees (see trailer/TrailerDirector.hpp's
// header comment) since these materials appear in trailer capture output.
//
// Deliberately real-but-simpler than a true "ground blend map" system
// (Phase 2's own "Ground -> layered textures (blend maps)" wording): the
// grass/dirt transition is baked into one real albedo texture via
// large-scale noise at *generation* time, not blended by a second UV/
// mask texture at *render* time -- see generateGroundBlend()'s own
// comment for the honest reasoning.
struct PbrTextureSet {
    uint32_t albedo = ~0u;
    uint32_t normal = ~0u;
    uint32_t metallic = ~0u;
    uint32_t roughness = ~0u;
    uint32_t ao = ~0u;
};

// One generated set per real material kind this engine currently needs,
// created once (generate()) and reused across every entity that wants
// that material -- textures are content, not per-entity state. `applyTo()`
// is the shared real dispatch both studio::plugins::TntWarsPlugin and
// trailer::TrailerDirector use when spawning tntwars::MapLayoutPiece
// entities, keyed off the same pure tntwars::classifyMapPieceMaterial()
// classification (see MapMaterial.hpp) so both real spawn sites -- Studio
// and the trailer -- always agree on which map piece gets which material.
struct ProceduralMaterialLibrary {
    PbrTextureSet stone;
    PbrTextureSet metal;
    PbrTextureSet lava;
    PbrTextureSet ground;
    // Sprint 16 Phase 5 (Mining Sim RTX prototype) -- glowing ore-vein
    // crystal, see generate()'s own .cpp comment. Not part of applyTo()'s
    // tntwars::MapPieceMaterialKind dispatch (no TNT-Wars map piece is a
    // crystal) -- miningsim::buildRtxPrototypeScene() reads this field
    // directly.
    PbrTextureSet crystal;

    // Kronos ("Four RTX Maps" Phase 3): real, procedurally-generated
    // presets for the new Volcano/Underwater/Trenches/Sky map content --
    // same real value-noise generation, same zero-art-asset discipline,
    // see ProceduralMaterials.cpp's own comment on each.
    PbrTextureSet mud;
    PbrTextureSet wood;
    PbrTextureSet coral;
    PbrTextureSet sand;

    // Kronos ("Sky Map Full Engine Specification"): real, procedurally-
    // generated presets for Terrain's own height/slope material blending
    // (Terrain::HeightSlopeMaterialBand takes a PbrTextureSet* directly,
    // not through applyTo()'s tntwars::MapPieceMaterialKind dispatch below
    // -- these two fields are consumed straight by a caller building
    // bands, same as `crystal` above already is for the Mining Sim scene).
    PbrTextureSet grass;
    PbrTextureSet dirt;

    [[nodiscard]] static ProceduralMaterialLibrary generate(TextureLibrary& textureLibrary, VmaAllocator allocator,
                                                              VkDevice device, VkCommandPool cmdPool, VkQueue queue);

    // Sets `renderable`'s texture handles plus real, material-appropriate
    // metallic/roughness/emissive values (never touched by the two real
    // ECS-spawn loops today -- see MapMaterial.hpp's own header comment)
    // -- a real no-op (every field left at its Components.hpp default)
    // for tntwars::MapPieceMaterialKind::None (team bases, water --
    // neither maps to a real material generated here).
    void applyTo(Renderable& renderable, tntwars::MapPieceMaterialKind kind) const;
};

} // namespace engine::core
