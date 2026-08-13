#include "marketplace/ListingReviewPipeline.hpp"

namespace engine::marketplace {

namespace {

void addIpFindings(std::vector<ListingReviewFinding>& findings, const safety::IPInfringementResult& scanResult,
                    const char* fieldLabel) {
    for (const auto& match : scanResult.matches) {
        findings.push_back(ListingReviewFinding{std::string("IPInfringementScanner:") + fieldLabel,
                                                  "Matched term \"" + match.matchedTerm + "\"",
                                                  scanResult.blocked});
    }
}

} // namespace

ListingReviewReport ListingReviewPipeline::review(const ListingSubmission& submission) const {
    ListingReviewReport report;

    addIpFindings(report.findings, ipScanner_.scan(submission.title), "title");
    addIpFindings(report.findings, ipScanner_.scan(submission.description), "description");

    safety::IdentityCheckResult identityResult = identityGuard_.checkDisplayName(submission.sellerDisplayName);
    if (identityResult.flagged) {
        report.findings.push_back(
            ListingReviewFinding{"CreatorIdentityGuard:sellerDisplayName", identityResult.reason, identityResult.blocked});
    }

    if (!submission.thumbnailImageBytes.empty()) {
        safety::AssetSafetyReport assetReport =
            assetGuard_.scanImageAsset(submission.thumbnailImageBytes, submission.thumbnailExtension, ipScanner_);
        for (const auto& finding : assetReport.findings) {
            report.findings.push_back(
                ListingReviewFinding{"AssetSafetyGuard:thumbnail", finding.description, finding.blocking});
        }
    }

    bool anyBlocking = false;
    for (const auto& finding : report.findings) {
        anyBlocking = anyBlocking || finding.blocking;
    }

    if (anyBlocking) {
        report.verdict = ListingReviewVerdict::Rejected;
    } else if (!report.findings.empty()) {
        report.verdict = ListingReviewVerdict::NeedsHumanReview;
    } else {
        report.verdict = ListingReviewVerdict::Approved;
    }

    return report;
}

} // namespace engine::marketplace
