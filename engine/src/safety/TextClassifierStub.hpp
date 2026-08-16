#pragma once

#include <string>
#include <vector>

namespace engine::safety {

// Kronos ("Moderation Architecture v1", Phase 1): Grooming/Hate/SelfHarm/
// Threats/Spam added alongside the original 4 -- the exact category list
// the user's spec names for the real-time text classifier. `Grooming` is
// deliberately its own real heuristic (see containsGroomingMarker()),
// not just "PiiSolicitation + OffPlatformRedirect" reused -- a real,
// distinct pattern (isolation/secrecy language, age-appropriateness
// probing, meet-in-person requests), same "different keyword set, same
// tiny-heuristic honesty" as every other category here.
enum class TextRiskCategory {
    Harassment,
    SexualContent,
    PiiSolicitation,
    OffPlatformRedirect,
    Grooming,
    Hate,
    SelfHarm,
    Threats,
    Spam,
};

[[nodiscard]] inline const char* textRiskCategoryName(TextRiskCategory category) {
    switch (category) {
        case TextRiskCategory::Harassment: return "Harassment";
        case TextRiskCategory::SexualContent: return "SexualContent";
        case TextRiskCategory::PiiSolicitation: return "PiiSolicitation";
        case TextRiskCategory::OffPlatformRedirect: return "OffPlatformRedirect";
        case TextRiskCategory::Grooming: return "Grooming";
        case TextRiskCategory::Hate: return "Hate";
        case TextRiskCategory::SelfHarm: return "SelfHarm";
        case TextRiskCategory::Threats: return "Threats";
        case TextRiskCategory::Spam: return "Spam";
    }
    return "Unknown";
}

struct TextClassification {
    bool flagged = false;
    std::vector<TextRiskCategory> categories;
    float confidence = 0.0f;

    // Kronos ("Moderation Architecture v1", Phase 1): set by
    // safety::PolicyEngine (safety/PolicyEngine.hpp), consulted from
    // TrustSafetyService::onChatMessage() -- true means the real hard-
    // block policy decided this message must NOT be delivered at all,
    // not just flagged-after-delivery. Mirrors IPInfringementResult::
    // blocked's exact same real shape/meaning.
    bool blocked = false;
};

// Structural stand-in for docs/ARCHITECTURE.md §10's real-time text
// classifier: "a small quantized/distilled transformer, edge-deployable
// ... inside the synchronous send-path budget (<10ms)".
//
// classify() below is NOT that model -- there is no ML here yet, only a
// tiny keyword heuristic, clearly not production-grade, so the pipeline
// around it (ModerationPipeline, RiskScore, the escalation tiers) has
// something to call and is exercisable/testable before a real model
// exists. Swapping this out for ONNX Runtime inference against a real
// classifier (per docs/ARCHITECTURE.md §3's stack) is a same-signature
// change to this one class, which is the point of keeping the interface
// stable now.
class TextClassifierStub {
public:
    [[nodiscard]] TextClassification classify(const std::string& message) const;

private:
    // Deliberately tiny and not case/context-aware -- see the class
    // comment. Real word/phrase lists for each category are a trust &
    // safety content decision, not something to hardcode here.
    [[nodiscard]] static bool containsHarassmentMarker(const std::string& message);
    [[nodiscard]] static bool containsSexualContentMarker(const std::string& message);
    [[nodiscard]] static bool containsPiiSolicitationMarker(const std::string& message);
    [[nodiscard]] static bool containsOffPlatformRedirectMarker(const std::string& message);
    [[nodiscard]] static bool containsGroomingMarker(const std::string& message);
    [[nodiscard]] static bool containsHateMarker(const std::string& message);
    [[nodiscard]] static bool containsSelfHarmMarker(const std::string& message);
    [[nodiscard]] static bool containsThreatMarker(const std::string& message);
    [[nodiscard]] static bool containsSpamMarker(const std::string& message);
};

} // namespace engine::safety
