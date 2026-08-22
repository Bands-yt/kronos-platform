#include "migration/AssetModeration.hpp"

#include <algorithm>
#include <cctype>

#include "core/OAuthPkce.hpp"

namespace engine::migration {
namespace {

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }

// Whole-word containment. A plain substring search flags "Rob" inside
// "Robert" and turns a compliance report into noise nobody reads.
bool containsWord(const std::string& haystackLower, const std::string& needleLower) {
    if (needleLower.empty()) return false;
    size_t pos = haystackLower.find(needleLower);
    while (pos != std::string::npos) {
        const bool leftOk = pos == 0 || !isWordChar(haystackLower[pos - 1]);
        const size_t end = pos + needleLower.size();
        const bool rightOk = end >= haystackLower.size() || !isWordChar(haystackLower[end]);
        if (leftOk && rightOk) return true;
        pos = haystackLower.find(needleLower, pos + 1);
    }
    return false;
}

bool containsSubstring(const std::string& haystackLower, const char* needle) {
    return haystackLower.find(needle) != std::string::npos;
}

} // namespace

const char* moderationReasonCodeName(ModerationReasonCode code) {
    switch (code) {
        case ModerationReasonCode::Allowed: return "ALLOWED";
        case ModerationReasonCode::BlockedProprietaryCdn: return "BLOCKED_PROPRIETARY_CDN";
        case ModerationReasonCode::BlockedTrademarkMetadata: return "BLOCKED_TRADEMARK_METADATA";
        case ModerationReasonCode::BlockedProtectedPackage: return "BLOCKED_PROTECTED_PACKAGE";
        case ModerationReasonCode::BlockedQuarantinedHash: return "BLOCKED_QUARANTINED_HASH";
    }
    return "UNKNOWN";
}

const char* placeholderKindName(PlaceholderKind kind) {
    switch (kind) {
        case PlaceholderKind::None: return "none";
        case PlaceholderKind::CheckerboardTexture: return "checkerboard texture";
        case PlaceholderKind::UnitCubeMesh: return "unit cube mesh";
        case PlaceholderKind::SilentAudio: return "silent audio";
    }
    return "unknown";
}

// --- report -----------------------------------------------------------------

size_t ModerationReport::countOf(ModerationReasonCode code) const {
    size_t count = 0;
    for (const ModerationFinding& finding : findings) {
        if (finding.code == code) ++count;
    }
    return count;
}

std::string ModerationReport::summary() const {
    std::string text = "moderation: " + std::to_string(allowedCount) + " allowed, " +
                        std::to_string(blockedCount) + " blocked";
    if (blockedCount == 0) return text;
    text += " (";
    bool first = true;
    for (const ModerationReasonCode code :
         {ModerationReasonCode::BlockedProprietaryCdn, ModerationReasonCode::BlockedTrademarkMetadata,
          ModerationReasonCode::BlockedProtectedPackage, ModerationReasonCode::BlockedQuarantinedHash}) {
        const size_t count = countOf(code);
        if (count == 0) continue;
        if (!first) text += ", ";
        text += std::string(moderationReasonCodeName(code)) + "=" + std::to_string(count);
        first = false;
    }
    text += ")";
    return text;
}

std::string ModerationReport::complianceNotice() const {
    if (blockedCount == 0) return {};
    return "COMPLIANCE: " + std::to_string(blockedCount) +
            " proprietary asset reference(s) were NOT downloaded. Each was replaced with a locally generated "
            "placeholder so the imported scene stays free of third-party content. Replace them with assets you own "
            "before publishing.";
}

// --- filter -----------------------------------------------------------------

AssetModerationFilter::AssetModerationFilter() {
    // Trademarks and brand identifiers. Illustrative and operator-
    // maintained, exactly as safety::IPInfringementScanner says of its own
    // list -- a shipped list is a starting point, not legal advice.
    trademarkTerms_ = {"roblox", "robloxian", "robux", "bloxburg", "rbxassetid", "powering imagination"};

    // First-party character/package names that ship with the platform.
    protectedPackages_ = {"noob",       "builderman", "bacon hair",   "korblox",   "headless horseman",
                           "guest 1337", "shedletsky", "telamon",      "dominus",   "classic roblox",
                           "man face",   "chill face",  "robloxclassic"};
}

bool AssetModerationFilter::isProprietaryUri(const std::string& reference) {
    if (reference.empty()) return false;
    const std::string lower = toLower(reference);

    // Roblox's own URI schemes.
    if (lower.rfind("rbxassetid://", 0) == 0) return true;
    if (lower.rfind("rbxasset://", 0) == 0) return true;
    if (lower.rfind("rbxthumb://", 0) == 0) return true;
    if (lower.rfind("rbxhttp://", 0) == 0) return true;
    if (lower.rfind("rbxgameasset://", 0) == 0) return true;

    // Asset endpoints and official CDN hosts, however they are spelled.
    static const char* kProprietaryHosts[] = {
        "roblox.com/asset", "assetgame.roblox.com", "assetdelivery.roblox.com", "www.roblox.com/asset",
        "c0.rbxcdn.com",    "c1.rbxcdn.com",        "c2.rbxcdn.com",            "c3.rbxcdn.com",
        "c4.rbxcdn.com",    "c5.rbxcdn.com",        "c6.rbxcdn.com",            "c7.rbxcdn.com",
        "t0.rbxcdn.com",    "t1.rbxcdn.com",        "rbxcdn.com",               "roblox.com/library",
        "create.roblox.com/marketplace",
    };
    for (const char* host : kProprietaryHosts) {
        if (containsSubstring(lower, host)) return true;
    }
    return false;
}

