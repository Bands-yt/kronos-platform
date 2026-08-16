#pragma once

#include <memory>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AnimationDatabase.hpp"
#include "core/AnimationPlayer.hpp"
#include "core/AvatarAccessories.hpp"
#include "core/AvatarFace.hpp"
#include "core/AvatarLOD.hpp"
#include "core/AvatarLoadout.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/LocalProfile.hpp"
#include "core/RiggedAvatar.hpp"
#include "core/RiggedMesh.hpp"
#include "core/Skeleton.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/PreviewScene.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {

// Kronos ("Avatar Phase" -- "AvatarEditor: Skin-Tone Selection", "Body
// Sliders", "Clothing & Accessory Slots", and "Animation Overrides"): a
// real, focused Studio panel -- same real "own PreviewScene + a real
// rigged demo body" shape as studio::plugins::AnimationPreviewerPlugin
// (see that class's own header comment for the full real precedent this
// mirrors). Covers skin tone, head shape, height/width/limb-scale/torso-
// length/shoulder-width sliders, real clothing/accessory equip slots, and
// real idle/walk/run/jump_start/jump_air/jump_land animation overrides
// with a real, live, crossfade-blended preview on this panel's own demo
// body (via its own AnimationPlayer -- see previewPlayer_'s own comment
// for why this is a second, real, independent player rather than a
// hand-off to AnimationPreviewerPlugin's).
//
// The demo body uses a real, injected core::AvatarLoadout/
// core::CatalogueIndex (the local player's own real, persisted equipped
// items and the real, shared catalogue -- see the constructor's own
// comment), unlike studio::plugins::AnimationPreviewerPlugin's demo body,
// which intentionally stays on an empty loadout (it previews arbitrary
// clips on a bare rig, not appearance).
class AvatarEditor final : public IStudioPlugin {
public:
    // `localProfile` is real, injected, same "caller owns identity, this
    // panel just gets a reference" shape studio::plugins::CataloguePanel's
    // own localProfile_ already establishes -- one real, shared identity/
    // wallet/appearance record across Studio and engine_runtime, not two
    // independently-drifting ones.
    // Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory
    // Slots"): `catalogueIndex`/`loadout` are real, new, injected the same
    // way -- `catalogueIndex` is StudioApp's own real, shared, populated
    // index (the same one studio::plugins::CataloguePanel searches and
    // studio::plugins::UploadAvatarItemPlugin writes to), `loadout` is the
    // real, persistent record of what the local player has equipped (see
    // StudioApp::localAvatarLoadout_'s own comment on why it's distinct
    // from AvatarPreviewer's own, separate, never-persisted loadout_).
    // Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"):
    // `animationDatabase` is StudioApp's own real, shared database (the
    // same one studio::plugins::UploadAnimationPlugin writes to) --
    // resolves a chosen override id to a real clip file path.
    AvatarEditor(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                 core::RiggedMeshLibrary& riggedMeshLibrary, core::LocalProfile& localProfile,
                 core::CatalogueIndex& catalogueIndex, core::AvatarLoadout& loadout,
                 core::AnimationDatabase& animationDatabase);

    [[nodiscard]] const char* name() const override { return "Avatar Editor"; }
    [[nodiscard]] const char* category() const override { return "Avatar"; }

    // Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"):
    // real, ticked every frame regardless of isOpen() (see
    // IStudioPlugin's own class comment) -- advances previewPlayer_ and
    // writes the resulting pose into the demo body's segments, the same
    // "advance + write skinningMatrices every frame" shape
    // AnimationPreviewerPlugin::update() already establishes.
    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Render hook, called once per frame from StudioApp's pre-pass
    // callback, only while this panel is open -- same convention as
    // every other PreviewScene-owning plugin.
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);
    void shutdown(core::Renderer& renderer);

