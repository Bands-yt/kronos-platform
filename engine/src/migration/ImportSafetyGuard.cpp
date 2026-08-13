#include "migration/ImportSafetyGuard.hpp"

#include <utility>

namespace engine::migration {

bool ImportSafetyReport::anyBlocked() const {
    for (const auto& finding : findings) {
        if (finding.scanResult.blocked) return true;
    }
    return false;
}

void ImportSafetyGuard::scanNode(const ImportedInstance& node, const std::string& parentPath,
                                  const safety::IPInfringementScanner& scanner, ImportSafetyReport& report) {
    std::string path = parentPath.empty() ? node.name : parentPath + "." + node.name;

    safety::IPInfringementResult result = scanner.scan(node.name);
    if (result.flagged) {
        report.findings.push_back(ImportSafetyFinding{path, node.className, std::move(result)});
    }

    for (const auto& child : node.children) {
        scanNode(child, path, scanner, report);
    }
}

ImportSafetyReport ImportSafetyGuard::scan(const std::vector<ImportedInstance>& tree,
                                            const safety::IPInfringementScanner& scanner) {
    ImportSafetyReport report;
    for (const auto& root : tree) {
        scanNode(root, "", scanner, report);
    }
    return report;
}

} // namespace engine::migration
