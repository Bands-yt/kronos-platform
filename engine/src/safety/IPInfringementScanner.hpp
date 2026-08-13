#pragma once

#include <string>
#include <vector>

namespace engine::safety {

// How a match was found -- surfaced so a human reviewer (or a future
// auto-triage rule) can weigh matches differently: an Exact hit on a
// hard-block term is unambiguous, while a Phonetic hit is the loosest,
// most false-positive-prone signal this scanner produces. See scan()'s
// doc comment for the confidence each kind carries.
enum class IPInfringementMatchKind {
    Exact,      // normalized substring match (leetspeak/case/punctuation-insensitive)
    Fuzzy,      // small Levenshtein edit distance from the term, catches typo-evasion
    Phonetic,   // Soundex-equal tokens, catches sound-alike respelling
    MultiToken, // every significant word of a multi-word term appears somewhere, any order
};

struct IPInfringementMatch {
    std::string matchedTerm;
    float confidence = 0.0f; // 0..1, heuristic -- see class comment, not a calibrated probability
    IPInfringementMatchKind kind = IPInfringementMatchKind::Exact;
};

struct IPInfringementResult {
    bool flagged = false;
    bool blocked = false; // true => caller MUST reject the submission, not just log/flag it
    std::vector<IPInfringementMatch> matches;
};

// A keyword/heuristic scanner for Roblox trademark terms and well-known
// Roblox-original IP names, run over creator-supplied names/titles/
// descriptions at content-submission points (see migration::ImportSafetyGuard
// for the one concrete call site that exists today, and
// TrustSafetyService::onCreatorContentSubmission for the generic hook
// future call sites -- marketplace listing titles, plugin publish names --
// attach to once those flows exist). Exists specifically because this
// platform doesn't do discretionary refunds any more than Roblox does
// (see docs/ARCHITECTURE.md's economy section), which makes "did a
// creator pass off Roblox's own trademarks or well-known original IP as
// their own" a real legal-exposure question for the platform operator,
// not just a content-quality one.
//
// This is NOT a real trademark/copyright detection system: it's a
// heuristic term matcher run four ways over a fixed term list, the same
// honesty level as TextClassifierStub (see that header's comment on why a
// heuristic stub is still worth having -- it gives the pipeline around
// it, and callers like ImportSafetyGuard, something real to call and test
// today, not pseudocode). It does not do perceptual image hashing or
// understand context (a creator legitimately discussing or parodying
// Roblox in text would also match). Four independent matching strategies
// run per term, each catching a different evasion pattern a determined
// creator might try, at a confidence that reflects how strong a signal
// that strategy actually is:
//
//   - Exact (confidence up to 0.95): normalize() collapses case,
//     punctuation/spacing, and common leetspeak substitutions, so
//     "R0bl0x", "adopt_me", and "AdoptMe!!" all match the same normalized
//     term. This was the entire scanner before this expansion.
//   - Fuzzy (~0.5-0.85): bounded Levenshtein edit distance against a
//     sliding window of the normalized text, catches genuine misspelling-
//     as-evasion ("Roboxx", "Adptme") that normalize()'s fixed
//     substitution table doesn't cover.
//   - Phonetic (~0.4-0.5): Soundex-equal tokens, catches sound-alike
//     respelling ("Robloks") where the letters differ too much for
//     Levenshtein but it still reads as the same word aloud. The
//     noisiest signal here -- short words collide in Soundex more than
//     people expect -- which is why it never blocks, only flags.
//   - MultiToken (~0.6): every significant word of a multi-word term
//     (e.g. "Tower Of Hell" -> {"tower", "hell"}, "of" dropped as a
//     stopword) appears *somewhere* in the input as its own token,
//     regardless of order or what's interspersed -- catches "Hell Tower
//     Simulator" or "the tower of pure hell" evading a straight substring
//     match by reordering or padding. Requires >= 2 significant tokens,
//     so single-word terms (Piggy, Jailbreak, ...) skip this strategy
//     entirely (Exact/Fuzzy/Phonetic already cover them).
//
// Only Exact and (for terms long enough to keep false positives rare)
// Fuzzy matches against kHardBlockTerms escalate to blocked=true; every
// Phonetic/MultiToken match, and every match against kReviewTerms
// regardless of strategy, flags for human review instead. See the .cpp's
// scoreForHardBlock()/scoreForReview() for the exact confidence table.
class IPInfringementScanner {
public:
    [[nodiscard]] IPInfringementResult scan(const std::string& text) const;
};

} // namespace engine::safety
