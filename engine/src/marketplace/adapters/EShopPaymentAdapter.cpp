#include "marketplace/adapters/EShopPaymentAdapter.hpp"

#include <cstdio>

namespace engine::marketplace {

bool EShopPaymentAdapter::initialize() {
    std::fprintf(stderr,
                  "EShopPaymentAdapter: cannot initialize -- the Nintendo NX SDK commerce API is NDA-gated "
                  "and is not present in this repository (see src/platform_adapters/adapters/README.md).\n");
    return false;
}

PurchaseResult EShopPaymentAdapter::purchase(const PurchaseRequest& /*request*/) {
    return PurchaseResult{false, "", "EShopPaymentAdapter: no real backend in this repository (NDA-gated SDK).", 0.0};
}

void EShopPaymentAdapter::onStoreForcedRevocation(const std::string& transactionId) {
    std::fprintf(stdout, "EShopPaymentAdapter: onStoreForcedRevocation(%s) -- no real backend in this repository.\n",
                 transactionId.c_str());
}

} // namespace engine::marketplace
