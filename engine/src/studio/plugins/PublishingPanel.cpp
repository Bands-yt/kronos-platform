#include "studio/plugins/PublishingPanel.hpp"

#include "core/NativeFileDialog.hpp"
#include "core/ResourcePaths.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <sstream>

#include <imgui.h>

#include "publishing/GamePackage.hpp"
#include "publishing/PackageArchive.hpp"
#include "publishing/PublishValidation.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
std::vector<std::string> splitCommaSeparated(const std::string& text) {
    std::vector<std::string> result;
    std::stringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        size_t start = token.find_first_not_of(' ');
        size_t end = token.find_last_not_of(' ');
        if (start == std::string::npos) continue;
        result.push_back(token.substr(start, end - start + 1));
    }
    return result;
}
} // namespace

PublishingPanel::PublishingPanel(core::SceneManager& sceneManager, core::Camera& viewportCamera, core::MeshLibrary& meshLibrary,
                                  core::TextureLibrary& textureLibrary, net::NetworkSession& networkSession)
    : sceneManager_(&sceneManager), viewportCamera_(&viewportCamera), meshLibrary_(&meshLibrary),
      textureLibrary_(&textureLibrary), networkSession_(&networkSession),
      // Same config.json > environment > localhost resolution the
      // launcher uses, so both point at one backend without Studio
      // needing its own setting.
      kronosApi_(core::loadKronosClientConfig(core::executableDirectory()).apiUrl) {
    thumbnailRig_.camera.position = glm::vec3(0.0f, 3.0f, 8.0f);
    thumbnailRig_.camera.yawDegrees = -90.0f;
    thumbnailRig_.camera.pitchDegrees = -15.0f;
}

void PublishingPanel::logMessage(const std::string& message) {
    publishLog_.push_back(message);
    if (publishLog_.size() > 200) publishLog_.erase(publishLog_.begin());
}

publishing::WorldPackage PublishingPanel::buildPackage(core::ECS& ecs) const {
    publishing::WorldPackage package;
    package.worldId = worldIdBuffer_;
    package.version = versionBuffer_;
    package.metadata.title = titleBuffer_;
    package.metadata.description = descriptionBuffer_;
    package.metadata.tags = splitCommaSeparated(tagsBuffer_);
    package.metadata.creatorName = creatorNameBuffer_;
    package.metadata.recommendedPlayerCount = recommendedPlayerCount_;
    package.metadata.category = static_cast<publishing::WorldCategory>(categoryIndex_);
    package.metadata.thumbnailPath = lastThumbnailPath_;
    package.scene = sceneManager_->captureWholeWorld(ecs, *viewportCamera_);
    return package;
}

