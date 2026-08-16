#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AvatarItemManifest.hpp"
#include "core/CatalogueDatabase.hpp"
#include "core/CatalogueIndex.hpp"
#include "core/LocalProfile.hpp"
#include "marketplace/TransactionLog.hpp"
#include "safety/TrustSafetyService.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/PreviewScene.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {

// Task category 6: "Catalogue Upload Pipeline (Creator Tools)." A real
// manifest editor (name/category/tags/price/mesh+texture paths/material
// sliders) over core::AvatarItemManifest, real upload validation
// (core::AvatarItem::validate() -- missing mesh, missing texture,
// invalid category all get caught here before anything is written), a
// real thumbnail render (the draft item alone in its own
// studio::PreviewScene, the exact same real PBR pipeline every other
// preview in this system uses), and a real write into
// core::CatalogueDatabase (persisted to disk) plus core::CatalogueIndex
// (so it's searchable in the Catalogue panel immediately, not just after
// a restart) on success.
class UploadAvatarItemPlugin final : public IStudioPlugin {
public:
    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"):
    // `localProfile`/`transactionLog` are real, new, injected the same
    // "caller owns identity, this panel just gets a reference" shape
    // every other real StudioApp-owned identity/ledger reference already
    // uses here -- `localProfile` supplies the real, stable creatorId a
    // publish is now filed under (replacing the old free-text field, see
    // that member's own comment), `transactionLog` is where a real
    // publish event is recorded (see TransactionLog::recordPublish()).
    UploadAvatarItemPlugin(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                            core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary,
                            core::CatalogueDatabase& database, core::CatalogueIndex& index,
                            std::string databaseFilePath, safety::TrustSafetyService& trustSafetyService,
                            core::LocalProfile& localProfile, marketplace::TransactionLog& transactionLog);

    [[nodiscard]] const char* name() const override { return "Upload Item"; }
    [[nodiscard]] const char* category() const override { return "Avatar"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Render hook for the thumbnail's own PreviewScene, called once per
    // frame from StudioApp's pre-pass callback, only while this panel is
    // open.
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);
    void shutdown(core::Renderer& renderer);

private:
    // Builds `draft_.item` from the editor's text fields, validates it,
    // and (on success) loads its mesh into thumbnailScene_ for a real
    // render. Returns false (leaving statusMessage_ set to the specific
    // problem) without touching thumbnailScene_ on validation failure.
    bool buildAndValidateDraft();
    void refreshThumbnail();
    void submitUpload();

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;
    core::CatalogueDatabase* database_;
    core::CatalogueIndex* index_;
    std::string databaseFilePath_; // where a successful upload persists the whole database to disk
    // Kronos ("Moderation Architecture v1", Phase 1): real image-upload
    // scanning (AssetSafetyGuard's structural checks -- see
    // TrustSafetyService::onImageUpload()'s own comment for exactly what
    // that does and doesn't catch) -- previously fully implemented but
    // never actually called from anywhere in this codebase.
    safety::TrustSafetyService* trustSafetyService_;
    core::LocalProfile* localProfile_;
    marketplace::TransactionLog* transactionLog_;

    core::AvatarItemManifest draft_;
    char idBuffer_[64] = "";
    char nameBuffer_[128] = "";
    char tagsBuffer_[256] = ""; // comma-separated, split into draft_.item.tags on build
    char meshPathBuffer_[256] = "";
    char texturePathBuffer_[256] = "";
    int categoryIndex_ = 0;
    int price_ = 0;
    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"): real
    // confirmation-dialog state -- "Publish to Marketplace" opens a real
    // ImGui modal (drawn from drawPanel()) rather than publishing
    // immediately on click; the modal's own "Confirm" button is the one
    // real call site that invokes submitUpload().
    bool showPublishConfirmation_ = false;

    PreviewScene thumbnailScene_;
    bool hasThumbnail_ = false;
    std::string statusMessage_;
    bool statusIsError_ = false;
};

} // namespace engine::studio::plugins
