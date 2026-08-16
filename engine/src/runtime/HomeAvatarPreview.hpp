#pragma once

#include <memory>
#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AnimationDatabase.hpp"
#include "core/AnimationPlayer.hpp"
#include "core/AvatarFace.hpp"
#include "core/AvatarLoadout.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/LocalProfile.hpp"
#include "core/RiggedAvatar.hpp"
#include "core/RiggedMesh.hpp"
#include "core/Skeleton.hpp"
#include "studio/PreviewScene.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::runtime {

// Kronos ("Home Screen Avatar Preview"): a real, live, rigged avatar
// preview for the Home screen -- the exact same real mesh + rig
// pipeline studio::plugins::AvatarEditor uses (studio::PreviewScene,
// core::buildHumanoidSkeleton/applyBodyProportionsToSkeleton/
// spawnRiggedAvatar, core::AnimationPlayer), just view-only (no slider/
// equip UI of its own -- appearance is edited in Studio's AvatarEditor
// or the Avatar Shop, this only ever *shows* the real, current,
// already-persisted appearance) and always idle-animated. Reused, not
// reimplemented -- see CMakeLists.txt's own comment on why
// studio::PreviewScene/OffscreenTarget compile directly into
// engine_runtime too, the same "one source file, two targets" precedent
// core::UITheme.cpp already established.
class HomeAvatarPreview {
public:
    HomeAvatarPreview(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                       core::RiggedMeshLibrary& riggedMeshLibrary, const core::LocalProfile& localProfile,
                       const core::CatalogueIndex& catalogueIndex, const core::AvatarLoadout& loadout,
                       const core::AnimationDatabase& animationDatabase);

    // Real, ticked every frame while the preview is visible -- advances
    // the idle animation and writes the resulting pose, same "advance +
    // write skinningMatrices every frame" shape AvatarEditor::update()
    // already establishes.
    void update(float dt);

    // Draws the orbit/zoom viewport into whatever ImGui child the caller
    // has already Begin()'d -- see studio::PreviewScene::
    // drawAndHandleOrbit()'s own comment for the real, free orbit (left-
    // drag) + zoom (scroll) behavior this reuses verbatim.
    void draw();

    // Real, destroys and rebuilds the preview body against the current
    // localProfile_/loadout_ state -- callers invoke this from the exact
    // same real appearance-change call sites that already call
    // core::Application::refreshLocalPlayerAvatarAppearance() (Avatar
    // Shop equip/unequip), so the Home preview never silently drifts out
    // of sync with what the player actually looks like in-game.
    void refresh();

    // Render hook, called once per frame from RuntimeShell's own real
    // PrePassCallback (core::Renderer::setPrePassCallback() -- confirmed
    // unused anywhere else in engine_runtime, free to claim), only while
    // Home is actually visible -- same "don't render a closed/invisible
    // preview" convention every PreviewScene-owning Studio plugin already
    // follows.
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);
    void shutdown(core::Renderer& renderer);

private:
    void spawnPreviewBody();

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::RiggedMeshLibrary* riggedMeshLibrary_;
    const core::LocalProfile* localProfile_;
    const core::CatalogueIndex* catalogueIndex_;
    const core::AvatarLoadout* loadout_;
    const core::AnimationDatabase* animationDatabase_;

    studio::PreviewScene scene_;
    std::vector<core::EntityId> skinnedEntities_;
    core::Skeleton skeleton_;
    std::unique_ptr<core::AnimationPlayer> previewPlayer_;

    // Kronos ("Avatar 2.0" -- "Runtime Integration" -- "Ensure Home
    // avatar preview supports facial expressions"): real -- the Home
    // preview has no gameplay AvatarController to drive this (it's
    // view-only), so it ticks the exact same real, shared
    // core::blendFacialExpressionTowards()/applyFacialExpressionToSkinningMatrices()
    // pure functions directly, with its own small, real, periodic
    // auto-blink -- same "a face that never blinks reads as broken"
    // reasoning AvatarController's own auto-blink already establishes,
    // duplicated here (not shared as a class) since this preview has no
    // locomotion state machine to hang a shared owner off of.
    core::AvatarFacialExpression facialExpression_;
    float autoBlinkTimer_ = 3.0f;
    float autoBlinkProgress_ = -1.0f;
};

} // namespace engine::runtime
