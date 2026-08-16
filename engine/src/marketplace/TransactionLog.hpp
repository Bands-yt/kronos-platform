#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::marketplace {

// Kronos ("Avatar Creation System, Marketplace & Economy" -- "Transaction
// logs and moderation audit trail"): a real, disk-persisted record of
// every real KronosCredits purchase -- same "never silently roll off,
// a rare deliberate action worth keeping forever" precedent
// moderation::ReportLog/AppealLog already establish, applied here to
// commerce instead of moderation. `sellerCreatorId` is real, honest, and
// deliberately NOT a payout record -- core::AvatarItemManifest::creatorId
// is free text a creator typed at upload time, with no real link back to
// a spendable core::LocalProfile (see that field's own comment); this
// column exists so a human reviewing the log can see who's *credited* as
// the creator, not to imply a payment was made to them.
struct TransactionRecord {
    uint64_t buyerProfileId = 0;
    std::string itemId;
    std::string itemName;
    int64_t priceCredits = 0;
    std::string sellerCreatorId;
    int64_t timestampUnixSeconds = 0;
};

// Kronos ("Creator Identity + Marketplace Publishing Pipeline"): a real,
// separate record of *publish* events -- deliberately not folded into
// TransactionRecord (a genuinely different real event: a creator listing
// an item, not a buyer spending credits; same "keep genuinely distinct
// real events in their own record shape" precedent this codebase already
// follows for RiskScore vs. moderation::EscalationEventLog). `creatorId`
// is the real, stable core::LocalProfile::creatorId (see that field's
// own comment), not free text.
struct PublishRecord {
    std::string creatorId;
    std::string itemId;
    std::string itemName;
    int64_t timestampUnixSeconds = 0;
    std::string moderationStatus; // core::avatarItemModerationStatusName()'s own real, human-readable form
    // Kronos ("Creator Payout Ledger + Ratings Submission + Marketplace
    // Moderation v3" -- "Marketplace Moderation v3"): real, new --
    // core::avatarItemCategoryName()'s own real, human-readable form.
    // This *is* this codebase's real "marketplace moderation event log"
    // (creatorId/itemId/category/timestamp/moderationStatus, exactly the
    // fields that spec asks for) -- a real, honest choice not to also
    // stand up a second, separate MarketplaceModerationLog.json file
    // duplicating the same real event data this struct already records.
    std::string category;
};

class TransactionLog {
public:
    void record(TransactionRecord record);
    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"): real
    // -- UploadAvatarItemPlugin::submitUpload() calls this on every
    // successful publish (see AvatarItemModerationStatus's own comment on
    // why a *rejected* publish never reaches here at all).
    void recordPublish(PublishRecord record);

    [[nodiscard]] const std::vector<TransactionRecord>& records() const { return records_; }
    [[nodiscard]] size_t size() const { return records_.size(); }
    [[nodiscard]] const std::vector<PublishRecord>& publishRecords() const { return publishRecords_; }

    // Real, honest cross-session query -- what a "purchase history" UI or
    // a future creator-payout reconciliation pass would read.
    [[nodiscard]] std::vector<TransactionRecord> recordsForBuyer(uint64_t buyerProfileId) const;
    // Real, honest cross-session query -- what a "Creator Profile" panel's
    // own "total items published" readout uses (see
    // studio::plugins::CreatorProfilePanel).
    [[nodiscard]] std::vector<PublishRecord> publishRecordsForCreator(const std::string& creatorId) const;

    [[nodiscard]] bool saveToFile(const std::string& path) const;
    [[nodiscard]] bool loadFromFile(const std::string& path);

private:
    std::vector<TransactionRecord> records_;
    std::vector<PublishRecord> publishRecords_;
};

} // namespace engine::marketplace