private:
    void spawnDemoBody();
    // Kronos ("Avatar Phase" -- "Avatar Head System"): real, destroys
    // every currently-spawned segment entity and calls spawnDemoBody()
    // again -- unlike skin tone (a pure per-entity baseColor mutation,
    // see applySkinTone()'s own comment), a head *shape* change is a
    // real geometry change (different ellipsoid radii, see
    // core::headShapeRadii()), which needs a real new GPU mesh upload,
    // not just a color write.
    void applyHeadShape(core::HeadShape shape);
    // Kronos ("Avatar Phase" -- "AvatarEditor: Body Sliders"): real
    // respawn, same reason as applyHeadShape() above -- a proportions
    // change is a real geometry/skeleton change (new joint bind-pose
    // positions, new limb cross-sections), not a color write. Reads the
    // three current slider values off `localProfile_` (already written by
    // the ImGui::SliderFloat callers in drawPanel() before this runs).
    void applyBodyProportions();
    // Real, live re-tint: mutates every already-spawned segment entity's
    // own SkinnedRenderable::baseColor directly (no re-upload, no
    // respawn -- baseColor is real per-draw data, not baked into the
    // mesh, see core::SkinnedRenderable's own comment), and real-saves
    // the real, chosen index to `localProfile_` immediately, the same
    // "persist on the spot" convention studio::plugins::CataloguePanel's
    // own real Purchase button already establishes.
    void applySkinTone(int index);
    // Kronos ("Avatar 2.0" -- "Clothing Meshes" -- "Studio Integration"):
    // real, full respawn (same reason setEquippedItem()'s own Torso/Legs
    // branch needs one -- the clothing shell has no fast re-tint path).
    void applyClothingFit(core::ClothingFit fit);
    // Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory
    // Slots"): the one real, shared re-tint step applySkinTone(),
    // equipItem(), and unequipItem() all call -- recomputes every
    // segment's real color via core::resolveSegmentColorsForLoadout()
    // (loadout takes precedence per-segment, skin tone is the real
    // fallback underneath, exactly that function's own existing,
    // unchanged contract) and writes the result straight into each
    // already-spawned entity's SkinnedRenderable::baseColor -- no
    // respawn, since equip/unequip/skin-tone changes never change
    // geometry, only per-segment color.
    void refreshSegmentColors();
    // Real: equips `itemId` (must resolve in `catalogueIndex_` under
    // `category`, per AvatarLoadout::equip()'s own contract) or unequips
    // whatever's currently in `category` when `itemId` is empty, then
    // real-saves `loadout_` to disk immediately (same "persist on the
    // spot" convention applySkinTone() already establishes) and
    // refreshes the live preview.
    void setEquippedItem(core::AvatarItemCategory category, const std::string& itemId);
    // Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"):
    // real -- writes `itemId` (a core::AnimationItem id, or empty for
    // "use the shipped default") into whichever LocalProfile field
    // `profileField` points at, saves the profile immediately (same
    // "persist on the spot" convention every other setter here already
    // establishes), then real-crossfades previewPlayer_ into the resolved
    // clip -- `itemId`'s own catalogue clip if set and it loads, else the
    // real shipped default named by `shippedFileBaseName` (matching
    // core::Application::spawnLocalPlayerAvatar()'s own real,
    // honest fail-soft fallback for a broken/missing override).
    void setAnimationOverride(std::string core::LocalProfile::*profileField, const std::string& itemId,
                               const char* shippedFileBaseName);
    // Real: loads `path`, crossfades previewPlayer_ into it (looping,
    // real 0.25s blend -- see core::AnimationPlayer::play()'s own
    // crossfade contract), returns the clip's own real display name on
    // success or an empty string on failure (caller decides how to
    // report that).
    std::string previewClipFromPath(const std::string& path, const std::string& displayName);

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::RiggedMeshLibrary* riggedMeshLibrary_;
    core::LocalProfile* localProfile_;
    core::CatalogueIndex* catalogueIndex_;
    core::AvatarLoadout* loadout_;
    core::AnimationDatabase* animationDatabase_;

    PreviewScene scene_;
    std::vector<core::EntityId> skinnedEntities_;
    core::Skeleton skeleton_;
    // Kronos ("Avatar Phase" -- "AvatarEditor: Animation Overrides"): a
    // real, second, independent core::AnimationPlayer -- deliberately not
    // a hand-off to AnimationPreviewerPlugin's own player, since that
    // would either steal focus to a different panel/body on every
    // override change or require this class to reach into that plugin's
    // private state; a self-contained player matches AvatarController's
    // own "one player per rigged body" scope (see AnimationPlayer's class
    // comment) and lets this panel's own viewport show overrides directly
    // as they're picked. unique_ptr, not a plain member, since it's
    // rebuilt (against the current, possibly body-slider-scaled skeleton)
    // every spawnDemoBody() call -- see that method's own comment.
    std::unique_ptr<core::AnimationPlayer> previewPlayer_;

    // Kronos ("Avatar 2.0" -- "Facial System" -- "Studio Integration" --
    // "face sliders"): real, live -- update() applies this every frame
    // via the exact same core::applyFacialExpressionToSkinningMatrices()
    // the real gameplay avatar/Home preview use, so a creator sees the
    // real expression system, not an approximation of it.
    core::AvatarFacialExpression facialExpression_;

    // Kronos ("Avatar 2.0" -- "Performance and LOD" -- "cache rig
    // transforms"): real, recomputed once per real spawnDemoBody() call,
    // reused by update() every frame after that -- same real caching
    // AvatarController::cachedBindPose_/runtime::HomeAvatarPreview::
    // cachedBindPose_ already establish.
    std::vector<glm::mat4> cachedBindPose_;

    std::string statusMessage_;
};

} // namespace engine::studio::plugins
