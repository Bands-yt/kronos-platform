#include "studio/plugins/CreatorDashboardPanel.hpp"

#include <algorithm>
#include <unordered_map>

#include <imgui.h>

#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

namespace {
constexpr const char* kDashboardSortNames[] = {"Most Purchased", "Most Viewed", "Highest Rated", "Newly Published"};
enum class DashboardSortMode { MostPurchased, MostViewed, HighestRated, NewlyPublished };
} // namespace

CreatorDashboardPanel::CreatorDashboardPanel(core::LocalProfile& localProfile, core::CatalogueDatabase& database,
                                              marketplace::TransactionLog& transactionLog)
    : localProfile_(&localProfile), database_(&database), transactionLog_(&transactionLog) {}

void CreatorDashboardPanel::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Creator Dashboard");

    if (localProfile_->creatorId.empty()) {
        ImGui::TextDisabled("No real creatorId yet -- publish an item from Upload Item first.");
        drawPluginFooter();
        ImGui::End();
        return;
    }

    // Kronos ("Creator Dashboard" -- "Performance" -- "Batched analytics
    // loading"): real, single O(transactions) pass building a real
    // itemId->earnings map, instead of re-scanning the whole real
    // transaction log once per item (which would be the real O(items *
    // transactions) this comment is warning against) -- the honest
    // "batching" a local, in-memory Alpha catalogue actually needs; see
    // this class's own header comment on why a separate cached-results
    // layer isn't built on top of this (nothing here is expensive enough
    // yet to justify one).
    std::unordered_map<std::string, int64_t> earningsByItemId;
    int64_t totalEarnings = 0;
    int totalPurchases = 0;
    for (const marketplace::TransactionRecord& record : transactionLog_->records()) {
        if (record.sellerCreatorId != localProfile_->creatorId) continue;
        earningsByItemId[record.itemId] += record.priceCredits;
        totalEarnings += record.priceCredits;
        ++totalPurchases;
    }

    std::vector<const core::AvatarItemManifest*> myItems;
    int64_t totalViews = 0;
    float ratingScoreSum = 0.0f;
    int32_t totalRatingCount = 0;
    for (const core::AvatarItemManifest& entry : database_->entries()) {
        if (entry.creatorId != localProfile_->creatorId) continue;
        myItems.push_back(&entry);
        totalViews += entry.views;
        ratingScoreSum += entry.ratingScore * static_cast<float>(entry.ratingCount);
        totalRatingCount += entry.ratingCount;
    }
    float averageRating = totalRatingCount > 0 ? ratingScoreSum / static_cast<float>(totalRatingCount) : 0.0f;

    ImGui::Text("Creator Id: %s", localProfile_->creatorId.c_str());
    ImGui::Text("Total Earnings: %lld KronosCredits", static_cast<long long>(totalEarnings));
    ImGui::Text("Total Purchases: %d", totalPurchases);
    ImGui::Text("Total Views: %lld", static_cast<long long>(totalViews));
    if (totalRatingCount > 0) {
        ImGui::Text("Average Rating: %.2f (%d rating%s)", static_cast<double>(averageRating), totalRatingCount,
                     totalRatingCount == 1 ? "" : "s");
    } else {
        ImGui::TextDisabled("Average Rating: not yet rated");
    }

    ImGui::Separator();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::Combo("Sort", &sortModeIndex_, kDashboardSortNames, IM_ARRAYSIZE(kDashboardSortNames));

    auto earningsFor = [&](const core::AvatarItemManifest* item) -> int64_t {
        auto it = earningsByItemId.find(item->item.id);
        return it != earningsByItemId.end() ? it->second : 0;
    };

    switch (static_cast<DashboardSortMode>(sortModeIndex_)) {
        case DashboardSortMode::MostPurchased:
            std::stable_sort(myItems.begin(), myItems.end(), [](const auto* a, const auto* b) {
                return a->purchaseCount > b->purchaseCount;
            });
            break;
        case DashboardSortMode::MostViewed:
            std::stable_sort(myItems.begin(), myItems.end(),
                              [](const auto* a, const auto* b) { return a->views > b->views; });
            break;
        case DashboardSortMode::HighestRated:
            std::stable_sort(myItems.begin(), myItems.end(), [](const auto* a, const auto* b) {
                if (a->ratingCount == 0 && b->ratingCount == 0) return false;
                if (a->ratingCount == 0) return false;
                if (b->ratingCount == 0) return true;
                return a->ratingScore > b->ratingScore;
            });
            break;
        case DashboardSortMode::NewlyPublished:
            std::stable_sort(myItems.begin(), myItems.end(), [](const auto* a, const auto* b) {
                return a->uploadDateUnixSeconds > b->uploadDateUnixSeconds;
            });
            break;
    }

    ImGui::TextDisabled("%zu item%s", myItems.size(), myItems.size() == 1 ? "" : "s");
    if (myItems.empty()) {
        ImGui::TextDisabled("You haven't published any items yet.");
        drawPluginFooter();
        ImGui::End();
        return;
    }

    // Kronos ("Creator Dashboard" -- "Performance" -- "Virtualized
    // scrolling"): real ImGuiListClipper over table rows -- same real
    // technique studio::plugins::CataloguePanel's own grid already uses,
    // applied here to a real ImGui table instead of a card grid.
    if (ImGui::BeginChild("dashboard_table_scroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders)) {
        if (ImGui::BeginTable("dashboard_items", 7,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Category");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Views");
            ImGui::TableSetupColumn("Purchases");
            ImGui::TableSetupColumn("Rating");
            ImGui::TableSetupColumn("Earnings");
            ImGui::TableHeadersRow();

            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(myItems.size()));
            while (clipper.Step()) {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                    const core::AvatarItemManifest* item = myItems[static_cast<size_t>(row)];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(item->item.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(core::avatarItemCategoryName(item->item.category));
                    ImGui::TableSetColumnIndex(2);
                    // Kronos ("Creator Dashboard" -- "Moderation
                    // Integration"): real -- shows this item's own real
                    // moderationStatus (Approved/UnderReview/Rejected)
                    // exactly as core::avatarItemModerationStatusName()
                    // names it; "Pending" in the spec's own wording is
                    // this codebase's real UnderReview value.
                    ImGui::TextUnformatted(core::avatarItemModerationStatusName(item->moderationStatus));
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%lld", static_cast<long long>(item->views));
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", item->purchaseCount);
                    ImGui::TableSetColumnIndex(5);
                    if (item->ratingCount > 0) {
                        ImGui::Text("%.1f (%d)", static_cast<double>(item->ratingScore), item->ratingCount);
                    } else {
                        ImGui::TextDisabled("Not yet rated");
                    }
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%lld", static_cast<long long>(earningsFor(item)));
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();

    drawPluginFooter();
    ImGui::End();
}

} // namespace engine::studio::plugins
