#include "marketplace/MarketplaceService.hpp"

#include <cstdio>

namespace engine::marketplace {

void MarketplaceService::registerAdapter(PlatformKind originPlatform, std::unique_ptr<IPaymentAdapter> adapter) {
    adapters_[originPlatform] = std::move(adapter);
}

void MarketplaceService::setSpendingApprovalCheck(std::function<bool(PlayerId, const PurchaseRequest&)> check) {
    spendingApprovalCheck_ = std::move(check);
}

PurchaseResult MarketplaceService::promptPurchase(PlatformKind originPlatform, const PurchaseRequest& request) {
    if (spendingApprovalCheck_ && !spendingApprovalCheck_(request.player, request)) {
        return PurchaseResult{false, "", "Rejected by platform parental-control / spending-approval gate.", 0.0};
    }

    auto it = adapters_.find(originPlatform);
    if (it == adapters_.end()) {
        return PurchaseResult{false, "", "No payment adapter registered for this platform.", 0.0};
    }
    return it->second->purchase(request);
}

void MarketplaceService::notifyStoreForcedRevocation(PlatformKind originPlatform, const std::string& transactionId) {
    auto it = adapters_.find(originPlatform);
    if (it == adapters_.end()) {
        std::fprintf(stderr, "MarketplaceService: revocation for unknown platform, transaction=%s\n", transactionId.c_str());
        return;
    }
    it->second->onStoreForcedRevocation(transactionId);
}

} // namespace engine::marketplace
