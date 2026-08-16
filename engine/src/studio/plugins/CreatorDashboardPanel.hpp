#pragma once

#include <vector>

#include "core/CatalogueDatabase.hpp"
#include "core/LocalProfile.hpp"
#include "marketplace/TransactionLog.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Kronos ("Creator Profiles v2 + Marketplace Analytics + Creator
// Dashboard" -- "Creator Dashboard (Studio)"): a real, new Studio panel,
// distinct from CreatorProfilePanel -- that panel is the real, small
// "public-facing summary" (what "View Creator Profile" from
// CataloguePanel shows, including for someone else's creatorId); this
// one is the real, local-player-only, per-item operational view (every
// one of your own real items, real analytics per row, real global
// totals), the same "summary card vs. operational table" split a real
// storefront's own public seller page vs. seller dashboard already has.
// Always scoped to `localProfile_->creatorId` -- there is no "view
// someone else's dashboard" mode, by design (a dashboard is real,
// operational data, not a public profile).
class CreatorDashboardPanel final : public IStudioPlugin {
public:
    CreatorDashboardPanel(core::LocalProfile& localProfile, core::CatalogueDatabase& database,
                           marketplace::TransactionLog& transactionLog);

    [[nodiscard]] const char* name() const override { return "Creator Dashboard"; }
    [[nodiscard]] const char* category() const override { return "Avatar"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    core::LocalProfile* localProfile_;
    core::CatalogueDatabase* database_;
    marketplace::TransactionLog* transactionLog_;

    // Kronos ("Creator Dashboard" -- "Sort options"): indexes
    // kDashboardSortNames in the .cpp (Most Purchased/Most Viewed/
    // Highest Rated/Newly Published) -- a real, small, dashboard-local
    // sort concept, deliberately not core::CatalogueSearchFilter::
    // SortOrder (that one drives the public Catalogue grid over
    // *every* creator's items; this one only ever sorts one real
    // creator's own real items, and "Most Purchased"/"Most Viewed" both
    // need this panel's own real per-item aggregation regardless).
    int sortModeIndex_ = 0;
};

} // namespace engine::studio::plugins
