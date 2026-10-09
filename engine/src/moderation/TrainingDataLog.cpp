#include "moderation/TrainingDataLog.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace engine::moderation {

std::string formatTrainingDataLine(const std::string& text, const safety::TextClassification& classification) {
    std::ostringstream line;
    line << "TEXT=\"" << text << "\" CATEGORIES=";
    for (size_t i = 0; i < classification.categories.size(); ++i) {
        if (i > 0) line << ',';
        line << safety::textRiskCategoryName(classification.categories[i]);
    }
    line << " CONFIDENCE=" << classification.confidence << " BLOCKED=" << (classification.blocked ? 1 : 0);
    return line.str();
}

bool appendTrainingDataSample(const std::string& path, const std::string& text,
                               const safety::TextClassification& classification) {
    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) return false;
    out << formatTrainingDataLine(text, classification) << "\n";
    return out.good();
}

std::string formatChatReviewLine(const ChatReviewSample& sample) {
    nlohmann::json categories = nlohmann::json::array();
    for (safety::TextRiskCategory category : sample.local.categories) {
        categories.push_back(safety::textRiskCategoryName(category));
    }
    nlohmann::json line = {
        {"time", sample.timeMillis},
        {"text", sample.text},
        {"shown", sample.shown},
        {"profanity", sample.profanity},
        {"local", {{"flagged", sample.local.flagged},
                   {"blocked", sample.local.blocked},
                   {"confidence", sample.local.confidence},
                   {"categories", categories}}},
        {"outcome", sample.outcome},
    };
    if (sample.geminiAsked) {
        line["gemini"] = {{"safe", sample.gemini.isSafe},
                          {"reason", sample.gemini.reasonCode},
                          {"fallback", sample.gemini.usedFallback},
                          {"detail", sample.gemini.detail}};
    } else {
        line["gemini"] = nullptr;
    }
    // Replace bad UTF-8 instead of throwing on odd chat input.
    return line.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

bool appendChatReviewSample(const std::string& path, const ChatReviewSample& sample) {
    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) return false;
    out << formatChatReviewLine(sample) << "\n";
    return out.good();
}

} // namespace engine::moderation
