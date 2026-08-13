#include "marketplace/adapters/XboxPaymentAdapter.hpp"

#include <cstdio>

namespace engine::marketplace {

bool XboxPaymentAdapter::initialize() {
    std::fprintf(stderr,
                  "XboxPaymentAdapter: cannot initialize -- the Microsoft GDK commerce API is NDA-gated and "
                  "is not present in this repository (see src/platform_adapters/adapters/README.md).\n");
    return false;
}

PurchaseResult XboxPaymentAdapter::purchase(const PurchaseRequest& /*request*/) {
    return PurchaseResult{false, "", "XboxPaymentAdapter: no real backend in this repository (NDA-gated SDK).", 0.0};
}

void XboxPaymentAdapter::onStoreForcedRevocation(const std::string& transactionId) {
    std::fprintf(stdout, "XboxPaymentAdapter: onStoreForcedRevocation(%s) -- no real backend in this repository.\n",
                 transactionId.c_str());
}

} // namespace engine::marketplace
