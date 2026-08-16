#include "moderation/TrainingDataLog.hpp"

#include <fstream>
#include <sstream>

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

} // namespace engine::moderation
