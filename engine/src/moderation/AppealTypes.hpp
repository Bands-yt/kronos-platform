#pragma once

#include <cstdint>
#include <string>

#include "net/NetTypes.hpp"

namespace engine::moderation {

// Kronos ("Moderation Architecture v1", Phase 1): the real, human-facing
// outcome a moderator picks when resolving an Appeal (below) --
// `Pending` is the real, honest default until a moderator actually
// decides; nothing in this codebase auto-resolves an appeal.
enum class AppealOutcome { Pending, Upheld, Reduced, Reversed };

[[nodiscard]] const char* appealOutcomeName(AppealOutcome outcome);

// A real, player-submitted appeal of a moderation action taken against
// them -- mirrors moderation::PlayerReport's exact real shape/precedent
// (ReportTypes.hpp), the same "player-authored free text distinct from
// safety::TrustSafetyService's fixed source labels" split. `player` is
// who's appealing; `relatedReviewCaseReason` is the free-text `reason`
// of whichever moderation::ReviewCase (or a real chat-log/report
// context) this appeal is about, given by the player themselves at
// submission time -- there is no real cross-reference-by-id to a
// specific ReviewCase entry yet (ReviewQueue has no stable per-case id,
// only an index into a vector that clear() invalidates wholesale), so a
// human moderator resolving this is expected to use `relatedReviewCaseReason`
// plus the real chat log / risk tier / review queue (all already
// real-queryable in studio::plugins::ModerationPanel) to find the real
// context, the same way a moderator today already reads a ReviewCase's
// free-text `reason` to understand what actually happened.
struct Appeal {
    net::PlayerId player = net::kInvalidPlayer;
    // Kronos ("Moderation Architecture v2", "Account System v1" -- "appeal
    // history tied to identity"): the real, stable core::LocalProfile
    // identity behind `player`'s ephemeral, per-session PlayerId -- the
    // same real profileId net::AccountModerationRegistry already keys
    // persistent bans/mutes by (see NetworkSession::serverPlayerProfileIds_'s
    // own comment). 0 means "unknown" -- a real, honest default for an
    // appeal submitted before this field existed, or by a pre-Account-
    // System-v2 client that never sent a real profileId at all; never
    // silently treated as a real match against another appeal's real 0.
    uint64_t profileId = 0;
    std::string playerStatement;
    std::string relatedReviewCaseReason;
    AppealOutcome outcome = AppealOutcome::Pending;
    std::string reviewerNote; // real, moderator-authored, set alongside outcome by resolve()
    // Real, but on two deliberately different clocks -- a real, stated
    // asymmetry, not an oversight: submission happens server-side, on
    // the same session-relative clockSeconds_ moderation::ChatLog/
    // ReportLog/ReviewQueue's own timestamps already use (a live game
    // session is running). Resolution happens later, in Studio, which
    // has no live session clock to read at all (studio::plugins::
    // ModerationPanel loads this log from disk, not from a connected
    // session) -- resolve()'s real caller uses a real wall-clock Unix
    // timestamp instead. Don't subtract one from the other expecting a
    // real duration; they're both real timestamps, just not on the same
    // timeline.
    double submittedServerTimestampSeconds = 0.0;
    double resolvedServerTimestampSeconds = 0.0; // 0.0 while outcome == Pending
};

} // namespace engine::moderation
