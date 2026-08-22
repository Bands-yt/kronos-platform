#include "migration/ProjectImporter.hpp"

#include <chrono>

#include "core/Logger.hpp"
#include "migration/PropertyDecoder.hpp"
#include "migration/RbxlxParser.hpp"

namespace engine::migration {
namespace {

bool isScriptClass(const std::string& className) {
    return className == "Script" || className == "LocalScript" || className == "ModuleScript";
}

// Roblox's own structural service containers. Their names are fixed by
// the platform, not chosen by the creator, so running the IP scan over
// them says nothing about the creator's content -- and it is not
// hypothetical noise: "ReplicatedStorage" fuzzy-matches "Roblox" in
// safety::IPInfringementScanner, so EVERY place file would carry a
// finding for a container the author never named. A report whose one
// constant entry is meaningless is a report people stop reading.
//
// Narrow on purpose: suppressed only when the instance's own name is
// still exactly its service class name. A container the creator renamed
// is theirs again and gets scanned normally.
bool isStructuralServiceName(const std::string& className, const std::string& leafName) {
    if (className != leafName) return false;
    static const char* kServices[] = {
        "Workspace", "ReplicatedStorage", "ReplicatedFirst", "ServerScriptService", "ServerStorage",
        "StarterPlayer", "StarterGui", "StarterPack", "StarterPlayerScripts", "StarterCharacterScripts",
        "Lighting", "SoundService", "Teams", "Players", "Chat", "TextChatService",
    };
    for (const char* service : kServices) {
        if (className == service) return true;
    }
    return false;
}

std::string leafOfPath(const std::string& path) {
    const size_t dot = path.rfind('.');
    return dot == std::string::npos ? path : path.substr(dot + 1);
}

} // namespace

size_t ImportReport::countOf(ImportSeverity severity) const {
    size_t count = 0;
    for (const ImportDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.severity == severity) ++count;
    }
    return count;
}

std::string ImportReport::summary() const {
    if (!parsed) return "import failed: the document could not be parsed as .rbxlx XML";
    std::string text = std::to_string(stats.instanceCount) + " instances, " + std::to_string(stats.scriptCount) +
                        " scripts, depth " + std::to_string(stats.maxDepth) + " -- " +
                        std::to_string(warningCount()) + " warning(s)";
    if (blocked) text += ", BLOCKED by the IP safety scan";
    return text;
}

ImportReport ProjectImporter::importDocument(const std::string& rbxlxSource,
                                              const safety::IPInfringementScanner& scanner) const {
    const auto started = std::chrono::steady_clock::now();
    ImportReport report;

    auto document = RbxlxParser::parse(rbxlxSource);
    if (!document.has_value()) {
        report.diagnostics.push_back({ImportSeverity::Blocked, "<document>",
                                       "could not be parsed as .rbxlx XML. Note that Roblox's BINARY formats "
                                       "(.rbxl/.rbxm) are a different container entirely and are not supported."});
        report.blocked = true;
        const auto finished = std::chrono::steady_clock::now();
        report.stats.elapsedMilliseconds =
            std::chrono::duration<double, std::milli>(finished - started).count();
        return report;
    }
    report.parsed = true;
    report.tree = InstanceTreeBuilder::build(*document);

    // Safety first, before anything reports on the contents: an import
    // that must be refused should say so at the top of its report.
    const ImportSafetyReport safety = ImportSafetyGuard::scan(report.tree, scanner);
    for (const ImportSafetyFinding& finding : safety.findings) {
        if (isStructuralServiceName(finding.className, leafOfPath(finding.instancePath))) continue;

        // Severity tracks what the scan actually decided. Only a hard
        // block must stop an import; a fuzzy or phonetic match is a
        // review signal, and reporting those as "blocked" would be a
        // false accusation against the creator's own content.
        const bool mustBlock = finding.scanResult.blocked;
        std::string message = mustBlock
                                   ? "IP safety scan BLOCKED this " + finding.className +
                                         ": its name matches a protected trademark."
                                   : "IP safety scan flagged this " + finding.className +
                                         " for review: its name resembles a protected term.";
        if (!finding.scanResult.matches.empty()) {
            message += " (matched \"" + finding.scanResult.matches.front().matchedTerm + "\")";
        }
        report.diagnostics.push_back(
            {mustBlock ? ImportSeverity::Blocked : ImportSeverity::Warning, finding.instancePath, message});
        if (mustBlock) report.blocked = true;
    }

    for (const ImportedInstance& root : report.tree) walk(root, "", 1, report);

    const auto finished = std::chrono::steady_clock::now();
    report.stats.elapsedMilliseconds = std::chrono::duration<double, std::milli>(finished - started).count();
    return report;
}

