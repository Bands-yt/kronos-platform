#pragma once

#include <string>

#include "safety/AssetSafetyGuard.hpp"
#include "safety/ImageClassifierStub.hpp"
#include "safety/IPInfringementScanner.hpp"
#include "safety/PolicyEngine.hpp"
#include "safety/TextClassifierStub.hpp"
#include "studio/IStudioPlugin.hpp"

namespace engine::studio::plugins {

// Kronos ("Moderation Architecture v2", item 5 "Creator Safety Tools"):
// SafeTextPreview/SafeThumbnailPreview/SafeScriptScan, one dedicated
// Studio panel -- a creator runs their own real listing text, thumbnail
// image, or script source through the exact same real heuristics
// safety::TrustSafetyService applies server-side (safety::
// TextClassifierStub/PolicyEngine, safety::AssetSafetyGuard/
// ImageClassifierStub), BEFORE publishing, rather than finding out only
// after a real player-facing rejection. Same real classifiers, same real
// honesty limits -- see each one's own header comment (TextClassifierStub.hpp/
// ImageClassifierStub.hpp) for exactly what "real" means (tiny keyword
// heuristics, not ML, not visual content understanding).
//
// SafeScriptScan is explicitly, plainly scoped: it runs a script's own
// literal source TEXT through the same real text heuristic as chat/
// listings (catching a harmful string a script embeds in a UI/dialogue
// line, e.g. `ui.showMessage("...")`), not a static-analysis or
// capability-sandboxing scan of what the script's CODE actually DOES --
// that's a categorically different, much larger engineering problem
// (real script-behavior security analysis) this doesn't attempt to
// solve, and claiming otherwise here would be exactly the kind of
// fabricated capability this whole safety:: layer's own honesty
// convention exists to avoid.
//
// Standalone -- no net::NetworkSession dependency, unlike
// ModerationPanel (this previews content that hasn't been sent/uploaded
// to any session yet).
class SafeContentPreviewPanel final : public IStudioPlugin {
public:
    [[nodiscard]] const char* name() const override { return "Safe Content Preview"; }
    [[nodiscard]] const char* category() const override { return "Moderation"; }

    void drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>& selectedEntities) override;

private:
    void drawSafeTextPreviewSection();
    void drawSafeThumbnailPreviewSection();
    void drawSafeScriptScanSection();

    safety::TextClassifierStub textClassifier_;
    safety::PolicyEngine policyEngine_;
    safety::AssetSafetyGuard assetGuard_;
    safety::ImageClassifierStub imageClassifier_;
    safety::IPInfringementScanner ipScanner_;

    char textPreviewBuffer_[512] = "";

    char thumbnailPathBuffer_[256] = "";
    std::string thumbnailScanStatus_;

    char scriptPathBuffer_[256] = "";
    std::string scriptScanStatus_;
};

} // namespace engine::studio::plugins
