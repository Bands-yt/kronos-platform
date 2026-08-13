#include "studio/plugins/CataloguePanel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include <imgui.h>

#include "core/ObjLoader.hpp"
#include "core/Renderer.hpp"
#include "studio/plugins/AvatarPreviewer.hpp"

namespace engine::studio::plugins {

namespace {

std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Index 0 is "Any" (no category filter); indices 1.. map to
// core::AvatarItemCategory in declaration order.
constexpr const char* kCategoryFilterNames[] = {"Any",  "Head", "Hair",  "Face",  "Torso",
                                                 "Legs", "Accessory", "LayeredClothing", "Emote"};
constexpr core::AvatarItemCategory kCategoryFilterValues[] = {
    core::AvatarItemCategory::Head,      core::AvatarItemCategory::Hair,  core::AvatarItemCategory::Face,
    core::AvatarItemCategory::Torso,     core::AvatarItemCategory::Legs,  core::AvatarItemCategory::Accessory,
    core::AvatarItemCategory::LayeredClothing, core::AvatarItemCategory::Emote,
};

constexpr const char* kSortOrderNames[] = {"Relevance", "Price: Low to High", "Price: High to Low", "Newest First"};

// True 2D card visual -- a color swatch in the item's real baseColor
// plus name/category/price text, not a 3D render. See CataloguePanel.hpp's
// class comment for why. Returns true the frame the card is clicked.
bool drawItemCard(const core::AvatarItemManifest& entry, ImVec2 cardSize) {
    ImGui::PushID(entry.item.id.c_str());
    ImGui::BeginGroup();

    ImVec2 swatchSize(cardSize.x, cardSize.x * 0.7f);
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##card", ImVec2(cardSize.x, swatchSize.y));
    bool hovered = ImGui::IsItemHovered();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImU32 swatchColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(entry.item.baseColor.r, entry.item.baseColor.g, entry.item.baseColor.b, 1.0f));
    drawList->AddRectFilled(cursor, ImVec2(cursor.x + swatchSize.x, cursor.y + swatchSize.y), swatchColor,
                             ImGui::GetStyle().FrameRounding);
    ImU32 borderColor = ImGui::GetColorU32(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Border);
    drawList->AddRect(cursor, ImVec2(cursor.x + swatchSize.x, cursor.y + swatchSize.y), borderColor,
                       ImGui::GetStyle().FrameRounding, 0, hovered ? 2.0f : 1.0f);

    char categoryLabel[24];
    std::snprintf(categoryLabel, sizeof(categoryLabel), "%s", core::avatarItemCategoryName(entry.item.category));
    drawList->AddText(ImVec2(cursor.x + 4.0f, cursor.y + 4.0f), IM_COL32(20, 20, 24, 220), categoryLabel);

    ImGui::TextWrapped("%s", entry.item.name.c_str());
    ImGui::TextDisabled("%d Robux", entry.price);

    if (hovered) {
        ImGui::BeginTooltip();
        ImGui::Text("%s", entry.item.name.c_str());
        ImGui::TextDisabled("%s", core::avatarItemCategoryName(entry.item.category));
        if (!entry.item.tags.empty()) {
            std::string tagLine;
            for (size_t i = 0; i < entry.item.tags.size(); ++i) {
                if (i > 0) tagLine += ", ";
                tagLine += entry.item.tags[i];
            }
            ImGui::TextDisabled("Tags: %s", tagLine.c_str());
        }
        ImGui::TextDisabled("By %s -- %d Robux", entry.creatorId.empty() ? "unknown" : entry.creatorId.c_str(), entry.price);
        ImGui::EndTooltip();
    }

    ImGui::EndGroup();
    ImGui::PopID();
    return clicked;
}

} // namespace

CataloguePanel::CataloguePanel(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary,
                                core::CatalogueIndex& index, core::CatalogueDatabase& database,
                                AvatarPreviewer& avatarPreviewer)
    : allocator_(allocator),
      device_(device),
      cmdPool_(cmdPool),
      queue_(queue),
      meshLibrary_(&meshLibrary),
      textureLibrary_(&textureLibrary),
      index_(&index),
      database_(&database),
      avatarPreviewer_(&avatarPreviewer) {}