void PublishingPanel::drawMetadataSection() {
    if (!ImGui::CollapsingHeader("World Metadata", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::InputText("World Id", worldIdBuffer_, sizeof(worldIdBuffer_));
    helpMarker("A stable ID that never changes once published.");
    ImGui::InputText("Version", versionBuffer_, sizeof(versionBuffer_));
    helpMarker("Semantic version, e.g. \"1.0\" or \"1.0.0\".");
    ImGui::InputText("Title", titleBuffer_, sizeof(titleBuffer_));
    ImGui::InputTextMultiline("Description", descriptionBuffer_, sizeof(descriptionBuffer_), ImVec2(0, 80));
    ImGui::InputText("Tags (comma-separated)", tagsBuffer_, sizeof(tagsBuffer_));
    ImGui::InputText("Creator Name", creatorNameBuffer_, sizeof(creatorNameBuffer_));
    ImGui::InputInt("Recommended Player Count", &recommendedPlayerCount_);
    const char* categories[] = {"Adventure", "Mining", "Horror", "Sandbox"};
    ImGui::Combo("Category", &categoryIndex_, categories, 4);
    ImGui::InputInt("Creator Player Id (for server registry)", &creatorPlayerId_);
}

void PublishingPanel::drawThumbnailSection() {
    if (!ImGui::CollapsingHeader("Thumbnail Camera", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextUnformatted("Independent camera; does not move your editing viewport.");
    ImGui::DragFloat3("Camera Position", &thumbnailRig_.camera.position.x, 0.1f);
    ImGui::DragFloat("Yaw", &thumbnailRig_.camera.yawDegrees, 1.0f);
    ImGui::DragFloat("Pitch", &thumbnailRig_.camera.pitchDegrees, 1.0f, -89.0f, 89.0f);

    int modeIndex = captureMode_ == publishing::ThumbnailCaptureMode::Auto ? 0 : 1;
    const char* modes[] = {"Auto", "Manual"};
    if (ImGui::Combo("Capture Mode", &modeIndex, modes, 2)) {
        captureMode_ = modeIndex == 0 ? publishing::ThumbnailCaptureMode::Auto : publishing::ThumbnailCaptureMode::Manual;
    }
    helpMarker("Auto captures as soon as the camera has rendered a frame. Manual waits for the button below.");

    ImGui::BeginChild("##thumbnail_preview", ImVec2(0, 260), true);
    if (thumbnailRig_.hasRenderedFrame()) {
        VkExtent2D extent = thumbnailRig_.extent();
        ImGui::Image(thumbnailRig_.imguiTextureId(), ImVec2(static_cast<float>(extent.width), static_cast<float>(extent.height)));
    } else {
        ImGui::TextDisabled("Rendering...");
    }
    ImGui::EndChild();

    if (ImGui::Button("Capture Thumbnail")) {
        // Real directory creation before the real write -- a bug this
        // sprint's own live testing found: captureThumbnailToFile()'s
        // std::ofstream doesn't create parent directories (matching
        // ofstream's own real behavior everywhere else in this
        // codebase), so a fresh Studio run with no real "published_worlds"
        // directory yet would silently fail every capture. See
        // WorldPackage::saveToDirectory()'s own real
        // create_directories() call for the same real requirement.
        std::error_code ec;
        std::filesystem::create_directories(publishDirectoryBuffer_, ec);
        std::string path = std::string(publishDirectoryBuffer_) + "/" + std::string(worldIdBuffer_) + "_thumbnail.png";
        // captureToFile() itself needs a live core::Renderer&, only
        // available from renderPreview()'s per-frame hook -- the button
        // just marks intent; the actual real capture happens the next
        // time renderPreview() runs, see that method's own comment.
        captureRequested_ = true;
        pendingThumbnailPath_ = path;
    }
    if (hasCapturedThumbnail_) ImGui::TextDisabled("Captured: %s", lastThumbnailPath_.c_str());
}

void PublishingPanel::drawValidationSection(core::ECS& ecs) {
    if (!ImGui::CollapsingHeader("Validation", ImGuiTreeNodeFlags_DefaultOpen)) return;

    publishing::WorldPackage package = buildPackage(ecs);
    publishing::PublishValidationResult result =
        publishing::validateForPublish(package.worldId, package.version, package.metadata, package.scene);

    if (result.valid) {
        ImGui::TextColored(ImVec4(0.35f, 0.80f, 0.40f, 1.0f), "Ready to publish.");
    } else {
        ImGui::TextColored(ImVec4(0.90f, 0.30f, 0.30f, 1.0f), "%zu validation error(s):", result.errors.size());
        for (const auto& error : result.errors) ImGui::BulletText("%s", error.c_str());
    }

    // Kronos ("Studio QoL Sprint" -- "flagging orphaned asset files"):
    // real, advisory (never blocks `result.valid` above) -- an orphaned
    // file is real hygiene/package-bloat feedback, not a correctness
    // failure the way a missing title or an absolute path is.
    ImGui::Spacing();
    ImGui::InputText("Asset Directory (for orphan scan)", assetDirectoryBuffer_, sizeof(assetDirectoryBuffer_));
    if (assetDirectoryBuffer_[0] != '\0') {
        std::vector<std::string> orphans =
            publishing::scanForOrphanedAssetFiles(assetDirectoryBuffer_, package.metadata, package.scene);
        if (orphans.empty()) {
            ImGui::TextDisabled("No orphaned asset files found under \"%s\".", assetDirectoryBuffer_);
        } else {
            ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.20f, 1.0f),
                                "%zu file(s) in this directory aren't referenced by anything:", orphans.size());
            for (const std::string& orphan : orphans) ImGui::BulletText("%s", orphan.c_str());
        }
    }
}


// Kronos ("One-Click Cloud Publishing"). Deliberately reuses the same
// validation the local Test Publish already runs: a place that would not
// package locally must not reach the public catalogue either.
void PublishingPanel::startCloudPublish(core::ECS& ecs) {
    if (cloudPublishInProgress_.load()) return;

    publishing::WorldPackage package = buildPackage(ecs);
    publishing::PublishValidationResult validation =
        publishing::validateForPublish(package.worldId, package.version, package.metadata, package.scene);
    if (!validation.valid) {
        cloudPublishSucceeded_ = false;
        cloudPublishStatus_ = "Fix " + std::to_string(validation.errors.size()) + " validation error(s) first.";
        logMessage("Publish to Kronos blocked by validation:");
        for (const auto& error : validation.errors) logMessage("  - " + error);
        return;
    }

    const std::string projectPath = projectPathProvider_ ? projectPathProvider_() : std::string();
    if (projectPath.empty()) {
        cloudPublishSucceeded_ = false;
        cloudPublishStatus_ = "Save this place as a project (File > Save Project) so players can download it.";
        return;
    }
    if (sceneManager_->isDirty()) {
        cloudPublishSucceeded_ = false;
        cloudPublishStatus_ = "Save your changes first -- players get the saved project.";
        return;
    }

    core::PublishRequest request;
    request.slug = package.worldId;
    request.title = package.metadata.title;
    request.description = package.metadata.description;

    core::GameManifest manifest;
    manifest.name = package.metadata.title;
    manifest.description = package.metadata.description;
    manifest.launchKind = core::GameLaunchKind::ProjectPath;
    manifest.projectPath = std::filesystem::path(projectPath).filename().string();
    const std::string gameDirectory = std::filesystem::path(projectPath).parent_path().string();

    if (cloudPublishThread_.joinable()) cloudPublishThread_.join();
    cloudPublishInProgress_.store(true);
    cloudPublishSucceeded_ = false;
    cloudPublishStatus_ = "Publishing to Kronos...";

    cloudPublishThread_ = std::thread([this, request, manifest, gameDirectory]() {
        // Restore the launcher's saved session if this Studio process
        // does not already have one -- signing in once covers both.
        if (!kronosApi_.isSignedIn()) (void)kronosApi_.restoreSession();
        CloudPublishOutcome outcome;
        core::PublishResult& result = outcome.publish;
        result = kronosApi_.publishGame(request);
        if (result.success) {
            std::error_code ec;
            std::string archivePath =
                (std::filesystem::temp_directory_path(ec) / ("kronos_publish_" + request.slug + ".kronos")).string();
            std::string error;
            if (!publishing::writeGameFolderArchive(gameDirectory, manifest, archivePath, error)) {
                result.success = false;
                result.error = "Listed, but not playable yet: " + error;
            } else {
                core::PackageUploadResult& upload = outcome.upload;
                upload = kronosApi_.uploadGamePackage(request.slug, archivePath, publishing::archiveSha256Hex(archivePath));
                if (!upload.success) {
                    result.success = false;
                    result.error = "Listed, but the game upload failed: " + upload.error;
                }
            }
            std::filesystem::remove(archivePath, ec);
        }
        std::lock_guard<std::mutex> lock(cloudPublishMutex_);
        cloudPublishPendingResult_ = std::move(outcome);
        cloudPublishInProgress_.store(false);
    });
}

void PublishingPanel::drawCloudPublishSection(core::ECS& ecs) {
    // Drain the worker's result on the UI thread.
    {
        std::optional<CloudPublishOutcome> outcome;
        {
            std::lock_guard<std::mutex> lock(cloudPublishMutex_);
            if (cloudPublishPendingResult_.has_value()) {
                outcome = std::move(cloudPublishPendingResult_);
                cloudPublishPendingResult_.reset();
            }
        }
        if (outcome.has_value()) {
            const core::PublishResult* result = &outcome->publish;
            cloudPublishSucceeded_ = result->success;
            if (result->success) {
                const std::string version = "version " + std::to_string(outcome->upload.versionNumber);
                if (outcome->upload.reviewStatus == "pending") {
                    cloudPublishStatus_ = "Uploaded \"" + result->slug + "\" " + version +
                                          ". It goes live once a moderator approves it.";
                } else {
                    cloudPublishStatus_ = (result->status == "updated" ? "Updated \"" : "Published \"") + result->slug +
                                          "\" -- " + version + " is live.";
                }
                logMessage(cloudPublishStatus_);
                runCatalogTask("Refreshing releases...", [] { return std::string(); });
            } else {
                // The backend's messages are written for a human, so they
                // are shown verbatim rather than replaced with "failed".
                cloudPublishStatus_ = result->error;
                logMessage("Publish to Kronos failed: " + result->error);
            }
        }
    }

    if (!ImGui::CollapsingHeader("Publish to Kronos", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextDisabled("Uploads this place to the public catalogue at %s", kronosApi_.baseUrl().c_str());
    ImGui::TextDisabled("Uses the Kronos account you signed into in the launcher.");
    ImGui::Dummy(ImVec2(0.0f, 6.0f));

    const bool busy = cloudPublishInProgress_.load();
    // A place with no id or title cannot be published, and disabling the
    // button says so before a round trip does.
    const bool hasIdentity = worldIdBuffer_[0] != '\0' && titleBuffer_[0] != '\0';

    ImGui::BeginDisabled(busy || !hasIdentity);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.698f, 0.349f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.075f, 0.788f, 0.420f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.0f, 0.588f, 0.290f, 1.0f));
    if (ImGui::Button(busy ? "Publishing..." : "Publish to Kronos", ImVec2(200.0f, 34.0f))) {
        startCloudPublish(ecs);
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();

    if (!hasIdentity) {
        ImGui::SameLine();
        ImGui::TextDisabled("Set a World ID and Title first.");
    }

    if (!cloudPublishStatus_.empty()) {
        const ImVec4 color = cloudPublishSucceeded_ ? ImVec4(0.0f, 0.698f, 0.349f, 1.0f)
                                                     : ImVec4(0.85f, 0.35f, 0.30f, 1.0f);
        ImGui::TextColored(color, "%s", cloudPublishStatus_.c_str());
    }
}

void PublishingPanel::runCatalogTask(std::string startedMessage, std::function<std::string()> task) {
    if (catalogBusy_.load() || worldIdBuffer_[0] == '\0') return;
    if (catalogThread_.joinable()) catalogThread_.join();
    catalogBusy_.store(true);
    const std::string slug = worldIdBuffer_;
    {
        std::lock_guard<std::mutex> lock(catalogMutex_);
        catalogStatus_ = std::move(startedMessage);
        catalogStatusIsError_ = false;
    }
    catalogThread_ = std::thread([this, slug, task = std::move(task)]() {
        if (!kronosApi_.isSignedIn()) (void)kronosApi_.restoreSession();
        std::string error = task();
        core::PackageVersionList versions = kronosApi_.fetchPackageVersions(slug);
        core::GameImageList images = kronosApi_.fetchGameImages(slug);
        std::lock_guard<std::mutex> lock(catalogMutex_);
        catalogSlug_ = slug;
        if (!error.empty()) {
            catalogStatus_ = error;
            catalogStatusIsError_ = true;
        } else if (!versions.success) {
            catalogStatus_ = versions.error;
            catalogStatusIsError_ = true;
        } else {
            catalogStatus_.clear();
            catalogStatusIsError_ = false;
        }
        versions_ = std::move(versions);
        images_ = std::move(images);
        catalogBusy_.store(false);
    });
}

namespace {
std::string formatBytes(uint64_t bytes) {
    char text[32];
    if (bytes >= (1ull << 30)) std::snprintf(text, sizeof(text), "%.2f GB", static_cast<double>(bytes) / (1ull << 30));
    else if (bytes >= (1ull << 20)) std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1ull << 20));
    else std::snprintf(text, sizeof(text), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    return text;
}

ImVec4 reviewColor(const std::string& status) {
    if (status == "approved") return ImVec4(0.35f, 0.80f, 0.40f, 1.0f);
    if (status == "rejected") return ImVec4(0.90f, 0.30f, 0.30f, 1.0f);
    return ImVec4(0.90f, 0.70f, 0.20f, 1.0f);
}

const char* reviewLabel(const std::string& status) {
    if (status == "approved") return "Approved";
    if (status == "rejected") return "Rejected";
    return "In review";
}
} // namespace

PublishingPanel::CatalogSnapshot PublishingPanel::catalogSnapshot() {
    std::lock_guard<std::mutex> lock(catalogMutex_);
    return {catalogSlug_ == worldIdBuffer_, versions_, images_, catalogStatus_, catalogStatusIsError_};
}

void PublishingPanel::drawReleasesSection() {
    if (!ImGui::CollapsingHeader("Releases")) return;
    const bool busy = catalogBusy_.load();
    const CatalogSnapshot snapshot = catalogSnapshot();
    const std::string slug = worldIdBuffer_;
    auto versionAction = [this, slug](std::string message, int number, bool activate) {
        runCatalogTask(std::move(message), [this, slug, number, activate] {
            core::CatalogActionResult r = activate ? kronosApi_.activatePackageVersion(slug, number)
                                                   : kronosApi_.deletePackageVersion(slug, number);
            return r.success ? std::string() : r.error;
        });
    };

    ImGui::BeginDisabled(busy || slug.empty());
    if (ImGui::Button(busy ? "Loading..." : "Refresh")) runCatalogTask("Loading releases...", [] { return std::string(); });
    ImGui::EndDisabled();
    if (!snapshot.status.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(snapshot.statusIsError ? ImVec4(0.90f, 0.30f, 0.30f, 1.0f) : ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s",
                           snapshot.status.c_str());
    }
    const core::PackageVersionList& list = snapshot.versions;
    if (!snapshot.current || !list.success) {
        ImGui::TextDisabled("Refresh to see the versions uploaded for \"%s\".", slug.c_str());
        return;
    }

    if (list.storage.quotaBytes > 0) {
        const double fraction = static_cast<double>(list.storage.usedBytes) / static_cast<double>(list.storage.quotaBytes);
        const std::string label =
            formatBytes(list.storage.usedBytes) + " of " + formatBytes(list.storage.quotaBytes) + " used";
        ImGui::ProgressBar(static_cast<float>(fraction), ImVec2(-1.0f, 0.0f), label.c_str());
    }
    if (list.gameReviewStatus != "approved") {
        ImGui::TextColored(reviewColor(list.gameReviewStatus), "Not listed yet: %s",
                           list.gameReviewStatus == "rejected" ? "the last submission was rejected."
                                                               : "waiting for the first approval.");
    }
    if (list.versions.empty()) {
        ImGui::TextDisabled("Nothing uploaded yet. Publish to Kronos to create version 1.");
        return;
    }
    if (!ImGui::BeginTable("##versions", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH)) return;
    ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Uploaded", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Review", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    for (const core::PackageVersion& version : list.versions) {
        const std::string label = "v" + std::to_string(version.versionNumber);
        std::string uploaded = version.createdAt.substr(0, 16);
        std::replace(uploaded.begin(), uploaded.end(), 'T', ' ');
        ImGui::PushID(version.versionNumber);
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        if (version.current) ImGui::TextColored(ImVec4(0.0f, 0.698f, 0.349f, 1.0f), "%s live", label.c_str());
        else ImGui::TextUnformatted(label.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(uploaded.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(formatBytes(version.sizeBytes).c_str());
        ImGui::TableNextColumn();
        ImGui::TextColored(reviewColor(version.reviewStatus), "%s%s", reviewLabel(version.reviewStatus),
                           version.reviewNote.empty() ? "" : " (?)");
        if (!version.reviewNote.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", version.reviewNote.c_str());
        ImGui::TableNextColumn();
        if (!version.current) {
            ImGui::BeginDisabled(busy);
            if (ImGui::SmallButton("...")) ImGui::OpenPopup("actions");
            ImGui::EndDisabled();
            if (ImGui::BeginPopup("actions")) {
                const bool canGoLive = version.reviewStatus == "approved";
                if (ImGui::MenuItem("Make live", nullptr, false, canGoLive)) {
                    versionAction("Making " + label + " live...", version.versionNumber, true);
                }
                if (!canGoLive && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Only approved versions can go live.");
                }
                if (ImGui::MenuItem("Delete version")) {
                    versionAction("Deleting " + label + "...", version.versionNumber, false);
                }
                ImGui::EndPopup();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

void PublishingPanel::drawStorePageSection() {
    if (!ImGui::CollapsingHeader("Store Page Images")) return;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("PNG or JPEG, up to 4096 px. Images are reviewed before players see them.");
    ImGui::PopStyleColor();
    const bool busy = catalogBusy_.load();
    const std::string slug = worldIdBuffer_;
    auto upload = [this, slug](const std::string& kind, const std::string& path) {
        runCatalogTask("Uploading " + kind + "...", [this, slug, kind, path] {
            core::GameImageUploadResult r = kronosApi_.uploadGameImage(slug, kind, path, publishing::archiveSha256Hex(path));
            return r.success ? std::string() : r.error;
        });
    };

    ImGui::BeginDisabled(busy || slug.empty() || !hasCapturedThumbnail_);
    if (ImGui::Button("Upload Thumbnail")) upload("thumbnail", lastThumbnailPath_);
    ImGui::EndDisabled();
    if (!hasCapturedThumbnail_ && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Capture one in Thumbnail Camera first.");
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(busy || slug.empty());
    if (ImGui::Button("Add Screenshot...")) {
        core::openFileDialogAsync({"Add Screenshot", {"*.png", "*.jpg", "*.jpeg"}, "Images"},
                                  [this, upload](const std::string& path) { upload("screenshot", path); });
    }
    ImGui::EndDisabled();

    const CatalogSnapshot snapshot = catalogSnapshot();
    if (!snapshot.current || !snapshot.images.success) {
        ImGui::TextDisabled("Refresh Releases to see this game's images.");
        return;
    }
    for (const core::GameImage& image : snapshot.images.images) {
        ImGui::PushID(image.id.c_str());
        ImGui::BulletText("%s %dx%d", image.kind == "thumbnail" ? "Thumbnail" : "Screenshot", image.width, image.height);
        ImGui::SameLine();
        ImGui::TextColored(reviewColor(image.reviewStatus), "%s", reviewLabel(image.reviewStatus));
        if (!image.reviewNote.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", image.reviewNote.c_str());
        ImGui::SameLine();
        ImGui::BeginDisabled(busy);
        if (ImGui::SmallButton("Remove")) {
            const std::string id = image.id;
            runCatalogTask("Removing image...", [this, slug, id] {
                core::CatalogActionResult r = kronosApi_.deleteGameImage(slug, id);
                return r.success ? std::string() : r.error;
            });
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

void PublishingPanel::drawTestPublishSection(core::ECS& ecs) {
    if (!ImGui::CollapsingHeader("Test Publish (local packaging)", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::InputText("Output Directory", publishDirectoryBuffer_, sizeof(publishDirectoryBuffer_));
    if (ImGui::Button("Test Publish")) {
        publishing::WorldPackage package = buildPackage(ecs);
        publishing::PublishValidationResult result =
            publishing::validateForPublish(package.worldId, package.version, package.metadata, package.scene);
        if (!result.valid) {
            logMessage("Test Publish failed validation (" + std::to_string(result.errors.size()) + " error(s)).");
            for (const auto& error : result.errors) logMessage("  - " + error);
        } else {
            std::string directory = std::string(publishDirectoryBuffer_) + "/" + package.worldId + "_v" + package.version;
            bool ok = package.saveToDirectory(directory);
            logMessage(ok ? "Test Publish succeeded: packaged to " + directory
                          : "Test Publish failed: could not write package to " + directory);
        }
    }
}

void PublishingPanel::drawServerRegistrySection(core::ECS& ecs) {
    if (!ImGui::CollapsingHeader("Publish to Server Registry")) return;
    if (!networkSession_->isServer()) {
        ImGui::TextDisabled("Host a server (Network Overlay) to publish to its world registry.");
        return;
    }

    ImGui::Text("%zu world(s) in this server\'s registry", networkSession_->worldRegistry().size());
    if (ImGui::Button("Publish to Registry")) {
        publishing::WorldPackage package = buildPackage(ecs);
        publishing::WorldListing listing;
        listing.worldId = package.worldId;
        listing.creatorId = static_cast<net::PlayerId>(creatorPlayerId_);
        listing.version = package.version;
        listing.metadata = package.metadata;

        publishing::PublishValidationResult result = networkSession_->publishWorld(listing);
        if (!result.valid) {
            logMessage("Registry publish failed (" + std::to_string(result.errors.size()) + " error(s)).");
            for (const auto& error : result.errors) logMessage("  - " + error);
        } else {
            logMessage("Registry publish succeeded: " + listing.worldId + " v" + listing.version);
        }
    }
}

void PublishingPanel::drawPublishLogSection() {
    if (!ImGui::CollapsingHeader("Publish Log")) return;
    ImGui::BeginChild("##publish_log_scroll", ImVec2(0, 150), true);
    for (const auto& line : publishLog_) ImGui::TextUnformatted(line.c_str());
    ImGui::EndChild();
    if (ImGui::Button("Clear Log")) publishLog_.clear();
}

void PublishingPanel::drawPanel(core::ECS& ecs, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Publishing");

    drawMetadataSection();
    drawThumbnailSection();
    drawValidationSection(ecs);
    drawTestPublishSection(ecs);
    drawCloudPublishSection(ecs);
    drawReleasesSection();
    drawStorePageSection();
    drawServerRegistrySection(ecs);
    drawPublishLogSection();

    drawPluginFooter();
    ImGui::End();
}

void PublishingPanel::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer, core::ECS& ecs) {
    thumbnailRig_.render(cmd, renderer, ecs, *meshLibrary_, *textureLibrary_);

    // Task 3's real Auto capture mode: the first time the rig has a real
    // rendered frame ready and nothing has been captured yet, trigger a
    // real capture automatically -- no button click required. Manual
    // mode never does this; the "Capture Thumbnail" button is the only
    // way to trigger one.
    if (captureMode_ == publishing::ThumbnailCaptureMode::Auto && !hasCapturedThumbnail_ && !captureRequested_ &&
        thumbnailRig_.hasRenderedFrame()) {
        std::error_code ec;
        std::filesystem::create_directories(publishDirectoryBuffer_, ec); // see the manual button's own comment on why this is real-required
        captureRequested_ = true;
        pendingThumbnailPath_ = std::string(publishDirectoryBuffer_) + "/" + std::string(worldIdBuffer_) + "_thumbnail.png";
    }

    if (captureRequested_ && thumbnailRig_.hasRenderedFrame()) {
        captureRequested_ = false;
        bool ok = thumbnailRig_.captureToFile(renderer, pendingThumbnailPath_);
        if (ok) {
            lastThumbnailPath_ = pendingThumbnailPath_;
            hasCapturedThumbnail_ = true;
            logMessage("Thumbnail captured: " + lastThumbnailPath_);
        } else {
            logMessage("Thumbnail capture FAILED: " + pendingThumbnailPath_);
        }
    }
}

void PublishingPanel::shutdown(core::Renderer& renderer) {
    // The worker captures `this`; it must not outlive the panel.
    if (cloudPublishThread_.joinable()) cloudPublishThread_.join();
    if (catalogThread_.joinable()) catalogThread_.join();

    thumbnailRig_.destroy(renderer, renderer.allocator(), renderer.device());
}

} // namespace engine::studio::plugins
