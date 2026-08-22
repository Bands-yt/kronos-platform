#include "studio/plugins/PublishingPanel.hpp"

#include "core/ResourcePaths.hpp"

#include <cstdio>
#include <filesystem>
#include <sstream>

#include <imgui.h>

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
    package.scene = sceneManager_->captureScene(ecs, *viewportCamera_);
    return package;
}

void PublishingPanel::drawMetadataSection() {
    if (!ImGui::CollapsingHeader("World Metadata", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::InputText("World Id", worldIdBuffer_, sizeof(worldIdBuffer_));
    helpMarker("A stable, creator-facing id -- never regenerated once published (same real convention Avatar Item ids already use).");
    ImGui::InputText("Version", versionBuffer_, sizeof(versionBuffer_));
    helpMarker("Real semantic version, e.g. \"1.0\" or \"1.0.0\".");
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

    ImGui::TextUnformatted("Real, independent camera -- does not move your edit viewport.");
    ImGui::DragFloat3("Camera Position", &thumbnailRig_.camera.position.x, 0.1f);
    ImGui::DragFloat("Yaw", &thumbnailRig_.camera.yawDegrees, 1.0f);
    ImGui::DragFloat("Pitch", &thumbnailRig_.camera.pitchDegrees, 1.0f, -89.0f, 89.0f);

    int modeIndex = captureMode_ == publishing::ThumbnailCaptureMode::Auto ? 0 : 1;
    const char* modes[] = {"Auto", "Manual"};
    if (ImGui::Combo("Capture Mode", &modeIndex, modes, 2)) {
        captureMode_ = modeIndex == 0 ? publishing::ThumbnailCaptureMode::Auto : publishing::ThumbnailCaptureMode::Manual;
    }
    helpMarker("Auto real-captures automatically once the camera has a real rendered frame ready. Manual waits for the button below.");

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
        std::string path = std::string(publishDirectoryBuffer_) + "/" + std::string(worldIdBuffer_) + "_thumbnail.ppm";
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
        ImGui::TextColored(ImVec4(0.90f, 0.30f, 0.30f, 1.0f), "%zu real validation error(s):", result.errors.size());
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

    core::PublishRequest request;
    request.slug = package.worldId;
    request.title = package.metadata.title;
    request.description = package.metadata.description;

    if (cloudPublishThread_.joinable()) cloudPublishThread_.join();
    cloudPublishInProgress_.store(true);
    cloudPublishSucceeded_ = false;
    cloudPublishStatus_ = "Publishing to Kronos...";

    cloudPublishThread_ = std::thread([this, request]() {
        // Restore the launcher's saved session if this Studio process
        // does not already have one -- signing in once covers both.
        if (!kronosApi_.isSignedIn()) (void)kronosApi_.restoreSession();
        core::PublishResult result = kronosApi_.publishGame(request);
        std::lock_guard<std::mutex> lock(cloudPublishMutex_);
        cloudPublishPendingResult_ = std::move(result);
        cloudPublishInProgress_.store(false);
    });
}

void PublishingPanel::drawCloudPublishSection(core::ECS& ecs) {
    // Drain the worker's result on the UI thread.
    {
        std::optional<core::PublishResult> result;
        {
            std::lock_guard<std::mutex> lock(cloudPublishMutex_);
            if (cloudPublishPendingResult_.has_value()) {
                result = std::move(cloudPublishPendingResult_);
                cloudPublishPendingResult_.reset();
            }
        }
        if (result.has_value()) {
            cloudPublishSucceeded_ = result->success;
            if (result->success) {
                cloudPublishStatus_ = result->status == "updated"
                                           ? "Updated \"" + result->slug + "\" in the Kronos catalogue."
                                           : "Published \"" + result->slug + "\" to the Kronos catalogue.";
                logMessage(cloudPublishStatus_);
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
        ImGui::TextDisabled("Host a server (Network Overlay) to publish into its real world registry.");
        return;
    }

    ImGui::Text("%zu world(s) currently in this server's real registry", networkSession_->worldRegistry().size());
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
        pendingThumbnailPath_ = std::string(publishDirectoryBuffer_) + "/" + std::string(worldIdBuffer_) + "_thumbnail.ppm";
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

    thumbnailRig_.destroy(renderer, renderer.allocator(), renderer.device());
}

} // namespace engine::studio::plugins
