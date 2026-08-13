#pragma once

#include "marketplace/IPaymentAdapter.hpp"

namespace engine::marketplace {

// NDA-gated, same situation as platform_adapters::XboxAdapter (see
// src/platform_adapters/adapters/README.md): Microsoft's commerce APIs
// for the Microsoft Store ship as part of the GDK, which cannot be built
// against or redistributed from this open repository. initialize() always
// fails, on purpose -- a real implementation lives in a separate,
// access-controlled repo compiled only inside Microsoft's toolchain.
class XboxPaymentAdapter final : public IPaymentAdapter {
public:
    [[nodiscard]] const char* storeName() const override { return "Microsoft Store"; }
    [[nodiscard]] float platformFeePercent() const override { return 30.0f; } // illustrative

    [[nodiscard]] bool initialize() override;
    [[nodiscard]] PurchaseResult purchase(const PurchaseRequest& request) override;
    void onStoreForcedRevocation(const std::string& transactionId) override;
};

} // namespace engine::marketplace
