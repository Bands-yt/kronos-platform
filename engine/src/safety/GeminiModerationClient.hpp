#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <string>
#include <vector>

#include "net/HttpWorkerPool.hpp"

namespace engine::safety {

// Why a verdict came out the way it did. Mirrors the reason_code the
// model is asked to return, plus the codes this client produces itself
// when the model was not consulted.
struct ModerationVerdict {
    bool isSafe = true;
    std::string reasonCode = "SAFE";
    // True when the remote model was NOT what decided this -- no API key,
    // rate limited, unreachable, or a malformed response. The caller needs
    // this: "safe" from a real classifier and "safe because we could not
    // ask" are very different assurances, and a moderation audit that
    // cannot tell them apart is misleading.
    bool usedFallback = false;
    std::string detail;
};

// Local-only classification, used both as the fallback and as the
// synchronous first pass. Supplied by the caller so this client does not
// have to know about ProfanityFilter, TextClassifier or the hash
// registry -- and so tests can drive the fallback deterministically.
using LocalTextClassifier = std::function<ModerationVerdict(const std::string& text)>;

struct GeminiConfig {
    // Read from the GEMINI_API_KEY environment variable by
    // configFromEnvironment(). Empty means "not configured", which is a
    // supported, non-error state: the pipeline runs on local filters.
    std::string apiKey;
    // Overridable so tests point at a local mock server instead of
    // Google's endpoint. Same reason the Kronos API base URL is
    // overridable.
    std::string endpoint = "https://generativelanguage.googleapis.com/v1beta/models";
    std::string model = "gemini-flash-lite-latest";
    // Chat is latency-sensitive; a moderation call that takes longer than
    // this is abandoned in favour of the local verdict. Bounding this is
    // what keeps a slow upstream from turning into unusable chat.
    long timeoutMillis = 1500;
    bool enabled = true;
};

// Reads GEMINI_API_KEY (and the optional GEMINI_ENDPOINT / GEMINI_MODEL
// overrides) from the environment.
[[nodiscard]] GeminiConfig configFromEnvironment();

// Client for Gemini Flash-Lite moderation, text and vision.
//
// Every path degrades to the local classifier rather than failing:
// no key, rate limited, unreachable, timed out, or a response that does
// not parse. That is deliberate -- moderation being unavailable must
// never mean chat stops working, and it must never mean unmoderated
// content ships either. The local filter is the floor, the model is the
// improvement on top.
class GeminiModerationClient {
public:
    GeminiModerationClient(net::HttpWorkerPool& pool, GeminiConfig config, LocalTextClassifier localClassifier);

    [[nodiscard]] bool isConfigured() const { return config_.enabled && !config_.apiKey.empty(); }
    [[nodiscard]] const GeminiConfig& config() const { return config_; }
    void setConfig(GeminiConfig config) { config_ = std::move(config); }

    // Asynchronous. The future always resolves -- with the model's verdict
    // when it answers in time, otherwise with the local one.
    [[nodiscard]] std::future<ModerationVerdict> classifyText(std::string text);

    // Vision path. `base64Image` is the raw image, base64-encoded;
    // `mimeType` is e.g. "image/png".
    [[nodiscard]] std::future<ModerationVerdict> classifyImage(std::string base64Image, std::string mimeType);

    // Consecutive rate-limit or transport failures. Once this passes the
    // threshold the client stops calling out entirely for a cooldown,
    // rather than hammering an endpoint that is already shedding load.
    [[nodiscard]] uint32_t consecutiveFailures() const { return consecutiveFailures_; }
    [[nodiscard]] bool inCooldown() const { return consecutiveFailures_ >= kFailureCooldownThreshold; }
    void resetFailures() { consecutiveFailures_ = 0; }
    static constexpr uint32_t kFailureCooldownThreshold = 5;

    // Exposed for tests and for the asset pipeline.
    [[nodiscard]] static std::string base64Encode(const std::vector<uint8_t>& bytes);
    // Builds the request body Gemini's structured-output API expects.
    [[nodiscard]] static std::string buildTextRequestBody(const std::string& text);
    [[nodiscard]] static std::string buildImageRequestBody(const std::string& base64Image, const std::string& mimeType);
    // Parses the model's structured JSON reply. Returns false when the
    // response is not a verdict this client can trust.
    [[nodiscard]] static bool parseVerdict(const std::string& responseBody, ModerationVerdict& out);

private:
    [[nodiscard]] std::future<ModerationVerdict> dispatch(std::string requestBody, std::string localSubject);
    [[nodiscard]] ModerationVerdict localVerdict(const std::string& text, const char* reason) const;

    net::HttpWorkerPool& pool_;
    GeminiConfig config_;
    LocalTextClassifier localClassifier_;
    std::atomic<uint32_t> consecutiveFailures_{0};
};

} // namespace engine::safety
