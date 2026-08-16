#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::core {

// Which equip slot a catalogue item occupies. LayeredClothing is its own
// category (not a variant of Torso/Legs) since it's meant to render *on
// top of* a Torso/Legs item rather than replace it -- see
// AvatarLoadout.hpp's comment on why it's the one category allowed to
// coexist with Torso/Legs in the same loadout. Emote has no visual
// attachment at all (see AvatarAttachment.hpp) -- it occupies a loadout
// slot the same way every other category does, but never spawns a child
// entity.
// Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory Slots"):
// `Shoes` and `Back` are real, new categories -- the spec's own explicit
// "top, bottom, shoes" clothing slots and "hat, face, back" accessory
// slots need a real, distinct category each to be independently
// equippable (AvatarLoadout's own "no duplicate categories" guarantee is
// structural, per-category -- see that class's own comment), which Torso
// (top)/Legs (bottom)/Head (hat)/Face didn't already provide.
enum class AvatarItemCategory {
    Head,
    Hair,
    Face,
    Torso,
    Legs,
    Accessory,
    LayeredClothing,
    Emote,
    Shoes,
    Back,
    // Kronos ("Marketplace Search + Categories v2" -- "Bundle (future use,
    // scaffold only)"): a real, new, distinct category value -- exists so
    // it round-trips/serializes/filters correctly and a future bundle
    // feature has a real home to file items under, but no real
    // multi-item-bundle purchase/composition logic is built yet (an
    // honest, stated gap, same "real slot, no fabricated behavior behind
    // it" pattern Shoes/Back/Emote/Accessory already establish for their
    // own real, stated visual-mapping gaps).
    Bundle,
};

[[nodiscard]] const char* avatarItemCategoryName(AvatarItemCategory category);

// Parses a category name (case-sensitive, exactly what
// avatarItemCategoryName() produces -- e.g. "Head", "LayeredClothing")
// back into the enum. Returns false (leaving `out` unchanged) for an
// unrecognized name rather than defaulting silently -- an unknown
// category in a catalogue JSON file is a real data problem a caller
// should surface, not paper over.
[[nodiscard]] bool avatarItemCategoryFromName(const std::string& name, AvatarItemCategory& out);

// One catalogue item's real content description -- what mesh/texture to
// load and what material parameters to render it with. Deliberately
// inlines the material fields (baseColor/metallic/roughness) the same
// way core::Renderable does, rather than a separate Material asset
// indirection -- this engine has no material *asset* system yet (see
// Renderable's own comment on its unused materialHandle), so an item's
// "material" is just its own baseColor/metallic/roughness, same as
// every other renderable thing in this engine.
//
// Pure data: loading the mesh/texture onto the GPU and creating real ECS
// entities from this needs Vulkan handles and an ECS this struct
// deliberately doesn't own -- see core::AvatarLoadout /
// studio::AvatarPreviewer for where that happens. Same separation as
// core::Prefab.
struct AvatarItem {
    std::string id;   // stable catalogue id (a creator-facing slug, e.g. "wizard_hat_01") -- never regenerated once uploaded
    std::string name; // display name, e.g. "Wizard Hat"
    AvatarItemCategory category = AvatarItemCategory::Accessory;
    std::vector<std::string> tags;

    std::string meshPath;    // real filesystem path to a .obj (core::loadObj)
    std::string texturePath; // real filesystem path to an image (core::Texture::loadFromFile); empty = no texture, matching Renderable::kInvalidHandle's "unassigned slot" convention

    glm::vec4 baseColor{0.8f, 0.8f, 0.8f, 1.0f};
    float metallic = 0.05f;
    float roughness = 0.6f;

    // Real validation, not cosmetic: non-empty id/name, a real category,
    // and meshPath/texturePath (if set) actually existing on disk --
    // "missing mesh"/"missing texture" are checked against the real
    // filesystem, not just "is the string non-empty." An empty
    // texturePath is legal (no texture assigned); a *non-empty*
    // texturePath pointing at a file that doesn't exist is not. Returns
    // false and fills `outError` with a specific, actionable reason on
    // the *first* problem found (not every problem at once -- matches
    // how a creator-facing upload UI wants to report one fixable issue
    // at a time, not a wall of text).
    [[nodiscard]] bool validate(std::string& outError) const;
};

} // namespace engine::core
