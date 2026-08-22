#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace engine::migration {

// Why an asset reference was refused. Stable identifiers, because these
// are what a compliance audit is read against -- a reason code that gets
// reworded between builds is useless in a report someone has to defend.
enum class ModerationReasonCode : uint8_t {
    Allowed = 0,
    // rbxassetid://, rbxasset://, roblox.com/asset/?id=, or an official CDN host.
    BlockedProprietaryCdn = 1,
    // A trademarked term or brand identifier in the asset's own metadata.
    BlockedTrademarkMetadata = 2,
    // A named first-party character/package (classic avatar bundles etc).
    BlockedProtectedPackage = 3,
    // Content hash present in the quarantine registry.
    BlockedQuarantinedHash = 4,
};

[[nodiscard]] const char* moderationReasonCodeName(ModerationReasonCode code);

// What an imported reference is replaced WITH once refused. Placeholders
// are generated, not shipped files: a checkerboard and a unit cube are
// cheap to synthesise and cannot themselves be an infringement.
enum class PlaceholderKind : uint8_t { None = 0, CheckerboardTexture = 1, UnitCubeMesh = 2, SilentAudio = 3 };

[[nodiscard]] const char* placeholderKindName(PlaceholderKind kind);

struct ModerationFinding {
    std::string subject;   // the instance path or asset reference that was scanned
    std::string reference; // the offending URI/term/hash, verbatim
    ModerationReasonCode code = ModerationReasonCode::Allowed;
    PlaceholderKind placeholder = PlaceholderKind::None;
    std::string detail;    // human-readable explanation, always populated for a block
};

struct ModerationReport {
    std::vector<ModerationFinding> findings; // blocks only; an allowed asset produces no entry
    size_t allowedCount = 0;
    size_t blockedCount = 0;

    [[nodiscard]] bool anyBlocked() const { return blockedCount > 0; }
    [[nodiscard]] size_t countOf(ModerationReasonCode code) const;
    // One-line audit summary for the Studio console.
    [[nodiscard]] std::string summary() const;
    // The compliance notice shown once per import when anything was
    // swapped, so it is never ambiguous whether a scene is missing art
    // because of a bug or because of this filter.
    [[nodiscard]] std::string complianceNotice() const;
};

// Blocks proprietary asset references before any conversion runs.
//
// The threat this addresses is specific: a .rbxlx names its art by
// rbxassetid:// URI rather than embedding it, so "importing" a place
// naively means fetching Roblox's assets from Roblox's CDN and baking
// them into another platform's content. This filter sits at
// AssetConverter's entry points -- the single choke point every asset
// path passes through -- and substitutes a generated placeholder instead.
//
// Nothing here performs network I/O, and that is the point: a blocked URI
// is never resolved, not merely discarded after fetching.
class AssetModerationFilter {
public:
    AssetModerationFilter();

    // --- URI classification -----------------------------------------------
    [[nodiscard]] static bool isProprietaryUri(const std::string& reference);
    // Placeholder appropriate to the reference's apparent asset kind.
    [[nodiscard]] static PlaceholderKind placeholderForReference(const std::string& reference);

    // --- metadata -----------------------------------------------------------
    // Trademarked terms and brand identifiers. Matched on whole words so
    // "Robloxian" is caught but an unrelated "Rob" is not.
    [[nodiscard]] bool hasTrademarkedMetadata(const std::string& text, std::string& outTerm) const;
    // First-party character/package names.
    [[nodiscard]] bool isProtectedPackage(const std::string& text, std::string& outPackage) const;

    // --- hash quarantine ----------------------------------------------------
    // Registry is seeded empty and populated by the operator: shipping a
    // guessed list of "known proprietary hashes" would be fiction. What is
    // real here is the mechanism and that it is checked on every import.
    void quarantineHash(const std::string& sha256Hex);
    [[nodiscard]] bool isQuarantined(const std::string& sha256Hex) const;
    [[nodiscard]] size_t quarantineSize() const { return quarantinedHashes_.size(); }
    // Hashes CONTENT, not a filename -- renaming a file must not launder it.
    [[nodiscard]] static std::string hashContent(const std::string& bytes);

    // --- the gate -----------------------------------------------------------
    // Single entry point AssetConverter calls. `subject` is what the
    // finding is reported against (an instance path, usually).
    [[nodiscard]] ModerationFinding evaluateReference(const std::string& subject,
                                                       const std::string& reference) const;
    // Same, for a file whose bytes are already in hand.
    [[nodiscard]] ModerationFinding evaluateContent(const std::string& subject, const std::string& reference,
                                                     const std::string& bytes) const;

    // Generates the checkerboard a blocked texture is replaced with:
    // RGBA8, `size` square, `cell` pixels per square. Deliberately
    // magenta/black -- unmistakably a placeholder, never plausibly the
    // art that was meant to be there.
    [[nodiscard]] static std::vector<uint8_t> generateCheckerboard(int size = 64, int cell = 8);

private:
    std::unordered_set<std::string> quarantinedHashes_;
    std::vector<std::string> trademarkTerms_;
    std::vector<std::string> protectedPackages_;
};

} // namespace engine::migration
