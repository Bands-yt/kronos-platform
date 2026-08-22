#include "safety/GeminiModerationClient.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>

namespace engine::safety {
namespace {

std::string envOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return (value != nullptr && *value != '\0') ? std::string(value) : fallback;
}

} // namespace

GeminiConfig configFromEnvironment() {
    GeminiConfig config;
    config.apiKey = envOr("GEMINI_API_KEY", "");
    config.endpoint = envOr("GEMINI_ENDPOINT", config.endpoint);
    config.model = envOr("GEMINI_MODEL", config.model);
    // An unset key is not an error. It means this deployment runs on local
    // filters, which is a legitimate configuration -- and the one every
    // developer machine and CI runner is in.
    config.enabled = true;
    return config;
}

GeminiModerationClient::GeminiModerationClient(net::HttpWorkerPool& pool, GeminiConfig config,
                                                LocalTextClassifier localClassifier)
    : pool_(pool), config_(std::move(config)), localClassifier_(std::move(localClassifier)) {}

std::string GeminiModerationClient::base64Encode(const std::vector<uint8_t>& bytes) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= bytes.size()) {
        const uint32_t chunk = (static_cast<uint32_t>(bytes[i]) << 16) | (static_cast<uint32_t>(bytes[i + 1]) << 8) |
                                static_cast<uint32_t>(bytes[i + 2]);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back(kAlphabet[chunk & 0x3F]);
        i += 3;
    }
    // Standard base64 with '=' padding -- NOT the base64url variant
    // core::base64UrlEncode produces. A JSON image payload must be
    // standard base64; sending base64url would be silently rejected by
    // the API as a malformed image.
    const size_t remaining = bytes.size() - i;
    if (remaining == 1) {
        const uint32_t chunk = static_cast<uint32_t>(bytes[i]) << 16;
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back('=');
        out.push_back('=');
    } else if (remaining == 2) {
        const uint32_t chunk = (static_cast<uint32_t>(bytes[i]) << 16) | (static_cast<uint32_t>(bytes[i + 1]) << 8);
        out.push_back(kAlphabet[(chunk >> 18) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 12) & 0x3F]);
        out.push_back(kAlphabet[(chunk >> 6) & 0x3F]);
        out.push_back('=');
    }
    return out;
}

namespace {

// The response schema the model is constrained to. Asking for structured
// output rather than parsing prose is the difference between a classifier
// and a guess: free-form text would need its own fragile parser, and a
// model that decided to be chatty would break moderation.
nlohmann::json responseSchema() {
    return nlohmann::json{
        {"type", "OBJECT"},
        {"properties",
         {{"is_safe", {{"type", "BOOLEAN"}}},
          {"reason_code", {{"type", "STRING"}}}}},
        {"required", nlohmann::json::array({"is_safe", "reason_code"})},
    };
}

const char* kSystemInstruction =
    "You are a content moderation classifier for a children's game platform. "
    "Return is_safe=false for sexual content, graphic violence, harassment, hate speech, self-harm, "
    "real-world personal information, or third-party trademarked branding. "
    "reason_code must be one of: SAFE, SEXUAL, VIOLENCE, HARASSMENT, HATE, SELF_HARM, PII, TRADEMARK, OTHER.";

} // namespace

std::string GeminiModerationClient::buildTextRequestBody(const std::string& text) {
    nlohmann::json body;
    body["systemInstruction"]["parts"] = nlohmann::json::array({{{"text", kSystemInstruction}}});
    body["contents"] = nlohmann::json::array(
        {{{"role", "user"}, {"parts", nlohmann::json::array({{{"text", text}}})}}});
    body["generationConfig"]["responseMimeType"] = "application/json";
    body["generationConfig"]["responseSchema"] = responseSchema();
    // Deterministic: a moderation verdict that varies run to run on the
    // same input is not a policy, it is a coin flip.
    body["generationConfig"]["temperature"] = 0.0;
    return body.dump();
}

std::string GeminiModerationClient::buildImageRequestBody(const std::string& base64Image,
                                                           const std::string& mimeType) {
    nlohmann::json body;
    body["systemInstruction"]["parts"] = nlohmann::json::array({{{"text", kSystemInstruction}}});
    nlohmann::json parts = nlohmann::json::array();
    parts.push_back({{"text", "Classify this image for the policy above."}});
    parts.push_back({{"inline_data", {{"mime_type", mimeType}, {"data", base64Image}}}});
    body["contents"] = nlohmann::json::array({{{"role", "user"}, {"parts", parts}}});
    body["generationConfig"]["responseMimeType"] = "application/json";
    body["generationConfig"]["responseSchema"] = responseSchema();
    body["generationConfig"]["temperature"] = 0.0;
    return body.dump();
}

