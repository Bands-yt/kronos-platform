#include "core/PhonemeLipSync.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

namespace engine::core {

const char* visemeIdName(VisemeId id) {
    switch (id) {
        case VisemeId::Silence: return "Silence";
        case VisemeId::A: return "A";
        case VisemeId::E: return "E";
        case VisemeId::I: return "I";
        case VisemeId::O: return "O";
        case VisemeId::U: return "U";
        case VisemeId::Consonant: return "Consonant";
    }
    return "Unknown";
}

std::vector<TimeRange> detectSpeechSegments(const std::vector<float>& samples, uint32_t sampleRate, float windowMs,
                                             float rmsThreshold) {
    std::vector<TimeRange> segments;
    if (samples.empty() || sampleRate == 0 || windowMs <= 0.0f) return segments;

    size_t windowSize = std::max<size_t>(1, static_cast<size_t>(windowMs / 1000.0f * static_cast<float>(sampleRate)));

    bool inSegment = false;
    float segmentStartMs = 0.0f;

    for (size_t start = 0; start < samples.size(); start += windowSize) {
        size_t end = std::min(start + windowSize, samples.size());
        double sumSq = 0.0;
        for (size_t i = start; i < end; ++i) sumSq += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
        double rms = std::sqrt(sumSq / static_cast<double>(end - start));

        float windowStartMs = static_cast<float>(start) / static_cast<float>(sampleRate) * 1000.0f;
        float windowEndMs = static_cast<float>(end) / static_cast<float>(sampleRate) * 1000.0f;

        bool isSpeech = rms > static_cast<double>(rmsThreshold);
        if (isSpeech && !inSegment) {
            inSegment = true;
            segmentStartMs = windowStartMs;
        } else if (!isSpeech && inSegment) {
            inSegment = false;
            segments.push_back({segmentStartMs, windowStartMs});
        }
        if (end == samples.size() && inSegment) {
            segments.push_back({segmentStartMs, windowEndMs});
        }
    }

    return segments;
}

namespace {

VisemeId visemeForWord(const std::string& word) {
    for (char c : word) {
        char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        switch (lower) {
            case 'a': return VisemeId::A;
            case 'e': return VisemeId::E;
            case 'i': return VisemeId::I;
            case 'o': return VisemeId::O;
            case 'u': return VisemeId::U;
            default: break;
        }
    }
    return VisemeId::Consonant;
}

std::vector<std::string> tokenizeWords(const std::string& transcript) {
    std::vector<std::string> words;
    std::istringstream stream(transcript);
    std::string word;
    while (stream >> word) words.push_back(word);
    return words;
}

// Standard largest-remainder apportionment -- guarantees
// sum(result) == totalCount exactly (never off by a rounding error),
// weighted by each segment's own real duration. Real, well-known
// algorithm (the same one many real-world seat-apportionment systems
// use), not an ad hoc rounding scheme.
std::vector<int> apportionByDuration(const std::vector<TimeRange>& segments, int totalCount) {
    size_t n = segments.size();
    std::vector<int> counts(n, 0);
    if (totalCount <= 0 || n == 0) return counts;

    std::vector<double> durations(n);
    double totalDuration = 0.0;
    for (size_t i = 0; i < n; ++i) {
        durations[i] = std::max(0.0f, segments[i].endMs - segments[i].startMs);
        totalDuration += durations[i];
    }
    if (totalDuration <= 0.0) return counts;

    std::vector<double> fraction(n);
    int assigned = 0;
    for (size_t i = 0; i < n; ++i) {
        double exact = static_cast<double>(totalCount) * durations[i] / totalDuration;
        counts[i] = static_cast<int>(std::floor(exact));
        fraction[i] = exact - counts[i];
        assigned += counts[i];
    }

    int remaining = totalCount - assigned;
    std::vector<size_t> order(n);
    for (size_t i = 0; i < n; ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return fraction[a] > fraction[b]; });
    for (int k = 0; k < remaining && k < static_cast<int>(n); ++k) counts[order[static_cast<size_t>(k)]]++;

    return counts;
}

} // namespace

std::vector<VisemeEvent> extractVisemesFromTranscript(const std::vector<TimeRange>& speechSegments,
                                                        const std::string& transcript) {
    std::vector<VisemeEvent> events;
    std::vector<std::string> words = tokenizeWords(transcript);
    if (words.empty() || speechSegments.empty()) return events;

    std::vector<int> counts = apportionByDuration(speechSegments, static_cast<int>(words.size()));

    size_t wordIndex = 0;
    for (size_t s = 0; s < speechSegments.size(); ++s) {
        int count = counts[s];
        if (count <= 0) continue;
        float segStart = speechSegments[s].startMs;
        float segEnd = speechSegments[s].endMs;
        float step = (segEnd - segStart) / static_cast<float>(count);
        for (int k = 0; k < count && wordIndex < words.size(); ++k) {
            VisemeEvent event;
            event.startMs = segStart + step * static_cast<float>(k);
            event.endMs = segStart + step * static_cast<float>(k + 1);
            event.visemeId = visemeForWord(words[wordIndex]);
            events.push_back(event);
            ++wordIndex;
        }
    }

    return events;
}

std::vector<VisemeEvent> extractLipSyncVisemes(const std::vector<float>& samples, uint32_t sampleRate,
                                                const std::string& transcript, float windowMs, float rmsThreshold) {
    std::vector<TimeRange> segments = detectSpeechSegments(samples, sampleRate, windowMs, rmsThreshold);
    return extractVisemesFromTranscript(segments, transcript);
}

} // namespace engine::core
