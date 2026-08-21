#include "core/KronosApi.hpp"

#include <cstdio>
#include <utility>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "core/CredentialStore.hpp"

namespace engine::core {

namespace {

// Key under which the refresh token lives in the OS credential store.
// Versioned so a future format change can be introduced without silently
// mis-reading an old value.
constexpr const char* kRefreshTokenKey = "kronos_backend_refresh_token_v1";

size_t writeToString(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}

// Pulls a human-usable message out of the backend's own error envelope
// ({"error":{"code":...,"message":...}}), falling back to something
// honest rather than dumping raw JSON at the user.
std::string extractError(const std::string& body, long status) {
    if (!body.empty()) {
        nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
        if (!parsed.is_discarded() && parsed.contains("error") && parsed["error"].is_object()) {
            const auto& error = parsed["error"];
            if (error.contains("message") && error["message"].is_string()) {
                return error["message"].get<std::string>();
            }
        }
    }
    if (status == 0) return "Could not reach the Kronos service.";
    return "The Kronos service returned an unexpected error (HTTP " + std::to_string(status) + ").";
}

KronosUser parseUser(const nlohmann::json& node) {
    KronosUser user;
    user.id = node.value("id", std::string());
    user.email = node.value("email", std::string());
    user.displayName = node.value("display_name", std::string());
    user.emailVerified = node.value("email_verified", false);
    return user;
}

} // namespace

KronosApi::KronosApi(std::string baseUrl) : baseUrl_(std::move(baseUrl)) {}

void KronosApi::setBaseUrl(std::string baseUrl) {
    std::lock_guard<std::mutex> lock(mutex_);
    baseUrl_ = std::move(baseUrl);
}

bool KronosApi::isSignedIn() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !accessToken_.empty();
}

std::optional<KronosUser> KronosApi::currentUser() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return user_;
}

KronosApi::HttpResponse KronosApi::request(const char* method, const std::string& path, const std::string& jsonBody,
                                            bool withAuth) {
    HttpResponse response;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        response.error = "curl_easy_init() failed";
        return response;
    }

    std::string url;
    std::string bearer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        url = baseUrl_ + path;
        if (withAuth && !accessToken_.empty()) bearer = "Authorization: Bearer " + accessToken_;
    }

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    if (!bearer.empty()) headers = curl_slist_append(headers, bearer.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    if (!jsonBody.empty()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.size()));
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kronos-client");
    // TLS verification stays on. There is no "development" switch to turn
    // it off here on purpose: a disable-verification flag is exactly the
    // kind of thing that survives into a shipping build.
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode code = curl_easy_perform(curl);
    if (code == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
        response.transportOk = true;
    } else {
        response.error = curl_easy_strerror(code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return response;
}

KronosApi::HttpResponse KronosApi::requestWithRefresh(const char* method, const std::string& path,
                                                       const std::string& jsonBody) {
    HttpResponse response = request(method, path, jsonBody, /*withAuth=*/true);
    if (!response.transportOk || response.status != 401) return response;

    // The access token is short-lived by design, so a 401 mid-session is
    // expected rather than exceptional. Refresh once, then retry.
    KronosAuthResult refreshed = restoreSession();
    if (!refreshed.success) return response;
    return request(method, path, jsonBody, /*withAuth=*/true);
}

KronosAuthResult KronosApi::adoptSession(const HttpResponse& response) {
    KronosAuthResult result;
    if (!response.transportOk) {
        result.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        result.error = extractError(response.body, response.status);
        return result;
    }

    nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("access_token")) {
        result.error = "The Kronos service returned a response this build could not parse.";
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        accessToken_ = parsed.value("access_token", std::string());
        if (parsed.contains("user") && parsed["user"].is_object()) {
            user_ = parseUser(parsed["user"]);
            result.user = *user_;
        }
    }

    // The refresh token is the long-lived credential, so it is the one
    // that goes to the OS keychain. A failure here is surfaced rather
    // than swallowed: the user stays signed in for this session but will
    // have to sign in again next launch, and they deserve to know that.
    std::string refreshToken = parsed.value("refresh_token", std::string());
    if (!refreshToken.empty()) persistRefreshToken(refreshToken);

    result.success = true;
    return result;
}

void KronosApi::persistRefreshToken(const std::string& token) {
    if (!storeCredential(kRefreshTokenKey, token)) {
        std::fprintf(stderr,
                      "KronosApi: could not store the refresh token in the OS credential store -- you will need to "
                      "sign in again next launch.\n");
    }
}

std::string KronosApi::loadPersistedRefreshToken() const {
    std::string token;
    if (!loadCredential(kRefreshTokenKey, token)) return {};
    return token;
}

void KronosApi::clearPersistedRefreshToken() {
    if (!deleteCredential(kRefreshTokenKey)) {
        std::fprintf(stderr, "KronosApi: could not clear the stored refresh token.\n");
    }
}

KronosAuthResult KronosApi::signUp(const std::string& email, const std::string& password,
                                    const std::string& displayName) {
    nlohmann::json body{{"email", email}, {"password", password}};
    if (!displayName.empty()) body["display_name"] = displayName;
    return adoptSession(request("POST", "/v1/auth/signup", body.dump(), /*withAuth=*/false));
}