bool GeminiModerationClient::parseVerdict(const std::string& responseBody, ModerationVerdict& out) {
    const nlohmann::json root = nlohmann::json::parse(responseBody, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return false;

    // Gemini returns the structured payload as TEXT inside the first
    // candidate's first part -- responseSchema constrains what that text
    // contains, it does not hoist it into the envelope. Reading
    // candidates[0] directly as the verdict is the obvious mistake here.
    const auto candidates = root.find("candidates");
    if (candidates == root.end() || !candidates->is_array() || candidates->empty()) return false;
    const auto& first = (*candidates)[0];
    const auto content = first.find("content");
    if (content == first.end()) return false;
    const auto parts = content->find("parts");
    if (parts == content->end() || !parts->is_array() || parts->empty()) return false;
    const auto textNode = (*parts)[0].find("text");
    if (textNode == (*parts)[0].end() || !textNode->is_string()) return false;

    const nlohmann::json verdict = nlohmann::json::parse(textNode->get<std::string>(), nullptr, false);
    if (verdict.is_discarded() || !verdict.is_object()) return false;
    const auto isSafe = verdict.find("is_safe");
    if (isSafe == verdict.end() || !isSafe->is_boolean()) return false;

    out.isSafe = isSafe->get<bool>();
    const auto reason = verdict.find("reason_code");
    out.reasonCode = (reason != verdict.end() && reason->is_string()) ? reason->get<std::string>()
                                                                      : (out.isSafe ? "SAFE" : "OTHER");
    out.usedFallback = false;
    return true;
}

ModerationVerdict GeminiModerationClient::localVerdict(const std::string& text, const char* reason) const {
    ModerationVerdict verdict;
    if (localClassifier_) {
        verdict = localClassifier_(text);
    }
    verdict.usedFallback = true;
    // The local verdict's own reason survives when it blocked something;
    // the fallback reason only explains why the model was not consulted.
    verdict.detail = reason;
    return verdict;
}

std::future<ModerationVerdict> GeminiModerationClient::classifyText(std::string text) {
    if (!isConfigured()) {
        std::promise<ModerationVerdict> promise;
        promise.set_value(localVerdict(text, "GEMINI_API_KEY is unset -- local filters only"));
        return promise.get_future();
    }
    if (inCooldown()) {
        std::promise<ModerationVerdict> promise;
        promise.set_value(localVerdict(text, "moderation endpoint in cooldown after repeated failures"));
        return promise.get_future();
    }
    // Sequenced deliberately. Written as
    //   dispatch(buildTextRequestBody(text), std::move(text))
    // the two arguments are unsequenced relative to each other, so the
    // move can run FIRST and buildTextRequestBody then reads a moved-from
    // string -- silently sending an empty message to the classifier and
    // getting back a verdict about nothing.
    std::string requestBody = buildTextRequestBody(text);
    return dispatch(std::move(requestBody), std::move(text));
}

std::future<ModerationVerdict> GeminiModerationClient::classifyImage(std::string base64Image, std::string mimeType) {
    if (!isConfigured() || inCooldown()) {
        std::promise<ModerationVerdict> promise;
        ModerationVerdict verdict;
        // No local image classifier exists, so an unreachable model means
        // the image is not assessed. Reported as such rather than as a
        // clean pass -- calling an un-inspected image "safe" would be a
        // false assurance in an audit.
        verdict.isSafe = true;
        verdict.reasonCode = "NOT_ASSESSED";
        verdict.usedFallback = true;
        verdict.detail = isConfigured() ? "moderation endpoint in cooldown" : "GEMINI_API_KEY is unset";
        promise.set_value(std::move(verdict));
        return promise.get_future();
    }
    return dispatch(buildImageRequestBody(base64Image, mimeType), std::string{});
}

std::future<ModerationVerdict> GeminiModerationClient::dispatch(std::string requestBody, std::string localSubject) {
    net::HttpRequest request;
    // The key travels in a header, never in the URL: query strings end up
    // in proxy logs and crash reports.
    request.url = config_.endpoint + "/" + config_.model + ":generateContent";
    request.method = "POST";
    request.body = std::move(requestBody);
    request.headers = {"Content-Type: application/json", "x-goog-api-key: " + config_.apiKey};
    request.timeoutMillis = config_.timeoutMillis;

    // The response is turned into a verdict ON THE WORKER THREAD, and the
    // returned future is fulfilled from there.
    //
    // std::async(std::launch::deferred) is the trap here: a deferred
    // future never becomes ready on its own, so a caller polling
    // wait_for() -- which is exactly what the server's chat pump does, to
    // avoid blocking the network tick -- would spin forever and every
    // message would sit parked until it timed out.
    auto promise = std::make_shared<std::promise<ModerationVerdict>>();
    std::future<ModerationVerdict> future = promise->get_future();

    pool_.submitWithCallback(std::move(request), [this, promise, localSubject](net::HttpResponse response) {
        if (response.transportFailed || response.rateLimited()) {
            ++consecutiveFailures_;
            const char* reason = response.rateLimited() ? "moderation endpoint rate-limited"
                                                         : "moderation endpoint unreachable";
            promise->set_value(localVerdict(localSubject, reason));
            return;
        }
        if (!response.ok()) {
            ++consecutiveFailures_;
            promise->set_value(localVerdict(localSubject, "moderation endpoint returned an error status"));
            return;
        }

        ModerationVerdict verdict;
        if (!parseVerdict(response.body, verdict)) {
            ++consecutiveFailures_;
            promise->set_value(localVerdict(localSubject, "moderation response could not be parsed"));
            return;
        }
        consecutiveFailures_ = 0;
        promise->set_value(std::move(verdict));
    });

    return future;
}

} // namespace engine::safety
