#include "studio/plugins/UploadAvatarItemPlugin.hpp"

#include <chrono>
#include <sstream>

#include <imgui.h>

#include "core/ObjLoader.hpp"
#include "core/Renderer.hpp"

namespace engine::studio::plugins {

namespace {

constexpr const char* kCategoryNames[] = {"Head",  "Hair",      "Face",           "Torso",
                                           "Legs", "Accessory", "LayeredClothing", "Emote"};
constexpr core::AvatarItemCategory kCategoryValues[] = {
    core::AvatarItemCategory::Head,  core::AvatarItemCategory::Hair,  core::AvatarItemCategory::Face,
    core::AvatarItemCategory::Torso, core::AvatarItemCategory::Legs,  core::AvatarItemCategory::Accessory,
    core::AvatarItemCategory::LayeredClothing, core::AvatarItemCategory::Emote,
};

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

UploadAvatarItemPlugin::UploadAvatarItemPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                                VkQueue queue, core::MeshLibrary& meshLibrary,
                                                core::TextureLibrary& textureLibrary, core::CatalogueDatabase& database,
                                                core::CatalogueIndex& index, std::string databaseFilePath)
    : allocator_(allocator),
      device_(device),
      cmdPool_(cmdPool),
      queue_(queue),
      meshLibrary_(&meshLibrary),
      textureLibrary_(&textureLibrary),
      database_(&database),
      index_(&index),
      databaseFilePath_(std::move(databaseFilePath)) {}

bool UploadAvatarItemPlugin::buildAndValidateDraft() {
    draft_.item.id = idBuffer_;
    draft_.item.name = nameBuffer_;
    draft_.item.category = kCategoryValues[categoryIndex_];
    draft_.item.tags = splitTags(tagsBuffer_);
    draft_.item.meshPath = meshPathBuffer_;
    draft_.item.texturePath = texturePathBuffer_;
    draft_.creatorId = creatorIdBuffer_;
    draft_.price = price_;

    std::string error;
    if (!draft_.item.validate(error)) {
        statusMessage_ = error;
        statusIsError_ = true;
        return false;
    }
    return true;
}

void UploadAvatarItemPlugin::refreshThumbnail() {
    if (!buildAndValidateDraft()) {
        hasThumbnail_ = false;
        return;
    }

    thumbnailScene_.reset();
    core::ObjLoadResult obj = core::loadObj(draft_.item.meshPath);
    if (!obj.succeeded) {
        statusMessage_ = "Thumbnail failed: " + obj.error;
        statusIsError_ = true;
        hasThumbnail_ = false;
        return;
    }
    core::Mesh mesh;
    if (!mesh.uploadFromHost(allocator_, device_, cmdPool_, queue_, obj.vertices, obj.indices)) {
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
    renderable.baseColor = draft_.item.baseColor;
    renderable.metallic = draft_.item.metallic;
    renderable.roughness = draft_.item.roughness;

    hasThumbnail_ = true;
    statusMessage_ = "Thumbnail rendered.";
    statusIsError_ = false;
}

void UploadAvatarItemPlugin::submitUpload() {
    if (!buildAndValidateDraft()) return;

    if (index_->findById(draft_.item.id) != nullptr) {
        // Not an error -- upsert semantics (see CatalogueDatabase::upsert's
        // comment): re-uploading the same id is a real, intentional update
        // path (a creator republishing a fixed version of their item), so
        // this is surfaced as informational, not blocked.
        statusMessage_ = "Updating existing catalogue entry \"" + draft_.item.id + "\"...";
    }

    draft_.uploadDateUnixSeconds =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    database_->upsert(draft_);
    if (!database_->saveToFile(databaseFilePath_)) {
        statusMessage_ = "Upload failed: could not write " + databaseFilePath_;
        statusIsError_ = true;
        return;
    }
    index_->upsert(draft_);

    statusMessage_ = "Uploaded \"" + draft_.item.name + "\" (id \"" + draft_.item.id + "\").";
    statusIsError_ = false;
}

void UploadAvatarItemPlugin::drawPanel(core::ECS& /*ecs*/, core::EntityId /*selected*/,
                                        const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Upload Item");

    ImGui::TextWrapped("Author a catalogue item's manifest, preview it, then upload it into the catalogue database.");
    ImGui::Separator();

    ImGui::InputText("Item Id", idBuffer_, sizeof(idBuffer_));
    ImGui::InputText("Name", nameBuffer_, sizeof(nameBuffer_));
    ImGui::Combo("Category", &categoryIndex_, kCategoryNames, IM_ARRAYSIZE(kCategoryNames));
    ImGui::InputText("Tags (comma-separated)", tagsBuffer_, sizeof(tagsBuffer_));
    ImGui::InputText("Mesh Path (.obj)", meshPathBuffer_, sizeof(meshPathBuffer_));
    ImGui::InputText("Texture Path (optional)", texturePathBuffer_, sizeof(texturePathBuffer_));
    ImGui::InputText("Creator Id", creatorIdBuffer_, sizeof(creatorIdBuffer_));
    ImGui::DragInt("Price (Robux)", &price_, 1.0f, 0, 1000000);

    ImGui::ColorEdit4("Base Color", &draft_.item.baseColor.x);
    ImGui::SliderFloat("Metallic", &draft_.item.metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &draft_.item.roughness, 0.045f, 1.0f);

    ImGui::Separator();
    if (ImGui::Button("Refresh Thumbnail")) refreshThumbnail();
    ImGui::SameLine();
    bool canUpload = idBuffer_[0] != '\0' && nameBuffer_[0] != '\0' && meshPathBuffer_[0] != '\0';
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
    ImGui::TextUnformatted("Thumbnail preview:");
    ImGui::BeginChild("upload_thumbnail", ImVec2(0.0f, 260.0f));
    if (hasThumbnail_) {
        thumbnailScene_.drawAndHandleOrbit();
    } else {
        ImGui::TextDisabled("Click \"Refresh Thumbnail\" once mesh/name/id are filled in.");
    }
    ImGui::EndChild();

    ImGui::End();
}

void UploadAvatarItemPlugin::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    if (!hasThumbnail_) return;
    thumbnailScene_.render(cmd, renderer, *meshLibrary_, *textureLibrary_);
}

void UploadAvatarItemPlugin::shutdown(core::Renderer& renderer) { thumbnailScene_.destroy(renderer, allocator_, device_); }

} // namespace engine::studio::plugins
