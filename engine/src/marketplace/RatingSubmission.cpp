#include "marketplace/RatingSubmission.hpp"

#include <algorithm>
#include <cstdio>

#include "notification/NotificationService.hpp"

namespace engine::marketplace {

RatingSubmissionResult submitRating(core::LocalProfile& rater, core::AvatarItemManifest& item, float score,
                                     core::LocalProfile* creatorProfile) {
    if (!rater.creatorId.empty() && rater.creatorId == item.creatorId) {
        return {RatingSubmissionOutcome::CannotRateOwnItem};
    }
    if (rater.hasRatedItem(item.item.id)) {
        return {RatingSubmissionOutcome::AlreadyRated};
    }

    float clampedScore = std::clamp(score, 0.0f, 5.0f);
    float totalScore = item.ratingScore * static_cast<float>(item.ratingCount) + clampedScore;
    item.ratingCount += 1;
    item.ratingScore = totalScore / static_cast<float>(item.ratingCount);
    rater.ratedItemIds.push_back(item.item.id);

    // Kronos ("Ratings Notifications (Creator-side)"): real, correct
    // creator resolution -- see this function's own header comment.
    if (creatorProfile != nullptr && !item.creatorId.empty() && creatorProfile->creatorId == item.creatorId) {
        char body[256];
        std::snprintf(body, sizeof(body), "%s rated \"%s\" %.1f stars. New average: %.1f (%d rating%s).",
                      rater.displayName.c_str(), item.item.name.c_str(), static_cast<double>(clampedScore),
                      static_cast<double>(item.ratingScore), item.ratingCount, item.ratingCount == 1 ? "" : "s");
        notification::push(*creatorProfile, core::NotificationKind::RatingReceived, "New rating on \"" + item.item.name + "\"",
                            body, item.item.id);
    }

    return {RatingSubmissionOutcome::Success};
}

} // namespace engine::marketplace
