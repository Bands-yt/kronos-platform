#include "core/GoogleAuth.hpp"

#include <cstdio>
#include <random>
#include <sstream>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "core/LoopbackHttpServer.hpp"
#include "core/OAuthPkce.hpp"
#include "core/OpenUrl.hpp"

namespace engine::core {

namespace {
constexpr const char* kAuthorizationEndpoint = "https://accounts.google.com/o/oauth2/v2/auth";
constexpr const char* kTokenEndpoint = "https://oauth2.googleapis.com/token";
constexpr const char* kPlaceholderClientId = "YOUR_GOOGLE_OAUTH_CLIENT_ID.apps.googleusercontent.com";

// Real, small, random CSRF-protection token -- checked against the
// real `state` query param the loopback redirect carries back, so this
// process only ever accepts a real redirect it itself initiated (not,
// say, a stray/malicious local connection to the same real loopback
// port racing the real browser redirect).
std::string generateState() {
    std::random_device rd;
    std::uniform_int_distribution<int> dist(0, 15);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string state;
    for (int i = 0; i < 32; ++i) state += kHex[dist(rd)];
    return state;
}

std::string redirectUri(uint16_t port) { return "http://localhost:" + std::to_string(port) + "/auth/callback"; }

size_t curlWriteCallback(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}

// Real, small libcurl POST helper -- both signIn()'s own token exchange
// and googleRefreshAccessToken() share this exact real request shape
// (application/x-www-form-urlencoded body, one POST, capture the
// response body regardless of HTTP status so a real error JSON body
// can still be parsed and reported). Real, honest failure: returns an
// empty string and sets `outError` on any real libcurl/transport-level
// failure -- callers still need to check the real HTTP status
// separately via `outHttpStatus`.
std::string httpPostForm(const std::string& url, const std::string& formBody, long& outHttpStatus,
                          std::string& outError) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        outError = "curl_easy_init() failed";
        return {};
    }

    std::string responseBody;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, formBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(formBody.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    // Real, deliberate: this is a one-shot, blocking, foreground sign-in
    // request, not a background poll -- a real, bounded timeout so a
    // dead network doesn't hang this thread forever.
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        outError = curl_easy_strerror(result);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &outHttpStatus);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return responseBody;
}

std::string urlEncode(const std::string& value) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return value;
    char* encoded = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
    std::string out = encoded != nullptr ? encoded : value;
    if (encoded != nullptr) curl_free(encoded);
    curl_easy_cleanup(curl);
    return out;
}

// Real, best-effort JWT payload decode -- see GoogleAuth.hpp's own
// header comment on why this deliberately does NOT verify the JWT's
// signature. A malformed/unexpected token shape is a real, honest
// no-op (email/displayName stay empty), never a crash.
void decodeIdTokenClaims(const std::string& idToken, std::string& outSubject, std::string& outEmail,
                          std::string& outDisplayName) {
    size_t firstDot = idToken.find('.');
    if (firstDot == std::string::npos) return;
    size_t secondDot = idToken.find('.', firstDot + 1);
    if (secondDot == std::string::npos) return;
    std::string payloadSegment = idToken.substr(firstDot + 1, secondDot - firstDot - 1);

    std::string payloadJson = base64UrlDecode(payloadSegment);
    try {
        nlohmann::json payload = nlohmann::json::parse(payloadJson);
        if (payload.contains("sub") && payload["sub"].is_string()) outSubject = payload["sub"].get<std::string>();
        if (payload.contains("email") && payload["email"].is_string()) outEmail = payload["email"].get<std::string>();
        if (payload.contains("name") && payload["name"].is_string()) outDisplayName = payload["name"].get<std::string>();
    } catch (const nlohmann::json::parse_error&) {
        // Real, honest no-op -- see this function's own header comment.
    }
}

