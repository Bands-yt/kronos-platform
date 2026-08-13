#pragma once

#include <string>
#include <vector>

namespace engine::safety {

enum class TextRiskCategory { Harassment, SexualContent, PiiSolicitation, OffPlatformRedirect };

struct TextClassification {
    bool flagged = false;
    std::vector<TextRiskCategory> categories;
    float confidence = 0.0f;
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
    [[nodiscard]] static bool containsPiiSolicitationMarker(const std::string& message);
    [[nodiscard]] static bool containsOffPlatformRedirectMarker(const std::string& message);
};

} // namespace engine::safety
