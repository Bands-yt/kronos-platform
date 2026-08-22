#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "migration/AssetModeration.hpp"
#include "migration/ImportSafetyGuard.hpp"
#include "migration/InstanceTreeBuilder.hpp"
#include "migration/LuauApiCompatibility.hpp"
#include "migration/ScriptCompatShimLoader.hpp"

namespace engine::migration {

enum class ImportSeverity : uint8_t { Info = 0, Warning = 1, Blocked = 2 };

struct ImportDiagnostic {
    ImportSeverity severity = ImportSeverity::Info;
    std::string subject; // dot-separated instance path, or "<file>" for the document itself
    std::string message;
};

struct ImportStats {
    size_t instanceCount = 0;
    size_t scriptCount = 0;      // Script + LocalScript + ModuleScript
    size_t moduleScriptCount = 0;
    size_t maxDepth = 0;
    size_t totalScriptBytes = 0;
    double elapsedMilliseconds = 0.0;
};

struct ImportReport {
    bool parsed = false;
    // True when ImportSafetyGuard flagged something that must not be
    // imported. The caller decides what to do; this class never mutates
    // engine state, so refusing is the caller's job.
    bool blocked = false;
    ImportStats stats;
    std::vector<ImportDiagnostic> diagnostics;
    std::vector<ImportedInstance> tree;
    // Proprietary asset references found during ingestion. Separate from
    // `diagnostics` because a compliance audit is read on its own terms --
    // "what did we refuse to fetch, and why" is a different question from
    // "what will not work after migrating".
    ModerationReport moderation;

    [[nodiscard]] size_t countOf(ImportSeverity severity) const;
    [[nodiscard]] size_t warningCount() const { return countOf(ImportSeverity::Warning); }
    // One-line summary, the shape Studio's status bar and the engine log
    // both want.
    [[nodiscard]] std::string summary() const;
};

// Runs a whole .rbxlx project through the migration pipeline in one call:
// parse -> instance tree -> IP safety scan -> per-script API compatibility
// and compat-shim detection -> report.
//
// Every stage already existed and was individually tested; nothing
// referenced any of them together, so there was no path that actually
// ingested a project. This is that path.
//
// Deliberately pure: it reads a document and produces a report. It does
// not touch the ECS, the filesystem or the Luau VM. That is what makes
// the whole pipeline testable without a device or a running Studio, and
// it is also the honest boundary -- ImportedInstance -> real ECS entities
// needs the Instance-over-ECS translation layer that does not exist yet
// (see InstanceTreeBuilder.hpp).
class ProjectImporter {
public:
    // Adds a content hash to the quarantine registry consulted during
    // ingestion. See AssetModerationFilter::quarantineHash.
    void quarantineAssetHash(const std::string& sha256Hex) { moderation_.quarantineHash(sha256Hex); }

    // `scanner` supplies the IP term list; see ImportSafetyGuard.
    [[nodiscard]] ImportReport importDocument(const std::string& rbxlxSource,
                                               const safety::IPInfringementScanner& scanner) const;

    // Writes the report to core::Logger under category "Import", which is
    // what puts it in Studio's Engine Log tab. Warnings log as warnings so
    // they are visible without the author going looking.
    static void logReport(const ImportReport& report, const std::string& documentLabel);

private:
    void walk(const ImportedInstance& node, const std::string& parentPath, size_t depth, ImportReport& report) const;

    LuauApiCompatibility apiCompatibility_;
    ScriptCompatShimLoader shimLoader_;
    AssetModerationFilter moderation_;
};

} // namespace engine::migration
