#include "marketplace/adapters/PSNPaymentAdapter.hpp"

#include <cstdio>

namespace engine::marketplace {

bool PSNPaymentAdapter::initialize() {
    std::fprintf(stderr,
                  "PSNPaymentAdapter: cannot initialize -- the Sony PS5 SDK commerce API is NDA-gated and is "
                  "not present in this repository (see src/platform_adapters/adapters/README.md).\n");
    return false;
}

PurchaseResult PSNPaymentAdapter::purchase(const PurchaseRequest& /*request*/) {
    return PurchaseResult{false, "", "PSNPaymentAdapter: no real backend in this repository (NDA-gated SDK).", 0.0};
}

void PSNPaymentAdapter::onStoreForcedRevocation(const std::string& transactionId) {
    std::fprintf(stdout, "PSNPaymentAdapter: onStoreForcedRevocation(%s) -- no real backend in this repository.\n",
                 transactionId.c_str());
}

} // namespace engine::marketplace
