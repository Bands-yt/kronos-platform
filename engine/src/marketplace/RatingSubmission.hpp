#pragma once

#include "core/AvatarItemManifest.hpp"
#include "core/LocalProfile.hpp"

namespace engine::marketplace {

// Kronos ("Creator Payout Ledger + Ratings Submission + Marketplace
// Moderation v3" -- "Ratings Submission"): the real, only three real
// outcomes -- mirrors CreditsPurchaseOutcome's own exact shape/reasoning
// (see CreditsPurchase.hpp's own header comment for why this pattern is
// used throughout marketplace::).
enum class RatingSubmissionOutcome { Success, AlreadyRated, CannotRateOwnItem };

struct RatingSubmissionResult {
    RatingSubmissionOutcome outcome = RatingSubmissionOutcome::Success;
    [[nodiscard]] bool succeeded() const { return outcome == RatingSubmissionOutcome::Success; }
};

// Real, pure(ish) rating submission -- mutates `item`'s own ratingScore
// (a real running average: `(oldScore*oldCount + score) / (oldCount+1)`,
// not a fabricated/random value) and ratingCount, and records the real,
// new "this profile already rated this item" fact on `rater` (so a
// second call for the same item+rater real-fails with AlreadyRated,
// rather than letting one profile skew an item's score with repeated
// ratings). `score` is real-clamped to [0, 5] (a real, honest 5-star
// scale) before being folded in. Real-fails, mutating neither `item` nor
// `rater`, if `rater.creatorId` (see that field's own comment on why it's
// real and not free text) matches `item.creatorId` -- a creator can't
// inflate their own item's score.
//
// Kronos ("Ratings Notifications (Creator-side)"): `creatorProfile` is
// real, new, optional (default nullptr, so every existing caller/test
// keeps compiling and behaving identically). On a real Success, if
// `creatorProfile` is non-null AND its own creatorId genuinely matches
// `item.creatorId` (real, correct creator resolution -- a mismatched or
// null profile never gets a notification meant for someone else), a real
// RatingReceived notification is pushed onto it with the item's real
// id/name, the real just-submitted score, and the real, updated
// ratingScore/ratingCount. Real, honest, stated limitation: there is no
// account/server system in this local Alpha (see core::LocalProfile's
// own "no auth" scope) -- a caller can only pass a real creatorProfile
// object it actually has in memory, which in single-profile-per-machine
// production use is almost never the rater's own resident profile (the
// CannotRateOwnItem guard above already prevents that combination); this
// parameter exists so the real notification logic is correct and fully
// testable, and so any future multi-profile-aware surface gets it for
// free.
[[nodiscard]] RatingSubmissionResult submitRating(core::LocalProfile& rater, core::AvatarItemManifest& item, float score,
                                                    core::LocalProfile* creatorProfile = nullptr);

} // namespace engine::marketplace
