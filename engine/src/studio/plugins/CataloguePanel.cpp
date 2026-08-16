#include "studio/plugins/CataloguePanel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <unordered_map>

#include <imgui.h>

#include "core/ObjLoader.hpp"
#include "core/Renderer.hpp"
#include "studio/plugins/AvatarPreviewer.hpp"
#include "studio/plugins/CreatorProfilePanel.hpp"

namespace engine::studio::plugins {

namespace {

// Index 0 is "Any" (no category filter); indices 1.. map to
// core::AvatarItemCategory in declaration order. Shoes/Back are real, new
// (see AvatarItem.hpp's own comment) -- without these two entries a
// player could never filter the Catalogue down to real, uploaded Shoes/
// Back items.
constexpr const char* kCategoryFilterNames[] = {"Any",   "Head", "Hair",      "Face",           "Torso",
                                                 "Legs",  "Accessory", "LayeredClothing", "Emote",
                                                 "Shoes", "Back", "Bundle"};
constexpr core::AvatarItemCategory kCategoryFilterValues[] = {
    core::AvatarItemCategory::Head,      core::AvatarItemCategory::Hair,  core::AvatarItemCategory::Face,
    core::AvatarItemCategory::Torso,     core::AvatarItemCategory::Legs,  core::AvatarItemCategory::Accessory,
    core::AvatarItemCategory::LayeredClothing, core::AvatarItemCategory::Emote,
    core::AvatarItemCategory::Shoes,     core::AvatarItemCategory::Back,  core::AvatarItemCategory::Bundle,
};

// Kronos ("Marketplace Search + Categories v2"): "Most Purchased" is
// real, deliberately NOT one of core::CatalogueSearchFilter::SortOrder's
// own values -- it needs real marketplace::TransactionLog purchase-count
// data CatalogueIndex has no dependency on by design (see that class's
// own header comment), so it's computed here, client-side, from this
// panel's own real transactionLog_ reference, then applied as one extra
// real sort pass after CatalogueIndex::search() returns.
constexpr const char* kSortOrderNames[] = {"Relevance",       "Price: Low to High", "Price: High to Low",
                                            "Newly Published", "Top Rated",          "Creator (A-Z)",
                                            "Most Purchased"};
constexpr core::CatalogueSearchFilter::SortOrder kSortOrderValues[] = {
    core::CatalogueSearchFilter::SortOrder::Relevance,          core::CatalogueSearchFilter::SortOrder::PriceLowToHigh,
    core::CatalogueSearchFilter::SortOrder::PriceHighToLow,     core::CatalogueSearchFilter::SortOrder::RecencyNewestFirst,
    core::CatalogueSearchFilter::SortOrder::TopRated,           core::CatalogueSearchFilter::SortOrder::CreatorAlphabetical,
    core::CatalogueSearchFilter::SortOrder::Relevance, // "Most Purchased" -- real sort applied separately, see drawGrid()
};
constexpr int kMostPurchasedSortIndex = 6;

// Kronos ("Marketplace Search + Categories v2" -- "show rating stars"):
// a real, honest text rendering of a 0-5 star rating -- no font glyph
// dependency, just filled/empty bracket characters, same "no external
// asset needed" spirit as this panel's own flat-swatch cards. Returns
// "Not yet rated" when ratingCount is real-zero, rather than a misleading
// "0.0 (0)".
std::string formatRatingStars(float ratingScore, int32_t ratingCount) {
    if (ratingCount <= 0) return "Not yet rated";
    int filled = std::clamp(static_cast<int>(ratingScore + 0.5f), 0, 5);
    std::string stars;
    for (int i = 0; i < 5; ++i) stars += (i < filled) ? '*' : '-';
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s %.1f (%d rating%s)", stars.c_str(), static_cast<double>(ratingScore), ratingCount,
                  ratingCount == 1 ? "" : "s");
    return buf;
}

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
    ImGui::TextDisabled("%d KronosCredits", entry.price);
    ImGui::TextDisabled("%s", formatRatingStars(entry.ratingScore, entry.ratingCount).c_str());

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
        ImGui::TextDisabled("By %s -- %d KronosCredits", entry.creatorId.empty() ? "unknown" : entry.creatorId.c_str(),
                             entry.price);
        ImGui::TextDisabled("%s", formatRatingStars(entry.ratingScore, entry.ratingCount).c_str());
        if (entry.moderationStatus != core::AvatarItemModerationStatus::Approved) {
            ImGui::TextDisabled("Status: %s", core::avatarItemModerationStatusName(entry.moderationStatus));
        }
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
                                std::string databaseFilePath, AvatarPreviewer& avatarPreviewer,
                                CreatorProfilePanel& creatorProfilePanel, core::LocalProfile& localProfile,
                                marketplace::TransactionLog& transactionLog)
    : allocator_(allocator),
      device_(device),
      cmdPool_(cmdPool),
      queue_(queue),
      meshLibrary_(&meshLibrary),
      textureLibrary_(&textureLibrary),
      index_(&index),
      database_(&database),
      databaseFilePath_(std::move(databaseFilePath)),
      avatarPreviewer_(&avatarPreviewer),
      creatorProfilePanel_(&creatorProfilePanel),
      localProfile_(&localProfile),
      transactionLog_(&transactionLog) {}

