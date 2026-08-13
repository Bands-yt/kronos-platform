#pragma once

#include "marketplace/IPaymentAdapter.hpp"

namespace engine::marketplace {

// Publicly-available SDK (Steamworks), stubbed for scope reasons -- a
// real implementation wraps ISteamMicroTxn / ISteamInventory and needs the
// Steam client running locally to test against at all.
class SteamPaymentAdapter final : public IPaymentAdapter {
public:
    [[nodiscard]] const char* storeName() const override { return "Steam"; }
    [[nodiscard]] float platformFeePercent() const override { return 30.0f; } // illustrative, see header note

    [[nodiscard]] bool initialize() override;
    [[nodiscard]] PurchaseResult purchase(const PurchaseRequest& request) override;
    void onStoreForcedRevocation(const std::string& transactionId) override;

private:
    bool initialized_ = false;
};

} // namespace engine::marketplace
