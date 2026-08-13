#pragma once

#include "marketplace/IPaymentAdapter.hpp"

namespace engine::marketplace {

// NDA-gated, same situation as platform_adapters::PlayStationAdapter (see
// src/platform_adapters/adapters/README.md): Sony's commerce API ships as
// part of the PS5 SDK. initialize() always fails, on purpose.
class PSNPaymentAdapter final : public IPaymentAdapter {
public:
    [[nodiscard]] const char* storeName() const override { return "PlayStation Store"; }
    [[nodiscard]] float platformFeePercent() const override { return 30.0f; } // illustrative

    [[nodiscard]] bool initialize() override;
    [[nodiscard]] PurchaseResult purchase(const PurchaseRequest& request) override;
    void onStoreForcedRevocation(const std::string& transactionId) override;
};

} // namespace engine::marketplace
