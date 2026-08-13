#include "studio/plugins/UploadAnimationPlugin.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

#include <imgui.h>

#include "core/Components.hpp"
#include "core/Renderer.hpp"

namespace engine::studio::plugins {

namespace {

constexpr const char* kCategoryNames[] = {"Emote", "Locomotion", "Misc"};
constexpr core::AnimationCategory kCategoryValues[] = {core::AnimationCategory::Emote, core::AnimationCategory::Locomotion,
                                                         core::AnimationCategory::Misc};

std::vector<std::string> splitTags(const std::string& csv) {
    std::vector<std::string> tags;
    std::istringstream iss(csv);
    std::string tag;
    while (std::getline(iss, tag, ',')) {
        size_t start = tag.find_first_not_of(' ');
        size_t end = tag.find_last_not_of(' ');
        if (start == std::string::npos) continue;
        tags.push_back(tag.substr(start, end - start + 1));
    }
    return tags;
}

} // namespace

UploadAnimationPlugin::UploadAnimationPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                              VkQueue queue, core::MeshLibrary& meshLibrary,
                                              core::TextureLibrary& textureLibrary, core::AnimationDatabase& database,
                                              std::string databaseFilePath)
    : allocator_(allocator),
      device_(device),
      cmdPool_(cmdPool),
      queue_(queue),
      meshLibrary_(&meshLibrary),
      textureLibrary_(&textureLibrary),
      database_(&database),
      databaseFilePath_(std::move(databaseFilePath)),
      referenceSkeleton_(core::buildHumanoidSkeleton()) {}

bool UploadAnimationPlugin::buildAndValidateDraft() {
    draft_.item.id = idBuffer_;
    draft_.item.name = nameBuffer_;
    draft_.item.category = kCategoryValues[categoryIndex_];
    draft_.item.tags = splitTags(tagsBuffer_);
    draft_.item.clipPath = clipPathBuffer_;
    draft_.creatorId = creatorIdBuffer_;

    std::string error;
    if (!draft_.item.validate(error)) {
        statusMessage_ = error;
        statusIsError_ = true;
        hasClip_ = false;
        return false;
    }

    core::AnimationClip imported;
    if (!imported.loadFromFile(draft_.item.clipPath)) {
        statusMessage_ = "Failed to parse clip file: " + draft_.item.clipPath;
        statusIsError_ = true;
        hasClip_ = false;
        return false;
    }

    if (!core::validateAnimationClipAgainstSkeleton(imported, referenceSkeleton_, error)) {
        statusMessage_ = error;
        statusIsError_ = true;
        hasClip_ = false;
        return false;
    }

    draftClip_ = std::move(imported);
    draft_.item.looping = draftClip_.looping;
    draft_.item.durationSeconds = draftClip_.duration;
    hasClip_ = true;
    return true;
}

