#pragma once

#include <string>
#include <vector>

#include "migration/InstanceTreeBuilder.hpp"
#include "safety/IPInfringementScanner.hpp"

namespace engine::migration {

struct ImportSafetyFinding {
    std::string instancePath; // dot-separated path from the tree root, e.g. "Workspace.AdoptMeHouse"
    std::string className;
    safety::IPInfringementResult scanResult;
};

struct ImportSafetyReport {
    std::vector<ImportSafetyFinding> findings; // only flagged instances -- a clean import produces an empty vector

    [[nodiscard]] bool anyBlocked() const;
    [[nodiscard]] bool anyFlagged() const { return !findings.empty(); }
};

// The one concrete call site for safety::IPInfringementScanner today (see
// that header's comment on why this matters for a platform that, like
// Roblox, doesn't do discretionary refunds): walks an already-parsed
// .rbxlx tree (InstanceTreeBuilder::build()'s output) and scans every
// instance's Name for Roblox trademark/IP terms *before* anything gets
// wired into the real ECS. There is no "undo an import" story in this
// codebase, so catching this before the tree ever reaches an
// Instance-wiring pass (still a documented TODO -- see
// InstanceTreeBuilder.hpp) is the only safe place to do it.
//
// This does not itself reject an import or touch a creator's account risk
// score -- it's pure scan-and-report over the tree, so it's usable and
// testable without a PlayerId or a TrustSafetyService instance (migration/
// has no notion of "which creator" an import belongs to). A caller that
// DOES have both (e.g. Studio's "Import .rbxlx..." action -- still a
// documented stub, see StudioApp.cpp's File menu) is expected to call
// scan() first, refuse the import outright if anyBlocked() is true, and
// separately call TrustSafetyService::onCreatorContentSubmission() per
// finding to fold the signal into that creator's risk score.
class ImportSafetyGuard {
public:
    [[nodiscard]] static ImportSafetyReport scan(const std::vector<ImportedInstance>& tree,
                                                  const safety::IPInfringementScanner& scanner);

private:
    static void scanNode(const ImportedInstance& node, const std::string& parentPath,
                          const safety::IPInfringementScanner& scanner, ImportSafetyReport& report);
};

} // namespace engine::migration
