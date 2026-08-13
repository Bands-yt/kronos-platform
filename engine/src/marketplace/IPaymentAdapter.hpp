#pragma once

#include <cstdint>
#include <string>

namespace engine::marketplace {

using PlayerId = uint32_t;

struct PurchaseRequest {
    PlayerId player = 0;
    std::string productSku;
    std::string currencyCode = "USD"; // ISO 4217; illustrative default
};

struct PurchaseResult {
    bool succeeded = false;
    std::string transactionId;
    std::string message;
    // What the creator payout ledger actually cares about, per
    // docs/ARCHITECTURE.md §9: "the creator payout ledger tracks
    // net-of-platform-fee revenue per transaction, never a single blended
    // rate." Left at 0 here since no adapter does a real charge yet.
    double netRevenueAfterFees = 0.0;
};

// One per storefront in docs/ARCHITECTURE.md §9: "a platform-agnostic
// MarketplaceService-compatible API routes internally to per-storefront
// payment adapters." Every method here is structural only -- no adapter
// in src/marketplace/adapters/ talks to a real payment backend yet.
//
// onStoreForcedRevocation is the other half of §9's "No-refund policy" +
// "Store-mandated revocation" split: this is deliberately NOT a
// processRefund()-shaped method. There is no discretionary-refund entry
// point anywhere in this interface, on purpose -- once virtual currency is
// spent it's final (matches Roblox's own model). This method only exists
// to react to a reversal the *storefront* forced on its own (a chargeback,
// a buyer-protection reversal, a statutory cooling-off right) by revoking
// whatever unconverted balance is still attributable to the transaction --
// asset protection against free currency, not a refund feature.
class IPaymentAdapter {
public:
    virtual ~IPaymentAdapter() = default;

    [[nodiscard]] virtual const char* storeName() const = 0;

    // Illustrative only -- real fee schedules vary by store, category, and
    // negotiated terms, and are a finance/legal concern, not something to
    // hardcode as a single constant per adapter (see MarketplaceService.hpp's
    // note on this too).
    [[nodiscard]] virtual float platformFeePercent() const = 0;

    [[nodiscard]] virtual bool initialize() = 0;

    [[nodiscard]] virtual PurchaseResult purchase(const PurchaseRequest& request) = 0;

    virtual void onStoreForcedRevocation(const std::string& transactionId) = 0;
};

} // namespace engine::marketplace
