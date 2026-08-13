#pragma once

#include "marketplace/IPaymentAdapter.hpp"

namespace engine::marketplace {

// Publicly-available SDK (Apple StoreKit), stubbed for scope reasons -- a
// real implementation wraps SKPaymentQueue/StoreKit 2 and needs receipt
// validation against Apple's servers, which platform_adapters::IOSAdapter
// would own the app-lifecycle side of.
class StoreKitPaymentAdapter final : public IPaymentAdapter {
public:
    [[nodiscard]] const char* storeName() const override { return "Apple StoreKit"; }
    [[nodiscard]] float platformFeePercent() const override { return 30.0f; } // illustrative; Apple's small-business tier is lower

    [[nodiscard]] bool initialize() override;
    [[nodiscard]] PurchaseResult purchase(const PurchaseRequest& request) override;
    void onStoreForcedRevocation(const std::string& transactionId) override;

private:
    bool initialized_ = false;
};

} // namespace engine::marketplace
