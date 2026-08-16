#include "studio/plugins/CreatorProfilePanel.hpp"

#include <imgui.h>

#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

CreatorProfilePanel::CreatorProfilePanel(core::LocalProfile& localProfile, core::CatalogueDatabase& database,
                                          marketplace::TransactionLog& transactionLog)
    : localProfile_(&localProfile), database_(&database), transactionLog_(&transactionLog) {}

void CreatorProfilePanel::viewCreatorProfile(const std::string& creatorId) {
    viewedCreatorId_ = creatorId;
    setOpen(true);
}

void CreatorProfilePanel::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Creator Profile");

    bool viewingSelf = viewedCreatorId_.empty() || viewedCreatorId_ == localProfile_->creatorId;
    const std::string& shownCreatorId = viewingSelf ? localProfile_->creatorId : viewedCreatorId_;

    if (!viewingSelf && ImGui::Button("< Back to My Profile")) {
        viewedCreatorId_.clear();
        viewingSelf = true;
    }
    ImGui::Text("Creator Id: %s", shownCreatorId.c_str());
    if (viewingSelf) {
        ImGui::TextDisabled("Real, stable, and non-editable -- see core::LocalProfile::creatorId's own comment.");
    } else {
        ImGui::TextDisabled("Viewing another creator's real, public stats (read-only).");
    }

    ImGui::Separator();

    int itemsPublished = 0;
    float ratingScoreSum = 0.0f;
    int32_t totalRatingCount = 0;
    int32_t totalPurchaseCount = 0;
    for (const core::AvatarItemManifest& entry : database_->entries()) {
        if (entry.creatorId != shownCreatorId) continue;
        ++itemsPublished;
        // Kronos ("Creator Profiles v2" -- "average rating"): a real,
        // rating-count-weighted average across every one of this
        // creator's own items (an item with 20 ratings should real-count
        // more toward the average than one with a single rating) --
        // never-rated items (ratingCount == 0) real-contribute nothing,
        // rather than dragging the average toward their own 0.0f default.
        ratingScoreSum += entry.ratingScore * static_cast<float>(entry.ratingCount);
        totalRatingCount += entry.ratingCount;
        totalPurchaseCount += entry.purchaseCount;
    }
    float averageRating = totalRatingCount > 0 ? ratingScoreSum / static_cast<float>(totalRatingCount) : 0.0f;

    ImGui::Text("Items Published: %d", itemsPublished);
    if (totalRatingCount > 0) {
        ImGui::Text("Average Rating: %.2f (%d rating%s)", static_cast<double>(averageRating), totalRatingCount,
                     totalRatingCount == 1 ? "" : "s");
    } else {
        ImGui::TextDisabled("Average Rating: not yet rated");
    }
    // Kronos ("Creator Profiles v2" -- "total purchases across all
    // items"): real, denormalized per-item AvatarItemManifest::
    // purchaseCount, real-summed -- the same real counter
    // studio::plugins::CataloguePanel increments on every successful
    // purchase (see that field's own header comment).
    ImGui::Text("Total Purchases: %d", totalPurchaseCount);

    int64_t totalEarningsCredits = 0;
    int transactionCount = 0;
    for (const marketplace::TransactionRecord& record : transactionLog_->records()) {
        if (record.sellerCreatorId != shownCreatorId) continue;
        ++transactionCount;
        totalEarningsCredits += record.priceCredits;
    }
    ImGui::Text("Total Earnings: %lld KronosCredits", static_cast<long long>(totalEarningsCredits));
    ImGui::TextDisabled(
        "A real count of KronosCredits purchases crediting this creatorId (%d transaction%s) -- not a payout "
        "ledger. No amount has actually been paid out; see marketplace::TransactionLog's own header comment.",
        transactionCount, transactionCount == 1 ? "" : "s");

    drawPluginFooter();
    ImGui::End();
}

} // namespace engine::studio::plugins