void ProjectImporter::walk(const ImportedInstance& node, const std::string& parentPath, size_t depth,
                            ImportReport& report) const {
    ++report.stats.instanceCount;
    if (depth > report.stats.maxDepth) report.stats.maxDepth = depth;

    const std::string path = parentPath.empty() ? node.name : parentPath + "." + node.name;

    // --- asset moderation -------------------------------------------------
    // Every property value is scanned, not a hand-listed set of asset
    // properties: Roblox has dozens of them (TextureId, MeshId, SoundId,
    // Graphic, the SurfaceAppearance maps, HumanoidDescription slots) and
    // a missed one is a proprietary asset that walks straight through the
    // filter. A value that is not a reference simply comes back Allowed.
    for (const auto& [key, value] : node.properties) {
        if (value.empty()) continue;
        if (key.rfind("@type.", 0) == 0) continue; // bookkeeping, not a value

        // Roblox marks asset references with the property type "Content",
        // which is what makes "N allowed, M blocked" a truthful count of
        // asset references rather than of every string in the document.
        // The URI test is still applied to everything as a backstop, for
        // documents where the type attribute is missing or unexpected.
        const bool isAssetReference = propertyType(node.properties, key) == "Content";
        const bool looksProprietary = AssetModerationFilter::isProprietaryUri(value);
        if (!isAssetReference && !looksProprietary) continue;

        const ModerationFinding finding = moderation_.evaluateReference(path + "." + key, value);
        if (finding.code == ModerationReasonCode::Allowed) {
            ++report.moderation.allowedCount;
            continue;
        }
        report.moderation.findings.push_back(finding);
        ++report.moderation.blockedCount;
        report.diagnostics.push_back(
            {ImportSeverity::Warning, path + "." + key,
             std::string(moderationReasonCodeName(finding.code)) + " -- " + finding.detail});
    }

    // The instance's own name, for trademarked terms and first-party
    // package names that arrive as metadata rather than as a URI.
    {
        std::string term;
        if (moderation_.isProtectedPackage(node.name, term)) {
            ModerationFinding finding;
            finding.subject = path;
            finding.reference = node.name;
            finding.code = ModerationReasonCode::BlockedProtectedPackage;
            finding.detail = "instance name references the protected first-party package \"" + term + "\"";
            report.moderation.findings.push_back(finding);
            ++report.moderation.blockedCount;
            report.diagnostics.push_back({ImportSeverity::Warning, path,
                                           std::string(moderationReasonCodeName(finding.code)) + " -- " +
                                               finding.detail});
        } else if (moderation_.hasTrademarkedMetadata(node.name, term)) {
            ModerationFinding finding;
            finding.subject = path;
            finding.reference = node.name;
            finding.code = ModerationReasonCode::BlockedTrademarkMetadata;
            finding.detail = "instance name contains the trademarked term \"" + term + "\"";
            report.moderation.findings.push_back(finding);
            ++report.moderation.blockedCount;
            report.diagnostics.push_back({ImportSeverity::Warning, path,
                                           std::string(moderationReasonCodeName(finding.code)) + " -- " +
                                               finding.detail});
        }
    }

    if (isScriptClass(node.className)) {
        ++report.stats.scriptCount;
        if (node.className == "ModuleScript") ++report.stats.moduleScriptCount;

        const auto sourceIt = node.properties.find("Source");
        if (sourceIt == node.properties.end() || sourceIt->second.empty()) {
            // Worth saying out loud: a Script with no Source is almost
            // always an export that dropped it, not an intentionally
            // empty script.
            report.diagnostics.push_back(
                {ImportSeverity::Warning, path, node.className + " has no Source property -- nothing was imported "
                                                 "for it."});
        } else {
            const std::string& source = sourceIt->second;
            report.stats.totalScriptBytes += source.size();

            for (const ApiCompatibilityFinding& finding : apiCompatibility_.scan(source)) {
                if (finding.status == ApiMappingStatus::Mapped) continue;
                report.diagnostics.push_back(
                    {finding.status == ApiMappingStatus::Shimmed ? ImportSeverity::Info : ImportSeverity::Warning,
                     path + ":" + std::to_string(finding.line), finding.identifier + " -- " + finding.guidance});
            }
            for (const CompatShimEntry& shim : shimLoader_.detectRequiredShims(source)) {
                report.diagnostics.push_back({ImportSeverity::Info, path, shim.description});
            }
        }
    }

    for (const ImportedInstance& child : node.children) walk(child, path, depth + 1, report);
}

void ProjectImporter::logReport(const ImportReport& report, const std::string& documentLabel) {
    core::logInfo("Import", "%s: %s", documentLabel.c_str(), report.summary().c_str());
    for (const ImportDiagnostic& diagnostic : report.diagnostics) {
        switch (diagnostic.severity) {
            case ImportSeverity::Blocked:
                core::logError("Import", "%s -- %s", diagnostic.subject.c_str(), diagnostic.message.c_str());
                break;
            case ImportSeverity::Warning:
                core::logWarn("Import", "%s -- %s", diagnostic.subject.c_str(), diagnostic.message.c_str());
                break;
            case ImportSeverity::Info:
                core::logInfo("Import", "%s -- %s", diagnostic.subject.c_str(), diagnostic.message.c_str());
                break;
        }
    }
    // --- moderation audit --------------------------------------------------
    core::logInfo("Import", "%s: %s", documentLabel.c_str(), report.moderation.summary().c_str());
    for (const ModerationFinding& finding : report.moderation.findings) {
        core::logWarn("Import", "[%s] %s -> %s (replaced with %s)", moderationReasonCodeName(finding.code),
                       finding.subject.c_str(), finding.reference.c_str(),
                       placeholderKindName(finding.placeholder));
    }
    const std::string notice = report.moderation.complianceNotice();
    if (!notice.empty()) core::logWarn("Import", "%s", notice.c_str());

    core::logInfo("Import", "%s: ingested in %.2f ms (%zu script bytes)", documentLabel.c_str(),
                  report.stats.elapsedMilliseconds, report.stats.totalScriptBytes);
}

} // namespace engine::migration