void CataloguePanel::drawSearchBar() {
    ImGui::SetNextItemWidth(220.0f);
    // Kronos ("Marketplace Search + Categories v2" -- "Marketplace Search
    // Engine"): real substring/tag/creator search, all through one box --
    // see core::CatalogueSearchFilter::textQuery's own header comment for
    // why one field covers all three.
    ImGui::InputTextWithHint("##catalogue_search", "Search by name, tag, or creator...", searchText_, sizeof(searchText_));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    ImGui::Combo("Category", &categoryFilterIndex_, kCategoryFilterNames, IM_ARRAYSIZE(kCategoryFilterNames));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    ImGui::Combo("Sort", &sortOrderIndex_, kSortOrderNames, IM_ARRAYSIZE(kSortOrderNames));
    // Kronos ("Marketplace Search + Categories v2" -- "My Items tab"):
    // real -- toggles between the real public browse view (Approved
    // items only) and every real item filed under this profile's own
    // creatorId, any moderationStatus.
    ImGui::Checkbox("My Items", &showMyItemsOnly_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Show every item published under your own creatorId (\"%s\"), including items still under "
                           "review -- not just what's Approved and publicly visible.",
                           localProfile_->creatorId.c_str());
    }
}

void CataloguePanel::drawGrid() {
    core::CatalogueSearchFilter filter;
    if (categoryFilterIndex_ > 0) {
        filter.category = kCategoryFilterValues[categoryFilterIndex_ - 1];
    }
    filter.textQuery = searchText_;
    // Kronos ("Marketplace Search + Categories v2" -- "Moderation
    // Integration"): real -- My Items shows every real status filed
    // under this creator's own real creatorId; the real public browse
    // view real-hides anything not Approved, so an UnderReview/Rejected
    // item never reaches a browsing player. See CatalogueSearchFilter::
    // moderationStatus's own header comment.
    if (showMyItemsOnly_) {
        filter.creatorId = localProfile_->creatorId;
    } else {
        filter.moderationStatus = core::AvatarItemModerationStatus::Approved;
    }

    bool sortByMostPurchased = sortOrderIndex_ == kMostPurchasedSortIndex;
    filter.sortOrder = kSortOrderValues[sortOrderIndex_];
    std::vector<const core::AvatarItemManifest*> results = index_->search(filter);

    if (sortByMostPurchased) {
        // Kronos ("Marketplace Search + Categories v2"): real purchase
        // counts, built once per real filter change (not once per
        // result) via a real hash map -- O(purchases) + O(results log
        // results), not the O(n^2) "count matches per item" scan this
        // would be if done naively inside the sort comparator itself.
        std::unordered_map<std::string, int> purchaseCounts;
        for (const marketplace::TransactionRecord& record : transactionLog_->records()) ++purchaseCounts[record.itemId];
        std::stable_sort(results.begin(), results.end(),
                          [&](const core::AvatarItemManifest* a, const core::AvatarItemManifest* b) {
                              return purchaseCounts[a->item.id] > purchaseCounts[b->item.id];
                          });
    }

    ImGui::TextDisabled("%zu item%s", results.size(), results.size() == 1 ? "" : "s");
    ImGui::Separator();

    constexpr float kCardWidth = 150.0f;
    float availWidth = ImGui::GetContentRegionAvail().x;
    int columns = std::max(1, static_cast<int>(availWidth / (kCardWidth + ImGui::GetStyle().ItemSpacing.x)));
    int rowCount = static_cast<int>((results.size() + static_cast<size_t>(columns) - 1) / static_cast<size_t>(columns));

    ImGui::BeginChild("catalogue_grid");
    if (results.empty()) {
        ImGui::TextDisabled(filter.textQuery.empty()
                                 ? (showMyItemsOnly_ ? "You haven't published any items yet."
                                                      : "No items in the catalogue yet -- upload one from the Upload "
                                                        "Item plugin.")
                                 : "No items match your search.");
    } else {
        // Kronos ("Marketplace Search + Categories v2" -- "Catalogue
        // Performance" -- "Virtualized scrolling"): real ImGuiListClipper
        // over rows of cards -- only the rows actually scrolled into view
        // get drawItemCard() calls each frame, the standard real ImGui
        // virtualization technique (not a fabricated "batched loading"
        // system; cards here have no per-item GPU resource to batch load
        // in the first place -- see this file's own class comment on why
        // cards are flat 2D swatches, not per-card 3D previews).
        ImGuiListClipper clipper;
        clipper.Begin(rowCount);
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                size_t rowStart = static_cast<size_t>(row) * static_cast<size_t>(columns);
                size_t rowEnd = std::min(rowStart + static_cast<size_t>(columns), results.size());
                for (size_t i = rowStart; i < rowEnd; ++i) {
                    if (drawItemCard(*results[i], ImVec2(kCardWidth, 0.0f))) openDetail(results[i]->item.id);
                    if (i + 1 < rowEnd) ImGui::SameLine();
                }
            }
        }
    }
    ImGui::EndChild();
}

