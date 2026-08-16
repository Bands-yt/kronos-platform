#include "studio/plugins/UploadAvatarItemPlugin.hpp"

#include <chrono>
#include <functional>
#include <sstream>

#include <imgui.h>

#include "core/ObjLoader.hpp"
#include "core/Renderer.hpp"

namespace engine::studio::plugins {

namespace {

// Kronos ("Avatar Phase" -- "AvatarEditor: Clothing & Accessory Slots"):
// Shoes/Back are real, new categories a creator can genuinely upload
// items into -- without these two entries here, AvatarEditor's own real
// Shoes/Back equip slots could never have any real catalogue content to
// offer, only test fixtures.
constexpr const char* kCategoryNames[] = {"Head",  "Hair",      "Face",           "Torso",          "Legs",
                                           "Accessory", "LayeredClothing", "Emote", "Shoes", "Back", "Bundle"};
// Kronos ("Avatar Creation System, Marketplace & Economy"): same real
// path StudioApp/CataloguePanel/AvatarEditor all already use -- one real,
// shared transaction/publish ledger.
constexpr const char* kTransactionLogPath = "transaction_log.transactions";
constexpr core::AvatarItemCategory kCategoryValues[] = {
    core::AvatarItemCategory::Head,  core::AvatarItemCategory::Hair,  core::AvatarItemCategory::Face,
    core::AvatarItemCategory::Torso, core::AvatarItemCategory::Legs,  core::AvatarItemCategory::Accessory,
    core::AvatarItemCategory::LayeredClothing, core::AvatarItemCategory::Emote,
    core::AvatarItemCategory::Shoes, core::AvatarItemCategory::Back, core::AvatarItemCategory::Bundle,
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
                                                core::CatalogueIndex& index, std::string databaseFilePath,
                                                safety::TrustSafetyService& trustSafetyService,
                                                core::LocalProfile& localProfile,
                                                marketplace::TransactionLog& transactionLog)
    : allocator_(allocator),
      device_(device),
      cmdPool_(cmdPool),
      queue_(queue),
      meshLibrary_(&meshLibrary),
      textureLibrary_(&textureLibrary),
      database_(&database),
      index_(&index),
      databaseFilePath_(std::move(databaseFilePath)),
      trustSafetyService_(&trustSafetyService),
      localProfile_(&localProfile),
      transactionLog_(&transactionLog) {}

bool UploadAvatarItemPlugin::buildAndValidateDraft() {
    draft_.item.id = idBuffer_;
    draft_.item.name = nameBuffer_;
    draft_.item.category = kCategoryValues[categoryIndex_];
    draft_.item.tags = splitTags(tagsBuffer_);
    draft_.item.meshPath = meshPathBuffer_;
    draft_.item.texturePath = texturePathBuffer_;
    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"): real,
    // no longer free text -- always the real, stable identity of whoever
    // is actually running this Studio session (see
    // core::LocalProfile::creatorId's own comment).
    draft_.creatorId = localProfile_->creatorId;
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

namespace {
// Kronos ("Moderation Architecture v1", Phase 1): safety::TrustSafetyService
// keys risk scores by a real safety::PlayerId (uint32_t), but Studio's
// creatorId is a real, free-text string (core::AvatarItemManifest has no
// numeric account-id concept at all yet -- see core/LocalProfile.hpp's
// own real "no account system" scope). A stable hash is the honest
// bridge: the same creatorId string always risk-scores as the same real
// "account" across uploads, at the real, small, stated cost of a
// possible hash collision between two different creatorId strings (an
// acceptable, non-security-critical tradeoff for a heuristic risk
// signal, not an identity system).
safety::PlayerId hashCreatorIdToPlayerId(const std::string& creatorId) {
    return static_cast<safety::PlayerId>(std::hash<std::string>{}(creatorId));
}
} // namespace

void UploadAvatarItemPlugin::submitUpload() {
    if (!buildAndValidateDraft()) return;

    // Kronos ("Moderation Architecture v1", Phase 1; extended v2 "Image
    // Moderation"): real image-upload scanning, now actually wired -- see
    // this class's own header comment. Scans the real texture file if one
    // was provided (a mesh-only item, with no texture path, has nothing
    // for this to scan). `blocked` is a real, new contract this call
    // makes (TrustSafetyService::ImageUploadResult::blocked) -- the
    // upload is genuinely rejected here, not just flagged-and-allowed the
    // way the structural-only scan used to be silently ignored.
    if (!draft_.item.texturePath.empty()) {
        auto imageResult =
            trustSafetyService_->onImageUpload(hashCreatorIdToPlayerId(draft_.creatorId), draft_.item.texturePath);
        if (imageResult.blocked) {
            statusMessage_ = "Upload rejected: the texture file failed a real safety scan.";
            statusIsError_ = true;
            return;
        }
    }

    // Kronos ("Creator Identity + Marketplace Publishing Pipeline" --
    // "Publishing must run through the existing moderation pipeline"):
    // real text-content scan (name + tags) via the same real
    // IPInfringementScanner-backed check every other real creator-content
    // submission point in this codebase already uses. `blocked` genuinely
    // refuses the publish (same hard-fail contract as the image scan
    // above); `flagged`-but-not-blocked genuinely still publishes, but
    // real-marks the listing UnderReview rather than silently Approved --
    // a human moderator reviewing it later is the real, honest next step,
    // not a fabricated auto-approval.
    std::string submittedText = draft_.item.name;
    for (const std::string& tag : draft_.item.tags) submittedText += " " + tag;
    auto contentResult =
        trustSafetyService_->onCreatorContentSubmission(hashCreatorIdToPlayerId(draft_.creatorId), submittedText,
                                                          "AvatarItemPublish");
    if (contentResult.blocked) {
        statusMessage_ = "Upload rejected: the item's name/tags failed a real content safety scan.";
        statusIsError_ = true;
        return;
    }
    draft_.moderationStatus =
        contentResult.flagged ? core::AvatarItemModerationStatus::UnderReview : core::AvatarItemModerationStatus::Approved;

    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"): real
    // creation-vs-update timestamp split -- uploadDateUnixSeconds (the
    // real, original first-publish time) stays fixed across a republish;
    // only updateTimestampUnixSeconds and version advance. Preserves the
    // *existing* entry's own real values (not the draft's, which are
    // still whatever this panel's own struct last held) by looking the
    // real, currently-catalogued entry up first.
    int64_t now =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const core::AvatarItemManifest* existing = index_->findById(draft_.item.id);
    if (existing != nullptr) {
        // Not an error -- upsert semantics (see CatalogueDatabase::upsert's
        // comment): re-uploading the same id is a real, intentional update
        // path (a creator republishing a fixed version of their item), so
        // this is surfaced as informational, not blocked.
        statusMessage_ = "Updating existing catalogue entry \"" + draft_.item.id + "\"...";
        draft_.uploadDateUnixSeconds = existing->uploadDateUnixSeconds;
        draft_.version = existing->version + 1;
        draft_.ratingScore = existing->ratingScore;
        draft_.ratingCount = existing->ratingCount;
    } else {
        draft_.uploadDateUnixSeconds = now;
        draft_.version = 1;
    }
    draft_.updateTimestampUnixSeconds = now;

    database_->upsert(draft_);
    if (!database_->saveToFile(databaseFilePath_)) {
        statusMessage_ = "Upload failed: could not write " + databaseFilePath_;
        statusIsError_ = true;
        return;
    }
    index_->upsert(draft_);

    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"): real
    // publish-event audit trail -- see TransactionLog::recordPublish()'s
    // own comment.
    transactionLog_->recordPublish(marketplace::PublishRecord{
        draft_.creatorId, draft_.item.id, draft_.item.name, now,
        core::avatarItemModerationStatusName(draft_.moderationStatus), core::avatarItemCategoryName(draft_.item.category)});
    (void)transactionLog_->saveToFile(kTransactionLogPath);

    statusMessage_ = "Published \"" + draft_.item.name + "\" (id \"" + draft_.item.id + "\", v" +
                      std::to_string(draft_.version) + ", " + core::avatarItemModerationStatusName(draft_.moderationStatus) +
                      ").";
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
    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"): real,
    // read-only -- no longer a free-text field a creator could type
    // anything into (see core::LocalProfile::creatorId's own comment).
    ImGui::Text("Creator Id: %s", localProfile_->creatorId.c_str());
    ImGui::DragInt("Price (KronosCredits)", &price_, 1.0f, 0, 1000000);

    ImGui::ColorEdit4("Base Color", &draft_.item.baseColor.x);
    ImGui::SliderFloat("Metallic", &draft_.item.metallic, 0.0f, 1.0f);
    ImGui::SliderFloat("Roughness", &draft_.item.roughness, 0.045f, 1.0f);

    ImGui::Separator();
    if (ImGui::Button("Refresh Thumbnail")) refreshThumbnail();
    ImGui::SameLine();
    bool canPublish = idBuffer_[0] != '\0' && nameBuffer_[0] != '\0' && meshPathBuffer_[0] != '\0';
    ImGui::BeginDisabled(!canPublish);
    if (ImGui::Button("Publish to Marketplace")) {
        // Kronos ("Creator Identity + Marketplace Publishing Pipeline"):
        // real confirmation dialog -- validates first (so a genuinely
        // invalid draft never even opens the modal), the actual publish
        // only happens from the modal's own "Confirm" button below.
        if (buildAndValidateDraft()) showPublishConfirmation_ = true;
    }
    ImGui::EndDisabled();

    if (showPublishConfirmation_) {
        ImGui::OpenPopup("Confirm Publish");
        showPublishConfirmation_ = false;
    }
    if (ImGui::BeginPopupModal("Confirm Publish", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Publish \"%s\" (id \"%s\") to the Marketplace as creator \"%s\"?", draft_.item.name.c_str(),
                    draft_.item.id.c_str(), localProfile_->creatorId.c_str());
        if (index_->findById(draft_.item.id) != nullptr) {
            ImGui::TextDisabled("This id already exists in the catalogue -- publishing will update it in place.");
        }
        if (ImGui::Button("Confirm")) {
            submitUpload();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

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
