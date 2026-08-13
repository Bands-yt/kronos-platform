#pragma once

#include "marketplace/IPaymentAdapter.hpp"

namespace engine::marketplace {

// Publicly-available SDK (Google Play Billing), stubbed for scope reasons
// -- a real implementation wraps the Play Billing Library's BillingClient
// and needs platform_adapters::AndroidAdapter's app-lifecycle integration
// to actually connect to the Play Store.
class PlayBillingPaymentAdapter final : public IPaymentAdapter {
public:
    [[nodiscard]] const char* storeName() const override { return "Google Play Billing"; }
    [[nodiscard]] float platformFeePercent() const override { return 30.0f; } // illustrative; Google also has a reduced tier

    [[nodiscard]] bool initialize() override;
    [[nodiscard]] PurchaseResult purchase(const PurchaseRequest& request) override;
    void onStoreForcedRevocation(const std::string& transactionId) override;

private:
    bool initialized_ = false;
};

} // namespace engine::marketplace