PlaceholderKind AssetModerationFilter::placeholderForReference(const std::string& reference) {
    const std::string lower = toLower(reference);
    // Roblox property names carry the kind more reliably than the URI does
    // -- an rbxassetid:// URI is just a number and says nothing about what
    // it points at.
    if (containsSubstring(lower, "mesh")) return PlaceholderKind::UnitCubeMesh;
    if (containsSubstring(lower, "sound") || containsSubstring(lower, "audio")) return PlaceholderKind::SilentAudio;
    return PlaceholderKind::CheckerboardTexture;
}

bool AssetModerationFilter::hasTrademarkedMetadata(const std::string& text, std::string& outTerm) const {
    const std::string lower = toLower(text);
    for (const std::string& term : trademarkTerms_) {
        // Multi-word terms cannot be word-matched the same way; a
        // substring check is correct for them and safe, since a phrase
        // that long does not collide accidentally.
        const bool hit = term.find(' ') != std::string::npos ? containsSubstring(lower, term.c_str())
                                                              : containsWord(lower, term);
        if (hit) {
            outTerm = term;
            return true;
        }
    }
    return false;
}

bool AssetModerationFilter::isProtectedPackage(const std::string& text, std::string& outPackage) const {
    const std::string lower = toLower(text);
    for (const std::string& package : protectedPackages_) {
        const bool hit = package.find(' ') != std::string::npos ? containsSubstring(lower, package.c_str())
                                                                 : containsWord(lower, package);
        if (hit) {
            outPackage = package;
            return true;
        }
    }
    return false;
}

void AssetModerationFilter::quarantineHash(const std::string& sha256Hex) {
    quarantinedHashes_.insert(toLower(sha256Hex));
}

bool AssetModerationFilter::isQuarantined(const std::string& sha256Hex) const {
    return quarantinedHashes_.find(toLower(sha256Hex)) != quarantinedHashes_.end();
}

std::string AssetModerationFilter::hashContent(const std::string& bytes) {
    // core::sha256 returns the RAW 32-byte digest. Hex-encoding here is not
    // cosmetic: the quarantine registry is keyed by strings an operator
    // types into a config and that appear in an audit log, and raw digest
    // bytes contain NULs and unprintable characters that neither survives.
    const std::string raw = core::sha256(bytes);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(raw.size() * 2);
    for (const char byte : raw) {
        const auto value = static_cast<uint8_t>(byte);
        hex.push_back(kHex[value >> 4]);
        hex.push_back(kHex[value & 0x0F]);
    }
    return hex;
}

ModerationFinding AssetModerationFilter::evaluateReference(const std::string& subject,
                                                            const std::string& reference) const {
    ModerationFinding finding;
    finding.subject = subject;
    finding.reference = reference;

    if (isProprietaryUri(reference)) {
        finding.code = ModerationReasonCode::BlockedProprietaryCdn;
        finding.placeholder = placeholderForReference(subject + " " + reference);
        finding.detail = "proprietary asset reference; not fetched, replaced with a " +
                          std::string(placeholderKindName(finding.placeholder));
        return finding;
    }

    std::string package;
    // Checked before the trademark scan: "classic roblox" should report as
    // a protected package rather than the broader trademark hit, because
    // that is the more specific and more actionable reason.
    if (isProtectedPackage(reference, package) || isProtectedPackage(subject, package)) {
        finding.code = ModerationReasonCode::BlockedProtectedPackage;
        finding.placeholder = placeholderForReference(subject + " " + reference);
        finding.detail = "references the protected first-party package \"" + package + "\"";
        return finding;
    }

    std::string term;
    if (hasTrademarkedMetadata(reference, term) || hasTrademarkedMetadata(subject, term)) {
        finding.code = ModerationReasonCode::BlockedTrademarkMetadata;
        finding.placeholder = placeholderForReference(subject + " " + reference);
        finding.detail = "metadata contains the trademarked term \"" + term + "\"";
        return finding;
    }

    finding.code = ModerationReasonCode::Allowed;
    return finding;
}

ModerationFinding AssetModerationFilter::evaluateContent(const std::string& subject, const std::string& reference,
                                                          const std::string& bytes) const {
    // The hash check runs FIRST and on content: a quarantined asset must
    // not slip through by being renamed or re-hosted at an innocuous URL.
    const std::string digest = hashContent(bytes);
    if (isQuarantined(digest)) {
        ModerationFinding finding;
        finding.subject = subject;
        finding.reference = reference;
        finding.code = ModerationReasonCode::BlockedQuarantinedHash;
        finding.placeholder = placeholderForReference(subject + " " + reference);
        finding.detail = "content hash " + digest.substr(0, 16) + "... is in the quarantine registry";
        return finding;
    }
    return evaluateReference(subject, reference);
}

std::vector<uint8_t> AssetModerationFilter::generateCheckerboard(int size, int cell) {
    size = std::max(size, 2);
    cell = std::max(cell, 1);
    std::vector<uint8_t> rgba(static_cast<size_t>(size) * static_cast<size_t>(size) * 4u);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool on = ((x / cell) + (y / cell)) % 2 == 0;
            const size_t index = (static_cast<size_t>(y) * static_cast<size_t>(size) + static_cast<size_t>(x)) * 4u;
            rgba[index + 0] = on ? 255 : 20;
            rgba[index + 1] = on ? 0 : 20;
            rgba[index + 2] = on ? 255 : 20;
            rgba[index + 3] = 255;
        }
    }
    return rgba;
}

} // namespace engine::migration
