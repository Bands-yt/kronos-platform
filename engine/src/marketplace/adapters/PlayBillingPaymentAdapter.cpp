#include "marketplace/adapters/PlayBillingPaymentAdapter.hpp"

#include <cstdio>

namespace engine::marketplace {

bool PlayBillingPaymentAdapter::initialize() {
    // TODO: BillingClient.newBuilder(...).build().startConnection(...) via JNI.
    std::fprintf(stdout, "PlayBillingPaymentAdapter: initialized (stub)\n");
    initialized_ = true;
    return true;
}

PurchaseResult PlayBillingPaymentAdapter::purchase(const PurchaseRequest& /*request*/) {
    return PurchaseResult{false, "", "PlayBillingPaymentAdapter::purchase not implemented -- see docs/ARCHITECTURE.md §9.", 0.0};
}

void PlayBillingPaymentAdapter::onStoreForcedRevocation(const std::string& transactionId) {
    std::fprintf(stdout, "PlayBillingPaymentAdapter: onStoreForcedRevocation(%s) -- not implemented\n", transactionId.c_str());
}

} // namespace engine::marketplace
