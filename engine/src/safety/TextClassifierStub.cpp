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

bool TextClassifierStub::containsPiiSolicitationMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"what's your address", "give me your number", "send a pic", "how old are you"});
}

bool TextClassifierStub::containsOffPlatformRedirectMarker(const std::string& message) {
    std::string lower = toLower(message);
    return containsAny(lower, {"add me on discord", "text me at", "let's talk on snap", "off this app"});
}

TextClassification TextClassifierStub::classify(const std::string& message) const {
    TextClassification result;

    if (containsPiiSolicitationMarker(message)) {
        result.categories.push_back(TextRiskCategory::PiiSolicitation);
    }
    if (containsOffPlatformRedirectMarker(message)) {
        result.categories.push_back(TextRiskCategory::OffPlatformRedirect);
    }

    result.flagged = !result.categories.empty();
    result.confidence = result.flagged ? 0.4f : 0.0f; // heuristic hit, not a calibrated probability -- see header note
    return result;
}

} // namespace engine::safety
