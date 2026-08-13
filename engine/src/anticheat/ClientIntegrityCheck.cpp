#include "anticheat/ClientIntegrityCheck.hpp"

namespace engine::anticheat {

namespace {
void matchAll(const ExploitSignatureDB& db, SignatureKind kind, const std::vector<std::string>& observedValues,
              std::vector<FlaggedSignature>& outFlagged) {
    for (const std::string& value : observedValues) {
        if (const ExploitSignature* signature = db.match(kind, value)) {
            outFlagged.push_back(FlaggedSignature{signature->label, value});
        }
    }
}
} // namespace

ClientIntegrityReport ClientIntegrityCheck::check(const std::vector<std::string>& observedProcessNames,
                                                    const std::vector<std::string>& observedModuleNames,
                                                    const std::vector<std::string>& observedWindowTitles) const {
    ClientIntegrityReport report;
    matchAll(signatureDb_, SignatureKind::ProcessName, observedProcessNames, report.flagged);
    matchAll(signatureDb_, SignatureKind::ModuleName, observedModuleNames, report.flagged);
    matchAll(signatureDb_, SignatureKind::WindowTitle, observedWindowTitles, report.flagged);
    report.passed = report.flagged.empty();
    return report;
}

} // namespace engine::anticheat