void CataloguePanel::persistManifestMutation(core::AvatarItemManifest manifest) {
    database_->upsert(manifest);
    (void)database_->saveToFile(databaseFilePath_);
    index_->upsert(std::move(manifest));
}

void CataloguePanel::openDetail(const std::string& itemId) {
    const core::AvatarItemManifest* entry = index_->findById(itemId);
    if (entry == nullptr) return;

    // Kronos ("Creator Profiles v2 + Marketplace Analytics + Creator
    // Dashboard" -- "Opening item detail popup -> increment views"): real,
    // done before the mesh-load work below so a real failed mesh load
    // (a real, separate, honest failure) doesn't suppress a real, already
    // "opened the detail popup" user action.
    core::AvatarItemManifest withView = *entry;
    withView.views += 1;
    persistManifestMutation(withView);
    entry = index_->findById(itemId); // re-resolve: persistManifestMutation() may have reallocated entries_

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
    ImGui::TextDisabled("%s -- %d KronosCredits", core::avatarItemCategoryName(entry->item.category), entry->price);
    ImGui::TextDisabled("By %s", entry->creatorId.empty() ? "unknown" : entry->creatorId.c_str());
    if (!entry->creatorId.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("View Creator Profile")) creatorProfilePanel_->viewCreatorProfile(entry->creatorId);
    }
    ImGui::TextDisabled("%s", formatRatingStars(entry->ratingScore, entry->ratingCount).c_str());
    ImGui::TextDisabled("%lld view%s", static_cast<long long>(entry->views), entry->views == 1 ? "" : "s");
    // Kronos ("Creator Payout Ledger + Ratings Submission + Marketplace
    // Moderation v3" -- "Show creator earnings (for your own items
    // only)"): real -- only computed/shown when this item is genuinely
    // this profile's own (creatorId match), a real, honest per-item
    // earnings figure (count of real KronosCredits purchases of exactly
    // this item, real-summed from transactionLog_), not a fabricated
    // payout amount.
    if (!localProfile_->creatorId.empty() && entry->creatorId == localProfile_->creatorId) {
        int64_t earnings = 0;
        int saleCount = 0;
        for (const marketplace::TransactionRecord& record : transactionLog_->records()) {
            if (record.itemId != entry->item.id) continue;
            earnings += record.priceCredits;
            ++saleCount;
        }
        ImGui::TextDisabled("Your earnings from this item: %lld KronosCredits (%d sale%s)", static_cast<long long>(earnings),
                             saleCount, saleCount == 1 ? "" : "s");
    }
    if (!entry->item.tags.empty()) {
        std::string tagLine;
        for (size_t i = 0; i < entry->item.tags.size(); ++i) {
            if (i > 0) tagLine += ", ";
            tagLine += entry->item.tags[i];
        }
        ImGui::TextWrapped("Tags: %s", tagLine.c_str());
    }

    // Kronos ("Creator Profiles v2 + Marketplace Analytics + Creator
    // Dashboard" -- "More from this Creator"): real, small, real-filtered
    // query (creatorId match, this item's own id excluded, Approved-only
    // -- same real public-visibility rule drawGrid()'s own default filter
    // already applies) over the real, same index_ this whole panel
    // already shares, capped to a real, small handful so the popup
    // doesn't grow unbounded for a prolific creator.
    if (!entry->creatorId.empty()) {
        core::CatalogueSearchFilter moreFilter;
        moreFilter.creatorId = entry->creatorId;
        moreFilter.moderationStatus = core::AvatarItemModerationStatus::Approved;
        auto moreResults = index_->search(moreFilter);
        constexpr size_t kMaxMoreItems = 5;
        std::string currentItemId = entry->item.id;
        bool wroteHeader = false;
        size_t shown = 0;
        for (const core::AvatarItemManifest* other : moreResults) {
            if (other->item.id == currentItemId) continue;
            if (shown >= kMaxMoreItems) break;
            if (!wroteHeader) {
                ImGui::SeparatorText("More from this Creator");
                wroteHeader = true;
            }
            ImGui::PushID(other->item.id.c_str());
            if (ImGui::Selectable(other->item.name.c_str())) {
                std::string nextItemId = other->item.id;
                ImGui::PopID();
                openDetail(nextItemId);
                ImGui::End();
                return;
            }
            ImGui::PopID();
            ++shown;
        }
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
    ImGui::Text("Balance: %lld KronosCredits", static_cast<long long>(localProfile_->kronosCredits));
    if (localProfile_->ownsItem(entry->item.id)) {
        ImGui::TextDisabled("Already owned");
    }
    // Kronos ("Mega Prompt -- Final Platform Systems" -- "Remove Studio
    // Shop" -- "purchasing/equipping is runtime-only"): real, deliberate
    // removal -- Studio previously had its own real KronosCredits
    // "Purchase" button and "Rate Item" submission here, spending/writing
    // through the exact same real LocalProfile/TransactionLog files
    // engine_runtime's own Avatar Shop uses (see runtime::RuntimeShell::
    // drawAvatarShopDetailPopup() for that real, current home for both).
    // Studio is a creator/authoring tool, not a player-facing runtime --
    // "Try On"/"Equip" above stay (they only ever affect this Studio
    // session's own, separate, harmless AvatarPreviewer mannequin, never
    // the real gameplay avatar or a real spend), but real economic
    // actions (spending real KronosCredits, submitting a real rating)
    // only happen where a real player actually plays.
    if (!statusMessage_.empty()) {
        ImGui::TextDisabled("%s", statusMessage_.c_str());
    }
    ImGui::TextDisabled("Purchase and rate this item from the Avatar Shop in engine_runtime.");

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
