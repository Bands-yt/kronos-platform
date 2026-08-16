#include "marketplace/CreditsPurchase.hpp"

namespace engine::marketplace {

CreditsPurchaseResult purchaseItemWithCredits(core::LocalProfile& profile, const core::AvatarItemManifest& item,
                                               TransactionLog& log, int64_t nowUnixSeconds) {
    if (profile.ownsItem(item.item.id)) {
        return CreditsPurchaseResult{CreditsPurchaseOutcome::AlreadyOwned};
    }
    if (profile.kronosCredits < static_cast<int64_t>(item.price)) {
        return CreditsPurchaseResult{CreditsPurchaseOutcome::InsufficientCredits};
    }

    profile.kronosCredits -= item.price;
    profile.ownedItemIds.push_back(item.item.id);

    log.record(TransactionRecord{profile.profileId, item.item.id, item.item.name, item.price, item.creatorId,
                                  nowUnixSeconds});

    return CreditsPurchaseResult{CreditsPurchaseOutcome::Success};
}

} // namespace engine::marketplace
