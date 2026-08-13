#include "marketplace/adapters/SteamPaymentAdapter.hpp"

#include <cstdio>

namespace engine::marketplace {

bool SteamPaymentAdapter::initialize() {
    // TODO: SteamAPI_Init() + ISteamMicroTxn callback registration.
    std::fprintf(stdout, "SteamPaymentAdapter: initialized (stub)\n");
    initialized_ = true;
    return true;
}

PurchaseResult SteamPaymentAdapter::purchase(const PurchaseRequest& /*request*/) {
    return PurchaseResult{false, "", "SteamPaymentAdapter::purchase not implemented -- see docs/ARCHITECTURE.md §9.", 0.0};
}

void SteamPaymentAdapter::onStoreForcedRevocation(const std::string& transactionId) {
    std::fprintf(stdout, "SteamPaymentAdapter: onStoreForcedRevocation(%s) -- not implemented\n", transactionId.c_str());
}

} // namespace engine::marketplace
