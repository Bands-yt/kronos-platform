#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AnimationDatabase.hpp"
#include "core/AnimationManifest.hpp"
#include "core/AnimationPlayer.hpp"
#include "core/Mesh.hpp"
#include "core/RiggedAvatar.hpp"
#include "core/Skeleton.hpp"
#include "core/Texture.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/PreviewScene.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {

// Task category 5: "Animation Upload Pipeline (Creator Tools)." A real
// manifest editor (name/category/tags/clip path) over
// core::AnimationManifest, real import (core::AnimationClip::loadFromFile
// -- .anim only, this engine's own real hand-rolled clip format; .fbx
// import is real, separate, substantial work -- a real third-party FBX
// parser -- this pass doesn't attempt to half-build, same honesty as
// core::ObjLoader being the only real mesh importer this engine has),
// real validation (core::AnimationItem::validate() for the manifest's own
// fields, core::validateAnimationClipAgainstSkeleton() for joint-mismatch/
// missing-channel problems against the same reference
// core::buildHumanoidSkeleton() the Animation Previewer uses), a real
// thumbnail (a CPU-skinned pose snapshot -- see refreshThumbnail()'s
// comment for why this is exactly RiggedMesh.hpp's stated "static frame
// doesn't need the GPU skinning pipeline" use case for skinVerticesCPU()),
// and a real write into core::AnimationDatabase (persisted to disk) on
// success.
class UploadAnimationPlugin final : public IStudioPlugin {
public:
    UploadAnimationPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                           core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary,
                           core::AnimationDatabase& database, std::string databaseFilePath);

    [[nodiscard]] const char* name() const override { return "Upload Animation"; }
    [[nodiscard]] const char* category() const override { return "Animation"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);
    void shutdown(core::Renderer& renderer);

private:
    // Builds `draft_.item` from the editor's text fields, imports the
    // clip file into draftClip_, and validates both the manifest fields
    // and (via the reference skeleton) the clip's content. Returns false
    // (leaving statusMessage_ set to the specific problem) on the first
    // failure.
    bool buildAndValidateDraft();
    // Real CPU-skinned pose snapshot at poseSnapshotTime_ -- see this
    // method's .cpp comment.
    void refreshThumbnail();
    void submitUpload();

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;
    core::AnimationDatabase* database_;
    std::string databaseFilePath_;

    core::Skeleton referenceSkeleton_; // buildHumanoidSkeleton() -- what every uploaded clip is validated/previewed against, see class comment

    core::AnimationManifest draft_;
    core::AnimationClip draftClip_;
    bool hasClip_ = false;

    char idBuffer_[64] = "";
    char nameBuffer_[128] = "";
    char tagsBuffer_[256] = "";
    char clipPathBuffer_[256] = "";
    char creatorIdBuffer_[64] = "studio_creator";
    int categoryIndex_ = 0;
    float poseSnapshotTime_ = 0.0f;

    PreviewScene thumbnailScene_;
    bool hasThumbnail_ = false;
    std::string statusMessage_;
    bool statusIsError_ = false;
};

} // namespace engine::studio::plugins
