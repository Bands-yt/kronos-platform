#include "safety/TextClassifierStub.hpp"

#include <algorithm>
#include <cctype>

namespace engine::safety {

namespace {
std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
    return out;
}
bool containsAny(const std::string& haystackLower, std::initializer_list<const char*> needles) {
    for (const char* needle : needles) {
        if (haystackLower.find(needle) != std::string::npos) return true;
    }
    return false;
}
} // namespace

// Kronos ("Moderation Architecture v1", Phase 1 gap fix): Harassment and
// SexualContent were declared in TextRiskCategory and already consulted
// by safety::PolicyEngine::decide() (the "SexualContent+MinorOrUnknown"
// hard-block, the "Harassment" warn) from the moment PolicyEngine was
// written this same session, but classify() itself never produced
// either one -- both were real dead code, unreachable from any actual
// message, until this fix. Found by re-deriving why PolicyEngine
// references categories nothing upstream of it ever sets; the existing
// unit tests for PolicyEngine never caught it because they construct
// TextClassification by hand rather than going through classify().
bool TextClassifierStub::containsHarassmentMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"you're worthless", "nobody likes you", "everyone hates you", "kys", "you should quit"});
}

bool TextClassifierStub::containsSexualContentMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"send nudes", "send a nude", "show me your body", "let's have sex"});
}

bool TextClassifierStub::containsPiiSolicitationMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"what's your address", "give me your number", "send a pic", "how old are you"});
}

bool TextClassifierStub::containsOffPlatformRedirectMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"add me on discord", "text me at", "let's talk on snap", "off this app"});
}

// Kronos ("Moderation Architecture v1", Phase 1): a real, distinct
// pattern from PiiSolicitation/OffPlatformRedirect -- isolation/secrecy
// language and meet-in-person requests, the real markers a grooming
// conversation escalates through, not a re-labeling of the other two.
bool TextClassifierStub::containsGroomingMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"don't tell your parents", "keep this between us", "our little secret",
                                "you're mature for your age", "meet in person", "meet up in real life"});
}

bool TextClassifierStub::containsHateMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"go back to your country", "subhuman", "kill all", "your kind isn't welcome"});
}

bool TextClassifierStub::containsSelfHarmMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"kill myself", "end it all", "want to die", "self harm", "self-harm"});
}

bool TextClassifierStub::containsThreatMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"i will kill you", "i know where you live", "i'm going to hurt you", "i will find you"});
}

bool TextClassifierStub::containsSpamMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"free robux", "click this link now", "subscribe to my channel", "check out my server"});
}

TextClassification TextClassifierStub::classify(const std::string& message) const {
    TextClassification result;

    if (containsHarassmentMarker(message)) {
        result.categories.push_back(TextRiskCategory::Harassment);
    }
    if (containsSexualContentMarker(message)) {
        result.categories.push_back(TextRiskCategory::SexualContent);
    }
    if (containsPiiSolicitationMarker(message)) {
        result.categories.push_back(TextRiskCategory::PiiSolicitation);
    }
    if (containsOffPlatformRedirectMarker(message)) {
        result.categories.push_back(TextRiskCategory::OffPlatformRedirect);
    }
    if (containsGroomingMarker(message)) {
        result.categories.push_back(TextRiskCategory::Grooming);
    }
    if (containsHateMarker(message)) {
        result.categories.push_back(TextRiskCategory::Hate);
    }
    if (containsSelfHarmMarker(message)) {
        result.categories.push_back(TextRiskCategory::SelfHarm);
    }
    if (containsThreatMarker(message)) {
        result.categories.push_back(TextRiskCategory::Threats);
    }
    if (containsSpamMarker(message)) {
        result.categories.push_back(TextRiskCategory::Spam);
    }

    result.flagged = !result.categories.empty();
    result.confidence = result.flagged ? 0.4f : 0.0f; // heuristic hit, not a calibrated probability -- see header note
    return result;
}

} // namespace engine::safety