KronosAuthResult KronosApi::logIn(const std::string& email, const std::string& password) {
    nlohmann::json body{{"email", email}, {"password", password}};
    return adoptSession(request("POST", "/v1/auth/login", body.dump(), /*withAuth=*/false));
}

KronosAuthResult KronosApi::logInWithGoogle(const std::string& googleIdToken) {
    nlohmann::json body{{"id_token", googleIdToken}};
    return adoptSession(request("POST", "/v1/auth/google", body.dump(), /*withAuth=*/false));
}

KronosAuthResult KronosApi::restoreSession() {
    std::string refreshToken = loadPersistedRefreshToken();
    if (refreshToken.empty()) {
        KronosAuthResult result;
        result.error = "No saved Kronos session.";
        return result;
    }

    nlohmann::json body{{"refresh_token", refreshToken}};
    HttpResponse response = request("POST", "/v1/auth/refresh", body.dump(), /*withAuth=*/false);

    // A 401 here means the stored token is genuinely dead -- expired,
    // revoked, or invalidated by reuse detection on the server. Keeping
    // it would just fail again every launch, so it is cleared.
    if (response.transportOk && response.status == 401) {
        clearPersistedRefreshToken();
        std::lock_guard<std::mutex> lock(mutex_);
        accessToken_.clear();
        user_.reset();
    }
    return adoptSession(response);
}

void KronosApi::logOut() {
    std::string refreshToken = loadPersistedRefreshToken();
    if (!refreshToken.empty()) {
        nlohmann::json body{{"refresh_token", refreshToken}};
        // Best effort: if this fails the token stays valid server-side
        // until it expires, but the local session is cleared regardless
        // so "log out" is never a lie to the person who clicked it.
        (void)request("POST", "/v1/auth/logout", body.dump(), /*withAuth=*/false);
    }
    clearPersistedRefreshToken();
    std::lock_guard<std::mutex> lock(mutex_);
    accessToken_.clear();
    user_.reset();
}

CatalogueResult KronosApi::fetchGames(int limit, const std::string& cursor, const std::string& search) {
    std::string path = "/v1/catalog/games?limit=" + std::to_string(limit);
    if (!cursor.empty()) path += "&cursor=" + cursor;
    if (!search.empty()) {
        // Real URL-encoding via libcurl rather than pasting user input
        // straight into a query string.
        CURL* escaper = curl_easy_init();
        if (escaper != nullptr) {
            char* escaped = curl_easy_escape(escaper, search.c_str(), static_cast<int>(search.size()));
            if (escaped != nullptr) {
                path += "&q=" + std::string(escaped);
                curl_free(escaped);
            }
            curl_easy_cleanup(escaper);
        }
    }

    CatalogueResult result;
    HttpResponse response = requestWithRefresh("GET", path, {});
    if (!response.transportOk) {
        result.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        result.error = extractError(response.body, response.status);
        return result;
    }

    nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("games") || !parsed["games"].is_array()) {
        result.error = "The Kronos service returned a catalogue this build could not parse.";
        return result;
    }

    result.playerCountsAvailable = parsed.value("player_counts_available", false);
    for (const auto& node : parsed["games"]) {
        CatalogueGame game;
        game.id = node.value("id", std::string());
        game.slug = node.value("slug", std::string());
        game.title = node.value("title", std::string());
        game.description = node.value("description", std::string());
        game.thumbnailUrl = node.value("thumbnail_url", std::string());
        if (node.contains("creator") && node["creator"].is_object()) {
            game.creatorName = node["creator"].value("display_name", std::string());
        }
        // Stays -1 when the backend reported counts as unavailable, so a
        // "0 players" label is only ever drawn when it is really true.
        if (result.playerCountsAvailable && node.contains("active_players") &&
            node["active_players"].is_number_integer()) {
            game.activePlayers = node["active_players"].get<int>();
        }
        result.games.push_back(std::move(game));
    }
    if (parsed.contains("next_cursor") && parsed["next_cursor"].is_string()) {
        result.nextCursor = parsed["next_cursor"].get<std::string>();
    }

    result.success = true;
    return result;
}

ServerAllocation KronosApi::allocateServer(const std::string& gameSlug) {
    ServerAllocation allocation;

    nlohmann::json body{{"game_slug", gameSlug}};
    HttpResponse response = requestWithRefresh("POST", "/v1/sessions/allocate", body.dump());
    if (!response.transportOk) {
        allocation.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return allocation;
    }
    if (response.status < 200 || response.status >= 300) {
        allocation.error = extractError(response.body, response.status);
        return allocation;
    }

    nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded() || !parsed.contains("server") || !parsed["server"].is_object()) {
        allocation.error = "The Kronos service returned an allocation this build could not parse.";
        return allocation;
    }

    const auto& server = parsed["server"];
    allocation.host = server.value("host", std::string());
    allocation.port = static_cast<uint16_t>(server.value("port", 0));
    allocation.region = server.value("region", std::string());
    allocation.joinTicket = parsed.value("join_ticket", std::string());

    if (allocation.host.empty() || allocation.port == 0) {
        allocation.error = "The Kronos service returned an allocation with no usable address.";
        return allocation;
    }

    allocation.success = true;
    return allocation;
}

} // namespace engine::core