GoogleAuthResult parseTokenResponse(const std::string& body, long httpStatus) {
    GoogleAuthResult result;
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(body);
    } catch (const nlohmann::json::parse_error&) {
        result.error = "Google's token endpoint returned a non-JSON response (HTTP " + std::to_string(httpStatus) + ")";
        return result;
    }

    if (httpStatus != 200) {
        result.error = json.contains("error_description") ? json["error_description"].get<std::string>()
                        : json.contains("error")           ? json["error"].get<std::string>()
                                                             : ("HTTP " + std::to_string(httpStatus));
        return result;
    }

    if (json.contains("access_token")) result.accessToken = json["access_token"].get<std::string>();
    if (json.contains("refresh_token")) result.refreshToken = json["refresh_token"].get<std::string>();
    if (json.contains("id_token")) result.idToken = json["id_token"].get<std::string>();
    if (result.idToken.empty() || result.accessToken.empty()) {
        result.error = "Google's token response was missing a real access_token/id_token";
        return result;
    }

    decodeIdTokenClaims(result.idToken, result.subject, result.email, result.displayName);
    result.success = true;
    return result;
}
} // namespace

GoogleAuthResult googleSignIn(const GoogleAuthConfig& config, float timeoutSeconds) {
    GoogleAuthResult result;
    if (config.clientId == kPlaceholderClientId || config.clientId.empty()) {
        result.error =
            "GoogleAuthConfig::clientId is still the placeholder -- register a real Google Cloud OAuth Client ID "
            "(Application type: Desktop app) and set it before calling googleSignIn().";
        return result;
    }

    std::string verifier = generateCodeVerifier();
    std::string challenge = deriveCodeChallenge(verifier);
    std::string state = generateState();
    std::string redirect = redirectUri(config.loopbackPort);

    LoopbackHttpServer server;
    if (!server.start(config.loopbackPort)) {
        result.error = "could not start the local loopback listener on port " + std::to_string(config.loopbackPort) +
                        " -- it may already be in use by another process";
        return result;
    }

    std::ostringstream authUrl;
    authUrl << kAuthorizationEndpoint << "?response_type=code" << "&client_id=" << urlEncode(config.clientId)
            << "&redirect_uri=" << urlEncode(redirect) << "&scope=" << urlEncode(config.scope)
            << "&state=" << urlEncode(state) << "&code_challenge=" << urlEncode(challenge)
            << "&code_challenge_method=S256" << "&access_type=offline" << "&prompt=consent";

    if (!openUrlInDefaultBrowser(authUrl.str())) {
        result.error = "could not open the system browser";
        return result;
    }

    LoopbackCallbackResult callback = server.waitForCallback(timeoutSeconds);
    server.stop();
    if (!callback.success) {
        result.error = callback.error;
        return result;
    }
    if (callback.state != state) {
        result.error = "the redirect's state parameter didn't match -- possible CSRF, aborting";
        return result;
    }

    std::ostringstream form;
    form << "grant_type=authorization_code" << "&code=" << urlEncode(callback.code)
         << "&redirect_uri=" << urlEncode(redirect) << "&client_id=" << urlEncode(config.clientId)
         << "&code_verifier=" << urlEncode(verifier);

    long httpStatus = 0;
    std::string transportError;
    std::string body = httpPostForm(kTokenEndpoint, form.str(), httpStatus, transportError);
    if (!transportError.empty()) {
        result.error = "token exchange request failed: " + transportError;
        return result;
    }
    return parseTokenResponse(body, httpStatus);
}

GoogleAuthResult googleRefreshAccessToken(const GoogleAuthConfig& config, const std::string& refreshToken) {
    GoogleAuthResult result;
    if (refreshToken.empty()) {
        result.error = "no real refresh token was supplied";
        return result;
    }

    std::ostringstream form;
    form << "grant_type=refresh_token" << "&refresh_token=" << urlEncode(refreshToken)
         << "&client_id=" << urlEncode(config.clientId);

    long httpStatus = 0;
    std::string transportError;
    std::string body = httpPostForm(kTokenEndpoint, form.str(), httpStatus, transportError);
    if (!transportError.empty()) {
        result.error = "token refresh request failed: " + transportError;
        return result;
    }
    GoogleAuthResult refreshed = parseTokenResponse(body, httpStatus);
    // Real: a refresh grant doesn't re-issue a refresh_token -- the
    // real, original one (the caller's own, from CredentialStore)
    // stays valid and should keep being used, not overwritten with an
    // empty string.
    if (refreshed.success && refreshed.refreshToken.empty()) refreshed.refreshToken = refreshToken;
    return refreshed;
}

} // namespace engine::core
