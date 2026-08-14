#pragma once

#include <string>
#include <vector>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AnimationPlayer.hpp"
#include "core/AvatarLoadout.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/RiggedAvatar.hpp"
#include "core/RiggedMesh.hpp"
#include "core/Skeleton.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/PreviewScene.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {

// Task category 4: "Studio Animation Previewer Plugin." The first real
// consumer of the GPU skinning pipeline (core::RiggedMesh/
// core::SkinnedRenderable/shaders/scene_skinned.vert -- see Renderer.hpp's
// AuxiliarySceneHandle/skinning comments) built in this pass to actually
// draw a skinned entity: everything through core::AvatarController
// compiled and was live-verified not to crash with zero skinned entities
// in the scene, but nothing had rendered through scene_skinned.vert for
// real until this plugin.
//
// Owns a real rigged demo body (core::buildHumanoidSkeleton() +
// core::spawnRiggedAvatar() against an empty loadout/catalogue, so every
// segment renders in its neutral default color -- there is no catalogue
// browsing here, just a real, animatable rig to preview clips against) in
// its own studio::PreviewScene, plus one real core::AnimationPlayer
// driving it. Play/pause/loop/scrub all operate on that one
// AnimationPlayer instance -- this plugin previews one clip on one demo
// body at a time, not a multi-clip timeline editor (a real, larger,
// separate feature this pass doesn't build). Preview lighting override
// comes for free from studio::PreviewScene's own built-in "studio
// lightbox" swap (see its render()'s comment) -- nothing extra needed
// here for that.
class AnimationPreviewerPlugin final : public IStudioPlugin {
public:
    AnimationPreviewerPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                              core::RiggedMeshLibrary& riggedMeshLibrary);

    [[nodiscard]] const char* name() const override { return "Animation Previewer"; }
    [[nodiscard]] const char* category() const override { return "Animation"; }

    // Ticked every frame regardless of isOpen() (see IStudioPlugin's
    // class comment) -- advances the AnimationPlayer by dt while
    // `playing_` (0 while paused, so a scrub-while-paused still
    // recomputes the pose without advancing time) and writes the
    // resulting skinningMatrices into the demo body's segments every
    // frame either way.
    void update(float dt, core::ECS& ecs, core::EntityId selected,
                const std::vector<core::EntityId>& selectedEntities) override;
    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Render hook, called once per frame from StudioApp's pre-pass
    // callback, only while this panel is open -- same convention as
    // AvatarPreviewer/CataloguePanel/UploadAvatarItemPlugin.
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);

    void shutdown(core::Renderer& renderer);

    // Real cross-plugin integration point: loads `clip` onto the demo
    // body and starts it playing (paused at frame 0, same as any
    // internally-triggered load -- see the .cpp), opening this panel so
    // the caller's action actually shows something. Used by this
    // plugin's own "Load Clip File" button, and by
    // studio::plugins::AvatarPreviewer's "equip an Emote" flow (see
    // core::playEquippedEmote()) to preview an emote on a real rigged
    // body -- the old capsule-mannequin AvatarPreviewer has no skeleton
    // of its own to animate (see AvatarPreviewer's class comment), so
    // handing the clip off here rather than duplicating a second rigged
    // body there is the honest, non-redundant integration.
    void previewClip(core::AnimationClip clip, std::string displayName);

private:
    void spawnDemoBody();
    void playFromStart();
    // Loads one of the 6 real shipped clips (engine/assets/animations/
    // idle|walk|run|jump_start|jump_air|jump_land.anim -- the same files
    // core::AvatarController's setIdleClip()/setWalkClip()/setRunClip()/
    // setJumpClip()/setJumpAirClip()/setJumpLandClip() are meant to be fed
    // in real gameplay) via previewClip(), resolving the real packaged-vs-
    // dev-build asset directory the same way Renderer/UIRenderer already
    // do for shaders (core::resolveResourceDir()) rather than a hardcoded
    // relative path.
    void loadShippedClip(const char* fileBaseName, const char* displayName);
    // Draws bone lines + joint markers over the just-rendered preview
    // image by projecting each joint's real current world position
    // (skinningMatrices()[i] applied to the joint's own bind-pose world
    // position -- the same "skinning matrix applied to a point sitting at
    // the joint's own pivot" reasoning tests/test_main.cpp's
    // testAnimationPlayerPlaybackAndPoseGeneration() already establishes)
    // through scene_'s real camera, using the exact image rect
    // drawAndHandleOrbit() just drew into (via ImGui::GetItemRectMin/Max,
    // the same pattern drawPanel()'s keyframe markers already use under
    // the playhead slider). Must be called immediately after
    // scene_.drawAndHandleOrbit(), before any other ImGui item is drawn.
    void drawSkeletonOverlay();

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::RiggedMeshLibrary* riggedMeshLibrary_;

    PreviewScene scene_;
    std::vector<core::EntityId> skinnedEntities_;
    core::AvatarLoadout emptyLoadout_;  // never equipped -- see class comment on why this demo body has no catalogue tie-in
    core::CatalogueIndex emptyIndex_;   // never rebuilt -- same reason

    core::Skeleton skeleton_;
    core::AnimationPlayer player_;

    core::AnimationClip currentClip_;
    std::string currentClipName_ = "(none)";
    core::AnimationPlayer::Handle playingHandle_ = core::AnimationPlayer::kInvalidHandle;
    bool playing_ = false;
    bool looping_ = true;

    char clipPathBuffer_[256] = "";
    std::string statusMessage_;

    bool showSkeletonOverlay_ = false;
    int selectedJointIndex_ = -1; // -1 = no bone selected
};

} // namespace engine::studio::plugins
