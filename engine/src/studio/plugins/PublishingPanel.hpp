#pragma once

#include <string>
#include <vector>

#include "core/Camera.hpp"
#include "core/SceneManager.hpp"
#include "net/NetworkSession.hpp"
#include "publishing/ThumbnailCapture.hpp"
#include "publishing/WorldPackage.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/ThumbnailCameraRig.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {

// Sprint 13 ("Publishing & Game Packaging") task 5's real "Publish
// World" UI: metadata fields (title/description/tags/creator/
// recommended player count/category), a real thumbnail camera + capture
// preview (studio::ThumbnailCameraRig, rendering the actual live ECS,
// not a draft item in isolation -- see that class's own header
// comment), real validation (publishing::validateForPublish(), shown as
// specific real error messages, not a bare pass/fail), a real "Test
// Publish" mode that captures the live scene into a real
// publishing::WorldPackage and saves it to a real local directory, and
// -- when this Studio session is hosting (net::NetworkSession::isServer()) --
// a real "Publish to Server Registry" action
// (net::NetworkSession::publishWorld()).
//
// Same "plugin holds a reference to state StudioApp already owns"
// pattern plugins::NetworkOverlayPlugin/ModerationPanel already
// established -- this one needs more of it (SceneManager for capturing
// the live scene, the viewport's own Camera for consistency with what
// "Save Scene" already captures, MeshLibrary/TextureLibrary for its own
// real GPU thumbnail render, and NetworkSession for the registry
// publish action).
class PublishingPanel final : public IStudioPlugin {
public:
    PublishingPanel(core::SceneManager& sceneManager, core::Camera& viewportCamera, core::MeshLibrary& meshLibrary,
                     core::TextureLibrary& textureLibrary, net::NetworkSession& networkSession);

    [[nodiscard]] const char* name() const override { return "Publishing"; }
    [[nodiscard]] const char* category() const override { return "World"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Render hook for the thumbnail camera rig, called once per frame
    // from StudioApp's pre-pass callback, only while this panel is open
    // -- same real convention UploadAvatarItemPlugin's renderPreview()
    // already established.
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer, core::ECS& ecs);
    void shutdown(core::Renderer& renderer);

private:
    void drawMetadataSection();
    void drawThumbnailSection();
    void drawValidationSection(core::ECS& ecs);
    void drawTestPublishSection(core::ECS& ecs);
    void drawServerRegistrySection(core::ECS& ecs);
    void drawPublishLogSection();

    [[nodiscard]] publishing::WorldPackage buildPackage(core::ECS& ecs) const;
    void logMessage(const std::string& message);

    core::SceneManager* sceneManager_;
    core::Camera* viewportCamera_;
    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;
    net::NetworkSession* networkSession_;

    char worldIdBuffer_[64] = "";
    char versionBuffer_[32] = "1.0.0";
    char titleBuffer_[128] = "";
    char descriptionBuffer_[1024] = "";
    char tagsBuffer_[256] = ""; // comma-separated, split on build
    char creatorNameBuffer_[128] = "";
    int recommendedPlayerCount_ = 4;
    int categoryIndex_ = 3; // Sandbox, matching WorldMetadata's own default
    int creatorPlayerId_ = 1; // manually supplied, same real convention UploadAvatarItemPlugin's creatorIdBuffer_ already established

    char publishDirectoryBuffer_[256] = "published_worlds";

    ThumbnailCameraRig thumbnailRig_;
    bool hasCapturedThumbnail_ = false;
    std::string lastThumbnailPath_;
    // Set by the "Capture Thumbnail" button (drawPanel(), no live
    // core::Renderer& available there); consumed by renderPreview() on
    // the next real pre-pass callback, the one place a live Renderer&
    // actually is -- see renderPreview()'s own comment.
    bool captureRequested_ = false;
    std::string pendingThumbnailPath_;
    // Task 3's "Add auto-capture + manual capture modes": Manual (the
    // default) requires the explicit "Capture Thumbnail" button; Auto
    // real-triggers a capture automatically the first time the thumbnail
    // rig has a real rendered frame ready and none has been captured yet
    // -- see renderPreview()'s own comment for exactly where.
    publishing::ThumbnailCaptureMode captureMode_ = publishing::ThumbnailCaptureMode::Manual;

    std::vector<std::string> publishLog_;
};

} // namespace engine::studio::plugins
