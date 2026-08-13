#pragma once

#include <string>

#include <volk.h>
#include <vk_mem_alloc.h>

#include "core/AssetCache.hpp"
#include "core/CatalogueDatabase.hpp"
#include "core/CatalogueIndex.hpp"
#include "studio/IStudioPlugin.hpp"
#include "studio/PreviewScene.hpp"

namespace engine::core {
class Renderer;
}

namespace engine::studio::plugins {

class AvatarPreviewer;

// Task category 3: "Catalogue Viewer (Studio + Runtime)." A scrollable
// grid of item cards over core::CatalogueIndex's search results (search
// bar: category/tag/sort), with a per-item detail popup that renders the
// item alone in its own studio::PreviewScene -- a second, independent
// isolated scene from AvatarPreviewer's own (see PreviewScene.hpp's
// class comment on why more than one of these can coexist safely).
//
// Card "thumbnails" are a flat 2D representation (a color swatch in the
// item's real baseColor, plus its name/category/price as text) rather
// than a live 3D render per card -- a real, stated scope decision: a
// grid can show dozens of cards at once, and a live PreviewScene per
// visible card (its own ECS + camera + OffscreenTarget + a
// drawSceneInto() call every frame) would multiply this panel's GPU cost
// by however many cards are on screen for very little benefit over a
// flat swatch at that size. The real 3D render (the actual PBR pipeline,
// orbit camera, the works) is one click away in the detail popup, and is
// what "Try On"/"Equip" ultimately show in the Avatar Previewer too --
// see drawDetailPopup().
//
// "Runtime" catalogue browsing (the other half of this task category's
// name) isn't a separate implementation here: this whole panel is built
// against core::CatalogueIndex/core::CatalogueDatabase, which are
// core:: (not studio::-only) types with zero Vulkan/ImGui dependency of
// their own -- an engine_runtime-side catalogue UI would query the same
// index the same way, it just doesn't have a UI framework wired up to
// draw one yet (engine_runtime has no ImGui at all, by design -- see
// docs/ARCHITECTURE.md's "no Studio-only privileges" principle). The
// data layer is shared; the runtime-side widget is not built in this
// pass.
class CataloguePanel final : public IStudioPlugin {
public:
    CataloguePanel(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                    core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary, core::CatalogueIndex& index,
                    core::CatalogueDatabase& database, AvatarPreviewer& avatarPreviewer);

    [[nodiscard]] const char* name() const override { return "Catalogue"; }
    [[nodiscard]] const char* category() const override { return "Avatar"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Render hook for the detail popup's own PreviewScene, called once
    // per frame from StudioApp's pre-pass callback, only while both this
    // panel and the detail popup are open.
    void renderPreview(VkCommandBuffer cmd, core::Renderer& renderer);
    void shutdown(core::Renderer& renderer);

private:
    void drawSearchBar();
    void drawGrid();
    void drawDetailPopup();
    void openDetail(const std::string& itemId);

    VmaAllocator allocator_;
    VkDevice device_;
    VkCommandPool cmdPool_;
    VkQueue queue_;
    core::MeshLibrary* meshLibrary_;
    core::TextureLibrary* textureLibrary_;
    core::CatalogueIndex* index_;
    core::CatalogueDatabase* database_;
    AvatarPreviewer* avatarPreviewer_;

    char searchText_[128] = "";
    int categoryFilterIndex_ = 0; // 0 = "Any" -- see kCategoryFilterNames in the .cpp
    int sortOrderIndex_ = 0;      // indexes core::CatalogueSearchFilter::SortOrder

    std::string detailItemId_; // empty = no detail popup open
    bool detailPopupOpen_ = false;
    PreviewScene detailScene_;
    core::AssetCache<uint32_t> detailMeshCache_;
    core::AssetCache<uint32_t> detailTextureCache_;
    std::string statusMessage_;
};

} // namespace engine::studio::plugins
