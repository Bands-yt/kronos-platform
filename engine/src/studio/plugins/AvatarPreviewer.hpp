#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AnimationDatabase.hpp"
#include "core/AssetCache.hpp"
#include "core/AvatarLoadout.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/Mesh.hpp"
#include "core/Texture.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/PreviewScene.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {

class AnimationPreviewerPlugin;

// Task category 4: "Avatar Previewer (Try-On System)." Owns a real
// mannequin entity (a procedural capsule -- see spawnMannequin()'s
// comment on why, same reasoning CharacterController's player capsule
// already established) in its own studio::PreviewScene, plus whatever
// items are currently equipped, applied through the exact same
// core::applyLoadoutToAvatar() a real runtime avatar sync would use --
// "try on" and "equip" are the same real mechanism here, just scoped to
// this previewer's own throwaway ECS/loadout rather than a player's
// actual one (this is Studio tooling, not a live multiplayer session).
class AvatarPreviewer final : public IStudioPlugin {
public:
    // `animationDatabase`/`animationPreviewer` are the Emote System's
    // integration seam (task category 6) -- equipping an Emote-category
    // item resolves it (core::resolveEmoteClip(), via `animationDatabase`)
    // and hands the real clip off to `animationPreviewer` for actual
    // rigged playback, since this class's own mannequin has no skeleton
    // to animate -- see equipItem()'s comment.
    AvatarPreviewer(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                     core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary,
                     core::AnimationDatabase& animationDatabase, AnimationPreviewerPlugin& animationPreviewer);

    [[nodiscard]] const char* name() const override { return "Avatar Previewer"; }
    [[nodiscard]] const char* category() const override { return "Avatar"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Equips `itemId` (resolved via `index`) into the previewer's own
    // loadout, replacing whatever was equipped in the same category, and
    // immediately reapplies it against the mannequin. `focusPanel` also
    // opens this plugin's window -- the "Try On" button passes true (so
    // clicking it actually shows the user something), "Equip" passes
    // false (equipping from a catalogue browse shouldn't yank the user's
    // UI focus away from what they were doing). Both are the same real
    // mechanism underneath -- there is only one mannequin/one loadout in
    // this Studio-only implementation -- a deliberate, stated scope
    // choice, not a placeholder distinction. Returns false if `itemId`
    // isn't found in `index`.
    //
    // If the equipped category is Emote, also resolves it against
    // animationDatabase_ (core::resolveEmoteClip(), see EmoteSystem.hpp)
    // and hands the real clip to animationPreviewer_->previewClip() --
    // this mannequin has no skeleton to play it on itself (see class
    // comment), so the actual rigged playback happens in the Animation
    // Previewer panel instead, which `focusPanel` opens in that case (not
    // this panel -- there'd be nothing to see here for an emote).
    // statusMessage_ reports either outcome (previewing, or why it
    // couldn't resolve) so equipping a not-yet-uploaded emote fails
    // visibly, not silently.
    bool equipItem(const std::string& itemId, const core::CatalogueIndex& index, bool focusPanel);

    [[nodiscard]] const core::AvatarLoadout& loadout() const { return loadout_; }

    // Render hook, called once per frame from StudioApp's pre-pass
    // callback, only while this panel is open (see PreviewScene.hpp's
    // class comment on why this is a separate call from drawPanel()).
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);

    void shutdown(core::Renderer& renderer);

private:
    void spawnMannequin();
    void reapplyLoadout(const core::CatalogueIndex& index);

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;
    core::AnimationDatabase* animationDatabase_;
    AnimationPreviewerPlugin* animationPreviewer_;

    PreviewScene scene_;
    core::EntityId mannequin_ = core::kNullEntity;
    core::AvatarLoadout loadout_;
    core::AssetCache<uint32_t> meshCache_;
    core::AssetCache<uint32_t> textureCache_;
    std::string statusMessage_;
};

} // namespace engine::studio::plugins
