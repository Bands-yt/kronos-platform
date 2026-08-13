#pragma once

#include <string>

#include "safety/IPInfringementScanner.hpp"

namespace engine::safety {

struct IdentityCheckResult {
    bool flagged = false;
    bool blocked = false; // true => caller MUST reject the display name, not just log/flag it
    // Short, human-readable explanation for the audit trail / reviewer
    // queue -- a source label like RiskScore's signal names, not free
    // text pulled from the input itself (see docs/ARCHITECTURE.md §10's
    // data-governance note, same reasoning TrustSafetyService::Callbacks
    // follows for `reason`).
    std::string reason;
};

// Impersonation detection for creator-chosen display names/usernames --
// a different question from IPInfringementScanner's ("does this *content
// title* reuse Roblox's trademarks or a well-known original IP name?"):
// this asks "is this creator presenting themselves *as* an authority they
// aren't?" The canonical abuse pattern this exists for is the classic
// platform scam account -- "Roblox_Admin", "Official Roblox Support",
// "RobIox Staff Team" -- created to phish players who trust anything that
// looks official. That pattern needs two signals together to be a strong
// automated-block case (either alone is too common/legitimate to block
// on): a reference to the platform's own brand, AND a claim of official
// authority ("admin", "staff", "support", ...). Neither signal alone
// blocks -- a creator can legitimately be named "Admin" (a clan/guild
// role name) or legitimately discuss/parody Roblox in their bio -- but
// either alone still flags for human review, same "flag on a single
// ambiguous signal, block only on a strong compound one" policy
// TrustSafetyService::onCreatorContentSubmission already uses for
// IPInfringementScanner's own hard-block-vs-review split.
//
// The other half of the abuse pattern this catches: confusable-character
// ("homoglyph") obfuscation, e.g. spelling "Roblox" with a Cyrillic а
// (U+0430) in place of Latin 'a' -- visually identical, byte-for-byte
// different, so a plain substring/leetspeak scan (IPInfringementScanner's
// normalize()) never sees it. checkDisplayName() first reduces the input
// to an ASCII "skeleton" (known Latin-lookalike codepoints mapped back to
// their ASCII letter, zero-width characters dropped outright, every
// other non-ASCII codepoint dropped as not a Latin-lookalike -- see the
// .cpp's confusableReplacement()/isZeroWidth()) and scans *that*, not the
// raw string. This is NOT a general Unicode confusables implementation
// (no IDNA/UTS-39 skeleton algorithm, no full Unicode confusables.txt) --
// it's a small, hand-picked table of the Cyrillic/Greek letters most
// commonly abused for exactly this kind of username spoofing, the same
// honesty level as IPInfringementScanner's own heuristic term list.
class CreatorIdentityGuard {
public:
    [[nodiscard]] IdentityCheckResult checkDisplayName(const std::string& displayName) const;

private:
    IPInfringementScanner ipScanner_; // reused for its normalize()/Fuzzy/Phonetic layer against "Roblox"/"Robux"
};

} // namespace engine::safety
