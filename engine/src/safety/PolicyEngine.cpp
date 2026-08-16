#include "safety/PolicyEngine.hpp"

#include <algorithm>

namespace engine::safety {

namespace {
bool hasCategory(const TextClassification& c, TextRiskCategory category) {
    return std::find(c.categories.begin(), c.categories.end(), category) != c.categories.end();
}
} // namespace

PolicyDecision PolicyEngine::decide(const TextClassification& classification, AgeGroup ageGroup) const {
    // Real, ordered by severity -- the first real match wins, since a
    // single message can hit multiple categories at once (e.g. Grooming
    // + SexualContent together) and the most severe real action applies,
    // not the last category checked.

    // Hard-block #1: explicit grooming, regardless of age -- the user's
    // spec's own explicit hard-block item, no ambiguity to weigh.
    if (hasCategory(classification, TextRiskCategory::Grooming)) {
        return PolicyDecision{PolicyAction::Block, "Grooming"};
    }

    // Hard-block #2: sexual content, when the sender is a self-declared
    // (or unset/Unknown, treated conservatively -- see AgeGroup's own
    // comment) minor. Real, honest scope: this is the sender's own
    // AgeGroup, not a real cross-check against who else is in the
    // conversation -- see this class's own header comment.
    if (hasCategory(classification, TextRiskCategory::SexualContent) && ageGroup != AgeGroup::Adult) {
        return PolicyDecision{PolicyAction::Block, "SexualContent+MinorOrUnknown"};
    }

    // Hard-block, Minor Mode only: off-platform redirects ("no off-
    // platform links" -- Kronos "Moderation Architecture v2", "Minor
    // Mode Enforcement"). An Adult sender gets the real, unchanged Warn
    // treatment below -- this is specifically the real, stated Minor
    // Mode restriction, not a global policy change.
    if (hasCategory(classification, TextRiskCategory::OffPlatformRedirect) && ageGroup != AgeGroup::Adult) {
        return PolicyDecision{PolicyAction::Block, "OffPlatformRedirect+MinorOrUnknown"};
    }

    // Hard-block #3: hate speech -- the real proxy this heuristic has for
    // the user's "extremist propaganda" item (TextClassifierStub's Hate
    // category doesn't distinguish casual toxicity from organized
    // extremist content -- a real, stated limitation; a real classifier
    // would need to make that distinction, this heuristic errs
    // conservative and blocks either).
    if (hasCategory(classification, TextRiskCategory::Hate)) {
        return PolicyDecision{PolicyAction::Block, "Hate"};
    }

    // Real, deliberate non-block: a self-harm disclosure must still
    // reach a human reviewer, but blocking it outright could silence
    // someone's real cry for help -- queue for review, don't suppress.
    if (hasCategory(classification, TextRiskCategory::SelfHarm)) {
        return PolicyDecision{PolicyAction::QueueForReview, "SelfHarm"};
    }

    // A real, credible-sounding threat needs a real human to see it
    // quickly -- queue for review rather than a bare warn.
    if (hasCategory(classification, TextRiskCategory::Threats)) {
        return PolicyDecision{PolicyAction::QueueForReview, "Threats"};
    }

    if (hasCategory(classification, TextRiskCategory::Harassment)) {
        return PolicyDecision{PolicyAction::Warn, "Harassment"};
    }
    if (hasCategory(classification, TextRiskCategory::PiiSolicitation)) {
        return PolicyDecision{PolicyAction::Warn, "PiiSolicitation"};
    }
    if (hasCategory(classification, TextRiskCategory::OffPlatformRedirect)) {
        return PolicyDecision{PolicyAction::Warn, "OffPlatformRedirect"};
    }
    if (hasCategory(classification, TextRiskCategory::Spam)) {
        return PolicyDecision{PolicyAction::Warn, "Spam"};
    }

    return PolicyDecision{PolicyAction::Allow, ""};
}

namespace {
bool hasImageCategory(const ImageClassification& c, ImageRiskCategory category) {
    return std::find(c.categories.begin(), c.categories.end(), category) != c.categories.end();
}
} // namespace

PolicyDecision PolicyEngine::decide(const ImageClassification& classification, AgeGroup ageGroup) const {
    if (hasImageCategory(classification, ImageRiskCategory::Gore)) {
        return PolicyDecision{PolicyAction::Block, "Gore"};
    }
    if (hasImageCategory(classification, ImageRiskCategory::ExtremistSymbols)) {
        return PolicyDecision{PolicyAction::Block, "ExtremistSymbols"};
    }
    if (hasImageCategory(classification, ImageRiskCategory::Nudity) && ageGroup != AgeGroup::Adult) {
        return PolicyDecision{PolicyAction::Block, "Nudity+MinorOrUnknown"};
    }
    if (hasImageCategory(classification, ImageRiskCategory::Sexualization) && ageGroup != AgeGroup::Adult) {
        return PolicyDecision{PolicyAction::Block, "Sexualization+MinorOrUnknown"};
    }
    if (hasImageCategory(classification, ImageRiskCategory::Nudity)) {
        return PolicyDecision{PolicyAction::Warn, "Nudity"};
    }
    if (hasImageCategory(classification, ImageRiskCategory::Sexualization)) {
        return PolicyDecision{PolicyAction::Warn, "Sexualization"};
    }

    return PolicyDecision{PolicyAction::Allow, ""};
}

} // namespace engine::safety
