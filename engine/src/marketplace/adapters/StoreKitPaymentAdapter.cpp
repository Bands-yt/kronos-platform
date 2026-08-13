#include "marketplace/adapters/StoreKitPaymentAdapter.hpp"

#include <cstdio>

namespace engine::marketplace {

bool StoreKitPaymentAdapter::initialize() {
    // TODO: SKPaymentQueue observer registration, product list fetch.
    std::fprintf(stdout, "StoreKitPaymentAdapter: initialized (stub)\n");
    initialized_ = true;
    return true;
}

PurchaseResult StoreKitPaymentAdapter::purchase(const PurchaseRequest& /*request*/) {
    return PurchaseResult{false, "", "StoreKitPaymentAdapter::purchase not implemented -- see docs/ARCHITECTURE.md §9.", 0.0};
}

void StoreKitPaymentAdapter::onStoreForcedRevocation(const std::string& transactionId) {
    std::fprintf(stdout, "StoreKitPaymentAdapter: onStoreForcedRevocation(%s) -- not implemented\n", transactionId.c_str());
}

} // namespace engine::marketplace
