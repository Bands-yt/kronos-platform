#include "safety/ImageClassifierStub.hpp"

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

bool ImageClassifierStub::containsNudityMarker(const std::string& lowerFilename) {
    return containsAny(lowerFilename, {"nude", "naked", "nsfw"});
}

bool ImageClassifierStub::containsSexualizationMarker(const std::string& lowerFilename) {
    return containsAny(lowerFilename, {"thirst_trap", "onlyfans", "fetish"});
}

bool ImageClassifierStub::containsGoreMarker(const std::string& lowerFilename) {
    return containsAny(lowerFilename, {"gore", "mutilat", "beheading"});
}

bool ImageClassifierStub::containsExtremistSymbolMarker(const std::string& lowerFilename) {
    return containsAny(lowerFilename, {"swastika", "nazi_flag", "isis_flag"});
}

ImageClassification ImageClassifierStub::classify(const std::string& filename) const {
    ImageClassification result;
    std::string lower = toLower(filename);

    if (containsNudityMarker(lower)) {
        result.categories.push_back(ImageRiskCategory::Nudity);
    }
    if (containsSexualizationMarker(lower)) {
        result.categories.push_back(ImageRiskCategory::Sexualization);
    }
    if (containsGoreMarker(lower)) {
        result.categories.push_back(ImageRiskCategory::Gore);
    }
    if (containsExtremistSymbolMarker(lower)) {
        result.categories.push_back(ImageRiskCategory::ExtremistSymbols);
    }

    result.flagged = !result.categories.empty();
    result.confidence = result.flagged ? 0.4f : 0.0f; // heuristic hit, not a calibrated probability -- see header note
    return result;
}

} // namespace engine::safety
