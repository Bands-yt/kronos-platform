#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "safety/AssetSafetyGuard.hpp"
#include "safety/CreatorIdentityGuard.hpp"
#include "safety/IPInfringementScanner.hpp"

namespace engine::marketplace {

using PlayerId = uint32_t;

// A creator-submitted catalog listing, prior to it existing anywhere
// real: there is no catalog, database, or storefront UI in this codebase
// yet for a listing to actually live in (MarketplaceService.hpp is the
// *purchase*/payment-routing half of docs/ARCHITECTURE.md §9, not the
// catalog half). This struct is purely the input ListingReviewPipeline
// scans -- not a persisted entity.
struct ListingSubmission {
    PlayerId creator = 0;
    std::string title;
    std::string description;
    std::string sellerDisplayName;
    std::vector<uint8_t> thumbnailImageBytes; // raw file bytes; empty if no thumbnail was provided
    std::string thumbnailExtension;           // e.g. "png" -- ignored if thumbnailImageBytes is empty
};

enum class ListingReviewVerdict {
    Approved,         // nothing flagged -- safe to publish automatically
    NeedsHumanReview, // flagged, ambiguous signal(s) -- hold for a human moderator, don't auto-publish OR auto-reject
    Rejected,         // at least one blocking signal -- must not publish
};

struct ListingReviewFinding {
    std::string source; // which scanner + which field, e.g. "IPInfringementScanner:title"
    std::string description;
    bool blocking = false;
};

struct ListingReviewReport {
    ListingReviewVerdict verdict = ListingReviewVerdict::Approved;
    std::vector<ListingReviewFinding> findings;
};

// Scaffolding for docs/ARCHITECTURE.md §9's marketplace listing review
// step, composing every content-safety scanner this codebase has into a
// single submit-a-listing -> get-a-verdict pipeline:
//   - title + description  -> safety::IPInfringementScanner
//   - sellerDisplayName    -> safety::CreatorIdentityGuard
//   - thumbnailImageBytes  -> safety::AssetSafetyGuard (skipped if empty --
//     a listing isn't required to have a thumbnail)
//
// This is pure scan-and-report, the same honesty level as
// migration::ImportSafetyGuard::scan(): review() does not publish,
// persist, or reject anything itself, and there is no moderator-queue or
// catalog-database integration behind it yet. A real deployment wires
// NeedsHumanReview into an actual moderation queue and Rejected into a
// hard publish-block, and separately folds each finding's signal into
// the creator's TrustSafetyService risk score the same way
// TrustSafetyService::onCreatorContentSubmission already does for a bare
// IPInfringementScanner hit -- that wiring isn't done here because there
// is no listing-submission call site yet for it to be wired *from*.
class ListingReviewPipeline {
public:
    [[nodiscard]] ListingReviewReport review(const ListingSubmission& submission) const;

private:
    safety::IPInfringementScanner ipScanner_;
    safety::CreatorIdentityGuard identityGuard_;
    safety::AssetSafetyGuard assetGuard_;
};

} // namespace engine::marketplace
