#pragma once

#include <functional>
#include <memory>
#include <unordered_map>

#include "marketplace/IPaymentAdapter.hpp"
#include "platform_adapters/IPlatformAdapter.hpp"

namespace engine::marketplace {

using engine::platform_adapters::PlatformKind;

// docs/ARCHITECTURE.md §9: "a platform-agnostic MarketplaceService-
// compatible API [that] routes internally to platform-specific payment
// adapters based on which client/store front-end the purchase originated
// from." This is the routing layer -- the Luau-facing MarketplaceService
// API scripts would actually call (matching Roblox's own service of the
// same name) doesn't exist yet, same TODO as everywhere else in this
// codebase that depends on the Instance/DataModel translation layer
// (see core/Scripting.hpp).
class MarketplaceService {
public:
    void registerAdapter(PlatformKind originPlatform, std::unique_ptr<IPaymentAdapter> adapter);

    // Parental controls gate, checked BEFORE any adapter is called --
    // §9: "gated in front of the marketplace by each platform's native
    // parental-control system ... before an engine-level purchase is even
    // attempted." Returning false here means the platform-native check
    // (Xbox Family Settings / Apple Ask to Buy / Google Family Link, none
    // of which this stub actually calls) rejected the purchase; the
    // request never reaches an IPaymentAdapter in that case.
    void setSpendingApprovalCheck(std::function<bool(PlayerId, const PurchaseRequest&)> check);

    // Routes to the adapter registered for `originPlatform`. Fails
    // immediately (without calling any adapter) if the spending-approval
    // check is set and returns false, or if no adapter is registered for
    // that platform.
    [[nodiscard]] PurchaseResult promptPurchase(PlatformKind originPlatform, const PurchaseRequest& request);

    // Forwards to the adapter that would have handled the original
    // purchase's platform -- see IPaymentAdapter.hpp's note on why this is
    // not a refund method.
    void notifyStoreForcedRevocation(PlatformKind originPlatform, const std::string& transactionId);

private:
    std::unordered_map<PlatformKind, std::unique_ptr<IPaymentAdapter>> adapters_;
    std::function<bool(PlayerId, const PurchaseRequest&)> spendingApprovalCheck_;
};

} // namespace engine::marketplace