void CataloguePanel::drawSearchBar() {
    ImGui::SetNextItemWidth(220.0f);
    ImGui::InputTextWithHint("##catalogue_search", "Search by name...", searchText_, sizeof(searchText_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("Category", &categoryFilterIndex_, kCategoryFilterNames, IM_ARRAYSIZE(kCategoryFilterNames));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Sort", &sortOrderIndex_, kSortOrderNames, IM_ARRAYSIZE(kSortOrderNames));
}

void CataloguePanel::drawGrid() {
    core::CatalogueSearchFilter filter;
    if (categoryFilterIndex_ > 0) {
        filter.category = kCategoryFilterValues[categoryFilterIndex_ - 1];
    }
    filter.sortOrder = static_cast<core::CatalogueSearchFilter::SortOrder>(sortOrderIndex_);

    std::vector<const core::AvatarItemManifest*> results = index_->search(filter);

    std::string needle = toLower(searchText_);
    if (!needle.empty()) {
        results.erase(std::remove_if(results.begin(), results.end(),
                                      [&](const core::AvatarItemManifest* entry) {
                                          return toLower(entry->item.name).find(needle) == std::string::npos;
                                      }),
                       results.end());
    }

    ImGui::TextDisabled("%zu item%s", results.size(), results.size() == 1 ? "" : "s");
    ImGui::Separator();

    constexpr float kCardWidth = 150.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    int columns = std::max(1, static_cast<int>(availWidth / (kCardWidth + ImGui::GetStyle().ItemSpacing.x)));

    ImGui::BeginChild("catalogue_grid");
    for (size_t i = 0; i < results.size(); ++i) {
        if (drawItemCard(*results[i], ImVec2(kCardWidth, 0.0f))) {
            openDetail(results[i]->item.id);
        }
        if (static_cast<int>(i + 1) % columns != 0 && i + 1 < results.size()) ImGui::SameLine();
    }
    if (results.empty()) {
        ImGui::TextDisabled(needle.empty() ? "No items in the catalogue yet -- upload one from the Upload Item plugin."
                                            : "No items match your search.");
    }
    ImGui::EndChild();
}

void CataloguePanel::openDetail(const std::string& itemId) {
    const core::AvatarItemManifest* entry = index_->findById(itemId);
    if (entry == nullptr) return;

    detailItemId_ = itemId;
    detailPopupOpen_ = true;
    detailScene_.reset();

    core::ObjLoadResult obj = core::loadObj(entry->item.meshPath);
    if (!obj.succeeded) {
        statusMessage_ = "Preview failed: " + obj.error;
        return;
    }
    core::Mesh mesh;
    if (!mesh.uploadFromHost(allocator_, device_, cmdPool_, queue_, obj.vertices, obj.indices)) {
        statusMessage_ = "Preview failed: GPU upload error";
        return;
    }
    uint32_t meshHandle = meshLibrary_->registerMesh(std::move(mesh));
    detailMeshCache_.put(entry->item.meshPath, meshHandle);

    core::EntityId previewEntity = detailScene_.ecs().createEntity(entry->item.name);
    auto& renderable = detailScene_.ecs().addComponent<core::Renderable>(previewEntity);
    renderable.meshHandle = meshHandle;
    renderable.baseColor = entry->item.baseColor;
    renderable.metallic = entry->item.metallic;
    renderable.roughness = entry->item.roughness;
    statusMessage_.clear();
}

void CataloguePanel::drawDetailPopup() {
    if (!detailPopupOpen_) return;

    const core::AvatarItemManifest* entry = index_->findById(detailItemId_);
    if (entry == nullptr) {
        detailPopupOpen_ = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(480.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Item Detail", &detailPopupOpen_);

    ImGui::Text("%s", entry->item.name.c_str());
    ImGui::TextDisabled("%s -- %d Robux", core::avatarItemCategoryName(entry->item.category), entry->price);
    ImGui::TextDisabled("By %s", entry->creatorId.empty() ? "unknown" : entry->creatorId.c_str());
    if (!entry->item.tags.empty()) {
        std::string tagLine;
        for (size_t i = 0; i < entry->item.tags.size(); ++i) {
            if (i > 0) tagLine += ", ";
            tagLine += entry->item.tags[i];
        }
        ImGui::TextWrapped("Tags: %s", tagLine.c_str());
    }

    ImGui::Separator();
    ImGui::BeginChild("detail_preview", ImVec2(0.0f, 280.0f));
    detailScene_.drawAndHandleOrbit();
    ImGui::EndChild();
    ImGui::Separator();

    if (ImGui::Button("Try On")) {
        avatarPreviewer_->equipItem(entry->item.id, *index_, /*focusPanel=*/true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Equip")) {
        avatarPreviewer_->equipItem(entry->item.id, *index_, /*focusPanel=*/false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Purchase")) {
        // Stubbed per this pass's explicit scope (task item 3: "Add
        // 'Purchase' button (stubbed for now)") -- no real economy
        // wiring, same honesty level as marketplace::MarketplaceService's
        // own "routing layer, not a real payment flow" stated scope.
        statusMessage_ = "Purchase is not implemented yet.";
    }
    if (!statusMessage_.empty()) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }

    ImGui::End();

    if (!detailPopupOpen_) detailItemId_.clear();
}

void CataloguePanel::drawPanel(core::ECS& /*ecs*/, core::EntityId /*selected*/,
                                const std::vector<core::EntityId>& /*selectedEntities*/) {
    ImGui::Begin("Catalogue");
    drawSearchBar();
    ImGui::Separator();
    drawGrid();
    ImGui::End();

    drawDetailPopup();
}

void CataloguePanel::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer) {
    if (!detailPopupOpen_) return;
    detailScene_.render(cmd, renderer, *meshLibrary_, *textureLibrary_);
}

void CataloguePanel::shutdown(core::Renderer& renderer) { detailScene_.destroy(renderer, allocator_, device_); }

} // namespace engine::studio::plugins
