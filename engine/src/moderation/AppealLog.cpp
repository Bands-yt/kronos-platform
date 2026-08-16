#include "moderation/AppealLog.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace engine::moderation {

const char* appealOutcomeName(AppealOutcome outcome) {
    switch (outcome) {
        case AppealOutcome::Pending: return "Pending";
        case AppealOutcome::Upheld: return "Upheld";
        case AppealOutcome::Reduced: return "Reduced";
        case AppealOutcome::Reversed: return "Reversed";
    }
    return "Unknown";
}

namespace {
int appealOutcomeToIndex(AppealOutcome outcome) { return static_cast<int>(outcome); }

AppealOutcome appealOutcomeFromIndex(int index) {
    switch (index) {
        case 0: return AppealOutcome::Pending;
        case 1: return AppealOutcome::Upheld;
        case 2: return AppealOutcome::Reduced;
        case 3: return AppealOutcome::Reversed;
        default: return AppealOutcome::Pending; // unrecognized on load -- fail soft
    }
}
} // namespace

void AppealLog::submit(Appeal appeal) { appeals_.push_back(std::move(appeal)); }

std::vector<Appeal> AppealLog::appealsForPlayer(net::PlayerId player) const {
    std::vector<Appeal> result;
    for (const auto& appeal : appeals_) {
        if (appeal.player == player) result.push_back(appeal);
    }
    return result;
}

std::vector<Appeal> AppealLog::appealsForProfileId(uint64_t profileId) const {
    std::vector<Appeal> result;
    // A real 0 never matches -- "unknown identity" isn't a real account to
    // aggregate appeals under, same "0 means unset, not a real value to
    // match" convention this codebase already applies to net::kInvalidPlayer.
    if (profileId == 0) return result;
    for (const auto& appeal : appeals_) {
        if (appeal.profileId == profileId) result.push_back(appeal);
    }
    return result;
}

bool AppealLog::resolve(size_t index, AppealOutcome outcome, const std::string& reviewerNote,
                         double nowServerTimestampSeconds) {
    if (index >= appeals_.size()) return false;
    appeals_[index].outcome = outcome;
    appeals_[index].reviewerNote = reviewerNote;
    appeals_[index].resolvedServerTimestampSeconds = nowServerTimestampSeconds;
    return true;
}

bool AppealLog::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "APPEALLOG 1\n";
    for (const auto& a : appeals_) {
        // Three real free-text fields (playerStatement/
        // relatedReviewCaseReason/reviewerNote) don't fit the usual
        // single-trailing-string-per-line convention -- same real
        // multi-sub-line-per-record shape core::SceneFile's own
        // ENTITY/TRANSFORM/RENDERABLE blocks already establish, applied
        // here instead of cramming three strings onto one line.
        out << "APPEAL " << a.player << ' ' << a.profileId << ' ' << appealOutcomeToIndex(a.outcome) << ' '
            << a.submittedServerTimestampSeconds << ' ' << a.resolvedServerTimestampSeconds << "\n";
        out << "STATEMENT " << a.playerStatement << "\n";
        out << "CONTEXT " << a.relatedReviewCaseReason << "\n";
        out << "NOTE " << a.reviewerNote << "\n";
    }
    out << "END\n";
    return out.good();
}

bool AppealLog::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("APPEALLOG", 0) != 0) return false;

    std::vector<Appeal> loaded;
    Appeal* current = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("APPEAL ", 0) == 0) {
            std::istringstream iss(line.substr(7));
            Appeal a;
            int outcomeIndex = 0;
            // Kronos ("Moderation Architecture v2", "Account System v1"):
            // profileId added as the real, second positional field --
            // this line is positional, not KEY-based, so this is a real,
            // one-time format change, not forward/backward compatible
            // with an older APPEAL line missing this field. Real, honest,
            // low-risk: no persisted .appeallog file exists anywhere in
            // this repo/checkout at the time of this change (grep-
            // verified), and persistModerationLogs is opt-in, so nothing
            // in this codebase's own history depends on the old shape.
            iss >> a.player >> a.profileId >> outcomeIndex >> a.submittedServerTimestampSeconds >>
                a.resolvedServerTimestampSeconds;
            a.outcome = appealOutcomeFromIndex(outcomeIndex);
            loaded.push_back(std::move(a));
            current = &loaded.back(); // refreshed here, never held across a later push_back -- same SceneFile convention
        } else if (line.rfind("STATEMENT ", 0) == 0 && current != nullptr) {
            current->playerStatement = line.substr(10);
        } else if (line.rfind("CONTEXT ", 0) == 0 && current != nullptr) {
            current->relatedReviewCaseReason = line.substr(8);
        } else if (line.rfind("NOTE ", 0) == 0 && current != nullptr) {
            current->reviewerNote = line.substr(5);
        } else if (line == "END") {
            break;
        }
    }

    appeals_ = std::move(loaded);
    return true;
}

} // namespace engine::moderation
