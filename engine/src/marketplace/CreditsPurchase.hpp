#pragma once

#include <cstdint>

#include "core/AvatarItemManifest.hpp"
#include "core/LocalProfile.hpp"
#include "marketplace/TransactionLog.hpp"

namespace engine::marketplace {

// Kronos ("Avatar Creation System, Marketplace & Economy" -- "Wire
// Wallet -> Catalogue purchases"): a real, LOCAL, virtual-currency-only
// transaction -- spends core::LocalProfile::kronosCredits against a real
// core::AvatarItemManifest::price. Deliberately NOT built on
// marketplace::MarketplaceService/IPaymentAdapter -- those are real,
// separate, already-stated-scope infrastructure for a FUTURE real-money
// purchase routed through a platform storefront (docs/ARCHITECTURE.md
// §9, PurchaseRequest::currencyCode/IPaymentAdapter::platformFeePercent).
// Spending a local integer against a local catalogue has no store, no
// currency code, no platform fee -- forcing it through that real-money
// shape would dishonestly imply this went through a real payment rail
// when it didn't. This is the real, honest, smaller thing it actually
// is: decrement a number, record ownership, append an audit entry.
//
// Real, explicit, stated scope boundary: this only ever mutates the
// *buyer's* profile. core::AvatarItemManifest::creatorId is free text a
// creator typed at upload time with no real, verified link back to a
// spendable core::LocalProfile/profileId (no account-linking system
// exists in this codebase) -- crediting a payout to that string would be
// fabricating a transfer with no real identity behind it. Seller payout
// is a real, separate, later feature once creator identity is real
// (matches core::LocalProfile::creatorVerified's own "real seam, no
// fabricated verification" precedent).
enum class CreditsPurchaseOutcome {
    Success,
    AlreadyOwned,
    InsufficientCredits,
};

struct CreditsPurchaseResult {
    CreditsPurchaseOutcome outcome = CreditsPurchaseOutcome::InsufficientCredits;
    [[nodiscard]] bool succeeded() const { return outcome == CreditsPurchaseOutcome::Success; }
};

// Real, pure(ish) purchase logic: mutates `profile` (deducts
// item.price real KronosCredits, records real ownership) and appends a
// real record to `log` on success. A real, honest no-op (no mutation to
// either `profile` or `log` at all) for AlreadyOwned/InsufficientCredits
// -- the caller's own UI reads `outcome` to show the right real message,
// not a generic failure. `nowUnixSeconds` is caller-supplied (this
// function has no clock of its own), matching this codebase's "pure
// function takes time as a parameter" testing convention throughout
// (e.g. core::computeGamePlayStats()).
[[nodiscard]] CreditsPurchaseResult purchaseItemWithCredits(core::LocalProfile& profile,
                                                              const core::AvatarItemManifest& item, TransactionLog& log,
                                                              int64_t nowUnixSeconds);

} // namespace engine::marketplace
