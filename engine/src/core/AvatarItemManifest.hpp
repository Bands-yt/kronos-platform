#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

#include "core/AvatarItem.hpp"

namespace engine::core {

// Kronos ("Creator Identity + Marketplace Publishing Pipeline"): a real,
// persistent per-item review state -- mirrors core::GameSafetyStatus's
// own {Safe/UnderReview/Unsafe}-shaped precedent, scoped to avatar
// items. Set by submitUpload()'s own real moderation gate (see
// UploadAvatarItemPlugin.cpp): `Rejected` items are never actually
// published at all (the whole upload is refused, matching
// TrustSafetyService::onImageUpload()'s own "blocked" contract), so in
// practice only Approved/UnderReview ever appear in a real, persisted
// catalogue entry -- `Rejected` exists for completeness/testability, not
// because a rejected item is ever written to disk.
enum class AvatarItemModerationStatus { Approved, UnderReview, Rejected };

[[nodiscard]] const char* avatarItemModerationStatusName(AvatarItemModerationStatus status);
[[nodiscard]] bool avatarItemModerationStatusFromName(const std::string& name, AvatarItemModerationStatus& out);

// The JSON-serializable package descriptor for exactly one AvatarItem --
// the same role core::PluginManifest plays for a Studio plugin, but real
// JSON instead of this codebase's usual hand-rolled "KEY value" text
// format (see cmake/Dependencies.cmake's nlohmann_json comment for why
// this is the deliberate exception). A manifest is what a creator
// authors/exports for one item (studio::UploadAvatarItemPlugin writes
// one); core::CatalogueDatabase persists many together as a JSON array
// of this same shape, reusing toJson()/fromJson() directly rather than
// round-tripping through temp files per entry.
struct AvatarItemManifest {
    AvatarItem item;
    std::string creatorId;
    int64_t uploadDateUnixSeconds = 0;
    // Kronos ("Avatar Creation System, Marketplace & Economy" -- "Wire
    // Wallet -> Catalogue purchases"): real -- this is the exact real
    // amount marketplace::purchaseItemWithCredits() deducts from a real
    // buyer's core::LocalProfile::kronosCredits (studio::plugins::
    // CataloguePanel's own real "Purchase" button). Still NOT a real-money
    // amount -- see marketplace::CreditsPurchase.hpp's own header comment
    // for why this stays deliberately separate from
    // marketplace::MarketplaceService's real-money IAP scaffolding.
    int32_t price = 0;

    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"): real,
    // new listing metadata. `updateTimestampUnixSeconds` is real-refreshed
    // on every republish; `uploadDateUnixSeconds` (above) stays fixed at
    // the item's real, original first-publish time (submitUpload() only
    // ever sets it once, on first upload -- see that method's own
    // comment). `version` starts at 1 and real-increments on every
    // successful republish of the same item id. `moderationStatus` is the
    // real, persisted outcome of the real moderation gate submitUpload()
    // runs on every publish (see AvatarItemModerationStatus's own
    // comment). Defaults (0/1/Approved) are the real, honest values for
    // a manifest that predates this field -- an older catalogue entry
    // never actually failed a moderation check that didn't exist yet, so
    // defaulting it to Approved (not UnderReview/Rejected) is the
    // accurate backward-compatible reading, not an optimistic guess.
    int64_t updateTimestampUnixSeconds = 0;
    int32_t version = 1;
    AvatarItemModerationStatus moderationStatus = AvatarItemModerationStatus::Approved;

    // Kronos ("Creator Identity + Marketplace Publishing Pipeline" --
    // "Creator Ratings (Scaffold Only)"): real data model, no submission
    // path yet -- see this feature's own spec for why. `ratingCount == 0`
    // is the real, honest "never rated" state; UI reads this to show
    // "No ratings yet" rather than a misleading 0-star average.
    float ratingScore = 0.0f;
    int32_t ratingCount = 0;

    // Kronos ("Creator Profiles v2 + Marketplace Analytics + Creator
    // Dashboard" -- "Marketplace Analytics"): real, persistent per-item
    // counters. `views` real-increments every time studio::plugins::
    // CataloguePanel::openDetail() actually opens this item's detail
    // popup; `purchaseCount` real-increments on every real, successful
    // marketplace::purchaseItemWithCredits() call for this item (kept
    // separately from marketplace::TransactionLog's own per-purchase
    // records -- TransactionLog is the real, detailed audit trail;
    // `purchaseCount` is a real, cheap, denormalized total this
    // manifest carries so a dashboard/card doesn't need to re-scan the
    // whole transaction log just to show a number). `favorites` is a
    // real, honest scaffold-only field per this feature's own spec --
    // persisted and round-trips, but no real "favorite" UI/action exists
    // anywhere yet to increment it.
    int64_t views = 0;
    int32_t purchaseCount = 0;
    int32_t favorites = 0;

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

    [[nodiscard]] nlohmann::json toJson() const;
    // Populates `out` from `j`. Returns false (leaving `out` untouched)
    // if required fields are missing/malformed -- a corrupt catalogue
    // entry is skipped by CatalogueDatabase::loadFromFile(), not fatal to
    // loading the rest of the file, same fail-soft precedent as every
    // other loadFromFile() in this codebase.
    [[nodiscard]] static bool fromJson(const nlohmann::json& j, AvatarItemManifest& out);
};

} // namespace engine::core