void UploadAnimationPlugin::refreshThumbnail() {
    if (!buildAndValidateDraft()) {
        hasThumbnail_ = false;
        return;
    }

    // A real pose snapshot, not a placeholder icon: plays draftClip_ on a
    // throwaway core::AnimationPlayer against referenceSkeleton_, seeks to
    // poseSnapshotTime_, ticks once (dt=0, just to make the player compute
    // the pose at that exact playhead -- see AnimationPreviewerPlugin::
    // update()'s identical dt=0 use), then bakes that one pose into plain
    // vertices via core::skinVerticesCPU(). This is exactly RiggedMesh.hpp's
    // documented reason skinVerticesCPU() exists at all: "a single static
    // frame doesn't need the GPU skinning pipeline stood up... uploads
    // through core::Mesh::uploadFromHost() like any static mesh" -- no
    // SkinnedRenderable, no RiggedMeshLibrary, no scene_skinned.vert
    // involved in a thumbnail at all.
    core::AnimationPlayer previewPlayer(referenceSkeleton_);
    auto handle = previewPlayer.play(draftClip_, core::AnimationLayer::Base, draftClip_.looping);
    previewPlayer.seek(handle, poseSnapshotTime_);
    previewPlayer.tick(0.0f);

    core::HumanoidMeshData bindMeshData = core::buildHumanoidMeshData(referenceSkeleton_);
    std::vector<core::Vertex> posedVertices =
        core::skinVerticesCPU(bindMeshData.vertices, bindMeshData.skinWeights, previewPlayer.skinningMatrices());

    thumbnailScene_.reset();
    core::Mesh mesh;
    if (!mesh.uploadFromHost(allocator_, device_, cmdPool_, queue_, posedVertices, bindMeshData.indices)) {
        statusMessage_ = "Thumbnail failed: GPU upload error";
        statusIsError_ = true;
        hasThumbnail_ = false;
        return;
    }
    uint32_t meshHandle = meshLibrary_->registerMesh(std::move(mesh));

    core::EntityId previewEntity =
        thumbnailScene_.ecs().createEntity(draft_.item.name.empty() ? "Draft" : draft_.item.name);
    auto& renderable = thumbnailScene_.ecs().addComponent<core::Renderable>(previewEntity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = {0.85f, 0.75f, 0.65f, 1.0f}; // the same neutral default RiggedAvatar.hpp's resolveSegmentColorsForLoadout() falls back to

    hasThumbnail_ = true;
    statusMessage_ = "Thumbnail rendered at t=" + std::to_string(poseSnapshotTime_) + "s.";
    statusIsError_ = false;
}

void UploadAnimationPlugin::submitUpload() {
    if (!buildAndValidateDraft()) return;

    if (database_->findById(draft_.item.id) != nullptr) {
        statusMessage_ = "Updating existing animation entry \"" + draft_.item.id + "\"...";
    }

    draft_.uploadDateUnixSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    database_->upsert(draft_);
    if (!database_->saveToFile(databaseFilePath_)) {
        statusMessage_ = "Upload failed: could not write " + databaseFilePath_;
        statusIsError_ = true;
        return;
    }

    statusMessage_ = "Uploaded \"" + draft_.item.name + "\" (id \"" + draft_.item.id + "\").";
    statusIsError_ = false;
}

void UploadAnimationPlugin::drawPanel(core::ECS& /*ecs*/, core::EntityId /*selected*/,
                                       const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Upload Animation");

    ImGui::TextWrapped(
        "Author an animation clip's manifest, import a .anim file, preview a pose, then upload it into the "
        "animation database.");
    ImGui::Separator();

    ImGui::InputText("Animation Id", idBuffer_, sizeof(idBuffer_));
    ImGui::InputText("Name", nameBuffer_, sizeof(nameBuffer_));
    ImGui::Combo("Category", &categoryIndex_, kCategoryNames, IM_ARRAYSIZE(kCategoryNames));
    ImGui::InputText("Tags (comma-separated)", tagsBuffer_, sizeof(tagsBuffer_));
    ImGui::InputText("Clip Path (.anim)", clipPathBuffer_, sizeof(clipPathBuffer_));
    ImGui::InputText("Creator Id", creatorIdBuffer_, sizeof(creatorIdBuffer_));

    if (hasClip_) {
        ImGui::Text("Loaded clip: \"%s\"  duration=%.2fs  looping=%s  tracks=%zu", draftClip_.name.c_str(),
                    draftClip_.duration, draftClip_.looping ? "yes" : "no", draftClip_.tracks.size());
        ImGui::SliderFloat("Thumbnail Pose Time", &poseSnapshotTime_, 0.0f, std::max(draftClip_.duration, 0.01f), "%.2fs");
    }

    ImGui::Separator();
    if (ImGui::Button("Refresh Thumbnail")) refreshThumbnail();
    ImGui::SameLine();
    bool canUpload = idBuffer_[0] != '\0' && nameBuffer_[0] != '\0' && clipPathBuffer_[0] != '\0';
    ImGui::BeginDisabled(!canUpload);
    if (ImGui::Button("Upload")) submitUpload();
    ImGui::EndDisabled();

    if (!statusMessage_.empty()) {
        if (statusIsError_) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", statusMessage_.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1.0f), "%s", statusMessage_.c_str());
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Pose snapshot preview:");
    ImGui::BeginChild("upload_animation_thumbnail", ImVec2(0.0f, 260.0f));
    if (hasThumbnail_) {
        thumbnailScene_.drawAndHandleOrbit();
    } else {
        ImGui::TextDisabled("Click \"Refresh Thumbnail\" once id/name/clip path are filled in.");
    }
    ImGui::EndChild();

    ImGui::End();
}

void UploadAnimationPlugin::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    if (!hasThumbnail_) return;
    thumbnailScene_.render(cmd, renderer, *meshLibrary_, *textureLibrary_);
}

void UploadAnimationPlugin::shutdown(core::Renderer& renderer) { thumbnailScene_.destroy(renderer, allocator_, device_); }

} // namespace engine::studio::plugins
