#pragma once

#include <string>
#include <vector>

#include "core/CatalogueDatabase.hpp"
#include "core/LocalProfile.hpp"
#include "marketplace/TransactionLog.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Kronos ("Creator Identity + Marketplace Publishing Pipeline" --
// "Creator Profile panel"; extended by "Creator Profiles v2"): a real,
// small Studio panel -- shows a creator's real creatorId (core::
// LocalProfile::creatorId, see that field's own comment), how many real
// catalogue entries are filed under it (core::CatalogueDatabase::
// entries(), filtered by creatorId), total real earnings/purchases
// (marketplace::TransactionLog::records(), filtered by sellerCreatorId --
// "total purchases" in the real, honest count-of-transactions sense; a
// real payout ledger/amount-owed reconciliation is explicitly out of
// scope here, same "no fabricated payout" discipline marketplace::
// TransactionLog's own header comment already establishes), and real
// average rating/total rating count aggregated across every one of that
// creator's own real catalogue entries.
//
// Kronos ("Creator Profiles v2" -- "View Profile" / "read-only for other
// creators"): real -- defaults to showing the *local* player's own
// profile (mutable identity, `localProfile_->creatorId`); studio::
// plugins::CataloguePanel's own "View Creator Profile" button calls
// viewCreatorProfile(otherCreatorId) to real-switch this panel to a real,
// different creatorId's stats instead. There's only ever one real,
// mutable identity in this codebase (LocalProfile, this machine's own
// profile) -- viewing someone else's real stats here is inherently
// read-only, simply because nothing on this screen writes to a
// *different* creator's data; there's no separate "am I allowed to edit
// this" check to build, the same "real, structural, not policed"
// contract core::AvatarLoadout's own "no duplicate categories" guarantee
// already establishes for a different invariant.
class CreatorProfilePanel final : public IStudioPlugin {
public:
    CreatorProfilePanel(core::LocalProfile& localProfile, core::CatalogueDatabase& database,
                         marketplace::TransactionLog& transactionLog);

    [[nodiscard]] const char* name() const override { return "Creator Profile"; }
    [[nodiscard]] const char* category() const override { return "Avatar"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

    // Real: switches this panel to display `creatorId`'s own real,
    // aggregated stats instead of the local player's own, and opens the
    // panel (setOpen(true)) so the caller's action actually shows
    // something. Passing an empty string real-reverts to "my own
    // profile" (localProfile_->creatorId).
    void viewCreatorProfile(const std::string& creatorId);

private:
    core::LocalProfile* localProfile_;
    core::CatalogueDatabase* database_;
    marketplace::TransactionLog* transactionLog_;
    // Empty = showing the local player's own profile (the real, honest
    // default) -- see viewCreatorProfile()'s own comment.
    std::string viewedCreatorId_;
};

} // namespace engine::studio::plugins
