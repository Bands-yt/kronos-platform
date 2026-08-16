#pragma once

#include <string>
#include <vector>

namespace engine::safety {

// Kronos ("Moderation Architecture v2", item A "Image Moderation"): the
// four categories the user's spec names.
enum class ImageRiskCategory {
    Nudity,
    Sexualization,
    Gore,
    ExtremistSymbols,
};

struct ImageClassification {
    bool flagged = false;
    std::vector<ImageRiskCategory> categories;
    float confidence = 0.0f;
};

// Structural stand-in for a real image-content classifier, same honesty
// contract as safety::TextClassifierStub (see that class's own header
// comment) -- there is NO vision model here, no pixel/frame decoding, no
// ML of any kind. Real content-understanding classification of nudity/
// sexualization/gore/extremist imagery needs a real trained vision model;
// this codebase has no ML inference runtime anywhere (confirmed by grep
// across the whole tree), and a naive pixel/color-ratio heuristic (e.g.
// "high skin-tone pixel percentage") was deliberately NOT built here --
// that specific technique is a well-documented false-positive generator
// against completely innocent photos (skin-toned wood, sand, sunsets),
// so shipping it would be actively misleading about what this class can
// detect, not just "tiny and crude" the way a keyword heuristic honestly
// is. See docs/ARCHITECTURE.md §10: real CSAM detection is explicitly
// scoped as hash-matching (PhotoDNA/Thorn Safer) against known-material
// databases, not a from-scratch classifier -- the same principle applies
// here: don't fabricate a detector for content this class structurally
// cannot see.
//
// What classify() below DOES do, honestly: the same tiny keyword-marker
// technique TextClassifierStub uses, applied to the one piece of text
// actually available about an uploaded image today -- its filename.
// This catches exactly what it sounds like it catches (an uploader who
// literally named the file something like "nsfw_final.png") and nothing
// about the image's actual visual content. Two real, stated future
// extensions, neither built here: (1) scanning embedded PNG tEXt/iTXt or
// JPEG COM metadata text the same way safety::AssetSafetyGuard already
// extracts it for IP-infringement scanning -- a small, real, additive
// change; (2) swapping in real ONNX Runtime vision-model inference over
// decoded pixels once one exists, which needs a genuinely new
// classify(pixels, width, height) overload, not a change to this one.
// Keeping the signature narrow (filename only) now rather than accepting
// unused pixel-buffer parameters is the honest choice -- an unused
// parameter would imply a capability this class doesn't have.
class ImageClassifierStub {
public:
    [[nodiscard]] ImageClassification classify(const std::string& filename) const;

private:
    // Deliberately tiny and not case/context-aware -- see the class
    // comment. Real word lists for each category are a trust & safety
    // content decision, not something to hardcode here.
    [[nodiscard]] static bool containsNudityMarker(const std::string& lowerFilename);
    [[nodiscard]] static bool containsSexualizationMarker(const std::string& lowerFilename);
    [[nodiscard]] static bool containsGoreMarker(const std::string& lowerFilename);
    [[nodiscard]] static bool containsExtremistSymbolMarker(const std::string& lowerFilename);
};

} // namespace engine::safety
