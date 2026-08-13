#pragma once

#include <string>

#include "core/AnimationDatabase.hpp"
#include "core/AvatarController.hpp"
#include "core/AvatarLoadout.hpp"

namespace engine::core {

// Links two independent catalogues under one shared id convention -- a
// real, deliberately minimal integration rather than a schema merge:
// an emote is listed in *both* core::CatalogueDatabase (an
// AvatarItemManifest, category Emote -- what a shopper browses: name,
// tags, price, "Purchase") and core::AnimationDatabase (an
// AnimationManifest, category Emote -- what actually plays: the real
// clip file, looping, duration) under the *same* AvatarItem::id /
// AnimationItem::id. Nothing forces the two entries to share an id
// beyond this convention -- resolveEmoteClip() is exactly the function
// that turns "an equipped catalogue item id" into "a real playable
// clip," and fails honestly (not silently) if the convention wasn't
// followed for a given id (a listed-but-unplayable emote is a real,
// surfaceable creator/data problem, not a no-op).
//
// This is *not* a general asset-linking system -- it's scoped to exactly
// the one cross-catalogue relationship this pass's Emote System needs.

// Resolves `itemId` to its real AnimationClip via `animationDatabase`
// (itemId must match an AnimationManifest whose category is Emote, and
// its clipPath must load) -- the "avatar catalogue id -> the animation
// database entry that actually plays" half of the convention above.
// Returns false (filling `outError`) if the id isn't in
// `animationDatabase` at all, isn't categorized as Emote, or its clip
// file fails to load.
[[nodiscard]] bool resolveEmoteClip(const std::string& itemId, const AnimationDatabase& animationDatabase,
                                     AnimationClip& outClip, std::string& outError);

// Real equip -> resolve -> play glue: if `loadout` has an Emote equipped
// and it resolves via resolveEmoteClip(), plays it on `controller` as a
// full-body emote (classic Roblox-style emotes take over the whole
// character, not just the upper body -- see
// AvatarController::playEmote()'s `fullBody` parameter), looping or not
// per the resolved clip's own AnimationClip::looping flag (an emote's
// author decides that when authoring the clip, not this function), and
// returns true.
// If nothing is equipped in the Emote category, or it fails to resolve,
// calls controller.stopEmote() (a real no-op if nothing was already
// playing, not an error) and returns false. `outError` is left empty for
// the ordinary "nothing equipped" case and only filled in when
// resolveEmoteClip() itself reports a real problem (a listed-but-
// unplayable emote) -- check `outError.empty()` to tell the two apart. A
// thin function most real callers (a runtime emote keybind,
// studio::plugins::AvatarPreviewer's "Equip" flow) can call directly
// instead of hand-rolling the equip-check + resolve + play sequence
// themselves.
bool playEquippedEmote(const AvatarLoadout& loadout, const AnimationDatabase& animationDatabase,
                        AvatarController& controller, std::string& outError);

} // namespace engine::core
