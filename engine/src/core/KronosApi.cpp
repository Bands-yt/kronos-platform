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

// nlohmann's value() THROWS on a present-but-null field rather than
// returning the default, and a guest account legitimately has a null
// email -- so a plain value("email", ...) crashes the launcher the moment
// somebody plays as a guest. Found by the end-to-end browser sign-in
// harness. Every field is read defensively here for the same reason: a
// client must never abort because the server sent a null it is entitled
// to send.
std::string jsonStringOr(const nlohmann::json& node, const char* key, const std::string& fallback = {}) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_string()) return fallback;
    return it->get<std::string>();
}

KronosUser parseUser(const nlohmann::json& node) {
    KronosUser user;
    user.id = jsonStringOr(node, "id");
    user.email = jsonStringOr(node, "email");
    user.displayName = jsonStringOr(node, "display_name");
    auto verified = node.find("email_verified");
    user.emailVerified = verified != node.end() && verified->is_boolean() && verified->get<bool>();
    return user;
}

} // namespace

bool verifyJoinTicketWithKronos(const std::string& baseUrl, const std::string& serverKey, const std::string& ticket,
                                 uint64_t& outUserId) {
    outUserId = 0;
    if (ticket.empty()) return false;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) return false;

    nlohmann::json requestBody{{"join_ticket", ticket}, {"server_key", serverKey}};
    std::string payload = requestBody.dump();
    std::string responseBody;
    std::string url = baseUrl + "/v1/sessions/verify-ticket";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kronos-gameserver");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    // Kronos ("SSL connect error" on Windows): this build links curl via
    // CMake's find_package(CURL) -- on Linux that resolves to the
    // distro's OpenSSL-backed package, whose default CA path
    // (/etc/ssl/certs, verified directly against this exact server:
    // `curl -v https://kronosplatform.com` completes a real TLS 1.3
    // handshake and reports "SSL certificate verified via OpenSSL")
    // already works with no configuration. Windows builds resolve curl
    // through vcpkg (`vcpkg install curl:x64-windows`, see
    // .github/workflows/build.yml), which -- unlike the Linux package --
    // has no OS-integrated trust store to fall back on when built
    // against OpenSSL rather than Schannel; this is a real, widely
    // reported vcpkg-curl-on-Windows gap, not a hypothetical one. Rather
    // than gamble on which TLS backend that vcpkg build actually
    // resolved to (not verifiable from this Linux environment),
    // CURLSSLOPT_NATIVE_CA asks curl to also trust the OS's own
    // certificate store when its backend supports that (OpenSSL,
    // GnuTLS, mbedTLS, wolfSSL); curl silently ignores it for backends
    // that don't (Schannel already uses the OS store natively either
    // way), so this is safe to set unconditionally on every platform,
    // not just Windows.
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
    // A join handshake is interactive: a player is sitting on a loading
    // screen. Bounded tightly so an unreachable backend fails fast (and
    // closed) instead of hanging the connection attempt.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK || status != 200) return false;

    nlohmann::json parsed = nlohmann::json::parse(responseBody, nullptr, false);
    if (parsed.is_discarded() || parsed.value("valid", false) != true) return false;

    // user_id comes back as a string (the backend stringifies 64-bit ids
    // so JSON number precision can never silently mangle them).
    std::string userId = parsed.value("user_id", std::string());
    if (userId.empty()) return false;
    try {
        outUserId = std::stoull(userId);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

bool sendServerHeartbeat(const std::string& baseUrl, const std::string& serverKey, size_t playerCount) {
    CURL* curl = curl_easy_init();
    if (curl == nullptr) return false;

    nlohmann::json requestBody{{"players", playerCount}};
    std::string payload = requestBody.dump();
    std::string responseBody;
    std::string url = baseUrl + "/v1/sessions/servers/" + serverKey + "/heartbeat";

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kronos-gameserver");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    // See verifyJoinTicketWithKronos's own comment on why this is safe
    // to set unconditionally on every platform.
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

    CURLcode code = curl_easy_perform(curl);
    long status = 0;
    if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return code == CURLE_OK && status == 200;
}

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
    // Real Windows-CA-bundle robustness, not decoration -- see the first
    // curl handle in this file (verifyJoinTicketWithKronos, above) for
    // the full explanation. This is the function that actually placed
    // every real /v1/* call this session traced the reported
    // "SSL connect error" to.
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
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

KronosAuthResult KronosApi::completeBrowserSignIn(const std::string& refreshToken) {
    KronosAuthResult result;
    if (refreshToken.empty()) {
        result.error = "The browser did not return a sign-in token.";
        return result;
    }

    // Exchange the token we were just handed DIRECTLY.
    //
    // This deliberately does not persist-then-read-back through
    // CredentialStore: on any machine where the OS keychain is
    // unavailable (a headless Linux box, a locked keyring, a container)
    // the read-back returns nothing and sign-in would fail even though
    // the token is perfectly good. Found by the end-to-end browser
    // sign-in harness, which runs in exactly such an environment.
    //
    // Persistence is a separate, best-effort concern: failing to save it
    // costs the user a re-login next launch, which is a far smaller
    // problem than not being able to sign in at all.
    result = exchangeRefreshToken(refreshToken);
    if (result.success) persistRefreshToken(refreshToken);
    return result;
}

// Shared by both the browser hand-off above and restoreSession() below,
// so there is one refresh-exchange implementation rather than two that
// can drift.
KronosAuthResult KronosApi::exchangeRefreshToken(const std::string& refreshToken) {
    nlohmann::json body{{"refresh_token", refreshToken}};
    HttpResponse response = request("POST", "/v1/auth/refresh", body.dump(), /*withAuth=*/false);
    return adoptSession(response);
}

KronosAuthResult KronosApi::exchangeHandoffCode(const std::string& code) {
    KronosAuthResult result;
    if (code.empty()) {
        result.error = "No hand-off code was provided.";
        return result;
    }
    nlohmann::json body{{"code", code}};
    // /*withAuth=*/false: this call IS the auth bootstrap -- this
    // process has no session yet, that is the entire reason the code
    // exists. adoptSession() below persists the resulting refresh token
    // exactly like every other successful sign-in does, so every launch
    // after this first one resumes from the OS credential store the
    // normal way and never needs a fresh code at all.
    HttpResponse response = request("POST", "/v1/auth/handoff/exchange", body.dump(), /*withAuth=*/false);
    return adoptSession(response);
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
        game.id = jsonStringOr(node, "id");
        game.slug = jsonStringOr(node, "slug");
        game.title = jsonStringOr(node, "title");
        game.description = jsonStringOr(node, "description");
        game.thumbnailUrl = jsonStringOr(node, "thumbnail_url");
        if (node.contains("creator") && node["creator"].is_object()) {
            game.creatorName = jsonStringOr(node["creator"], "display_name");
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
    allocation.host = jsonStringOr(server, "host");
    auto portNode = server.find("port");
    allocation.port = (portNode != server.end() && portNode->is_number_integer())
                           ? static_cast<uint16_t>(portNode->get<int>())
                           : 0;
    allocation.region = jsonStringOr(server, "region");
    allocation.serverKey = jsonStringOr(server, "server_key");
    allocation.joinTicket = jsonStringOr(parsed, "join_ticket");

    if (allocation.host.empty() || allocation.port == 0) {
        allocation.error = "The Kronos service returned an allocation with no usable address.";
        return allocation;
    }

    allocation.success = true;
    return allocation;
}


namespace {
KronosFriend parseFriend(const nlohmann::json& node) {
    KronosFriend entry;
    entry.id = jsonStringOr(node, "id");
    entry.username = jsonStringOr(node, "username");
    entry.displayName = jsonStringOr(node, "display_name");
    entry.status = jsonStringOr(node, "status", "offline");
    entry.currentGameId = jsonStringOr(node, "current_game_id");
    entry.currentServerId = jsonStringOr(node, "current_server_id");
    entry.joinTicket = jsonStringOr(node, "join_ticket");
    return entry;
}
} // namespace

FriendsResult KronosApi::fetchFriends() {
    FriendsResult result;
    HttpResponse response = requestWithRefresh("GET", "/v1/friends/list", {});
    if (!response.transportOk) {
        result.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        result.error = extractError(response.body, response.status);
        return result;
    }
    nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded()) {
        result.error = "The Kronos service returned a friends list this build could not parse.";
        return result;
    }
    auto readList = [](const nlohmann::json& node, const char* key, std::vector<KronosFriend>& out) {
        auto it = node.find(key);
        if (it == node.end() || !it->is_array()) return;
        for (const auto& entry : *it) out.push_back(parseFriend(entry));
    };
    readList(parsed, "friends", result.friends);
    readList(parsed, "incoming_requests", result.incomingRequests);
    readList(parsed, "outgoing_requests", result.outgoingRequests);
    auto available = parsed.find("presence_available");
    result.presenceAvailable = available != parsed.end() && available->is_boolean() && available->get<bool>();
    result.success = true;
    return result;
}

UserSearchResponse KronosApi::searchUsers(const std::string& queryText) {
    UserSearchResponse result;
    if (queryText.size() < 3) {
        // Matches the server's own minimum, so a too-short query never
        // becomes a pointless round trip.
        result.error = "Enter at least 3 characters to search.";
        return result;
    }

    std::string path = "/v1/users/search?q=";
    CURL* escaper = curl_easy_init();
    if (escaper != nullptr) {
        char* escaped = curl_easy_escape(escaper, queryText.c_str(), static_cast<int>(queryText.size()));
        if (escaped != nullptr) {
            path += escaped;
            curl_free(escaped);
        }
        curl_easy_cleanup(escaper);
    }

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
    auto results = parsed.is_discarded() ? parsed.end() : parsed.find("results");
    if (parsed.is_discarded() || results == parsed.end() || !results->is_array()) {
        result.error = "The Kronos service returned search results this build could not parse.";
        return result;
    }
    for (const auto& node : *results) {
        UserSearchResult entry;
        entry.id = jsonStringOr(node, "id");
        entry.username = jsonStringOr(node, "username");
        entry.displayName = jsonStringOr(node, "display_name");
        entry.relationship = jsonStringOr(node, "relationship", "none");
        result.results.push_back(std::move(entry));
    }
    result.success = true;
    return result;
}

bool KronosApi::sendFriendRequest(const std::string& userId, std::string& outError) {
    nlohmann::json body{{"user_id", userId}};
    HttpResponse response = requestWithRefresh("POST", "/v1/friends/request", body.dump());
    if (!response.transportOk) {
        outError = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        outError = extractError(response.body, response.status);
        return false;
    }
    return true;
}

bool KronosApi::respondToFriendRequest(const std::string& userId, bool accept, std::string& outError) {
    nlohmann::json body{{"user_id", userId}, {"accept", accept}};
    HttpResponse response = requestWithRefresh("POST", "/v1/friends/respond", body.dump());
    if (!response.transportOk) {
        outError = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        outError = extractError(response.body, response.status);
        return false;
    }
    return true;
}

bool KronosApi::removeFriend(const std::string& userId, std::string& outError) {
    HttpResponse response = requestWithRefresh("DELETE", "/v1/friends/" + userId, {});
    if (!response.transportOk) {
        outError = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        outError = extractError(response.body, response.status);
        return false;
    }
    return true;
}

bool KronosApi::followUser(const std::string& userId, std::string& outError) {
    HttpResponse response = requestWithRefresh("POST", "/v1/follows/" + userId, "{}");
    if (!response.transportOk) {
        outError = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        outError = extractError(response.body, response.status);
        return false;
    }
    return true;
}

bool KronosApi::unfollowUser(const std::string& userId, std::string& outError) {
    HttpResponse response = requestWithRefresh("DELETE", "/v1/follows/" + userId, {});
    if (!response.transportOk) {
        outError = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return false;
    }
    if (response.status < 200 || response.status >= 300) {
        outError = extractError(response.body, response.status);
        return false;
    }
    return true;
}

namespace {
FollowListResult parseFollowList(const std::string& body, const char* listKey) {
    FollowListResult result;
    nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    auto list = parsed.is_discarded() ? parsed.end() : parsed.find(listKey);
    if (parsed.is_discarded() || list == parsed.end() || !list->is_array()) {
        result.error = "The Kronos service returned a follow list this build could not parse.";
        return result;
    }
    for (const auto& node : *list) {
        FollowListUser entry;
        entry.id = jsonStringOr(node, "id");
        entry.username = jsonStringOr(node, "username");
        entry.displayName = jsonStringOr(node, "display_name");
        entry.directoryName = jsonStringOr(node, "directory_name");
        entry.status = jsonStringOr(node, "status", "offline");
        entry.currentGameId = jsonStringOr(node, "current_game_id");
        auto viewerFollows = node.find("viewer_is_following");
        entry.viewerIsFollowing = viewerFollows != node.end() && viewerFollows->is_boolean() && viewerFollows->get<bool>();
        result.users.push_back(std::move(entry));
    }
    result.nextCursor = jsonStringOr(parsed, "next_cursor");
    auto available = parsed.find("presence_available");
    result.presenceAvailable = available != parsed.end() && available->is_boolean() && available->get<bool>();
    result.success = true;
    return result;
}
} // namespace

FollowListResult KronosApi::fetchFollowers(const std::string& userId, int limit, const std::string& cursor) {
    std::string path = "/v1/follows/" + userId + "/followers?limit=" + std::to_string(limit);
    if (!cursor.empty()) path += "&cursor=" + cursor;
    HttpResponse response = requestWithRefresh("GET", path, {});
    if (!response.transportOk) {
        FollowListResult result;
        result.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        FollowListResult result;
        result.error = extractError(response.body, response.status);
        return result;
    }
    return parseFollowList(response.body, "followers");
}

FollowListResult KronosApi::fetchFollowing(const std::string& userId, int limit, const std::string& cursor) {
    std::string path = "/v1/follows/" + userId + "/following?limit=" + std::to_string(limit);
    if (!cursor.empty()) path += "&cursor=" + cursor;
    HttpResponse response = requestWithRefresh("GET", path, {});
    if (!response.transportOk) {
        FollowListResult result;
        result.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        FollowListResult result;
        result.error = extractError(response.body, response.status);
        return result;
    }
    return parseFollowList(response.body, "following");
}

FollowCounts KronosApi::fetchFollowCounts(const std::string& userId) {
    FollowCounts result;
    HttpResponse response = requestWithRefresh("GET", "/v1/follows/" + userId + "/counts", {});
    if (!response.transportOk) {
        result.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        result.error = extractError(response.body, response.status);
        return result;
    }
    nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded()) {
        result.error = "The Kronos service returned follow counts this build could not parse.";
        return result;
    }
    result.followers = parsed.value("followers", 0);
    result.following = parsed.value("following", 0);
    result.success = true;
    return result;
}

void KronosApi::sendPresenceHeartbeat(const std::string& status, const std::string& gameId,
                                       const std::string& serverId) {
    nlohmann::json body{{"status", status}};
    if (!gameId.empty()) body["current_game_id"] = gameId;
    if (!serverId.empty()) body["current_server_id"] = serverId;
    // Best effort by design: a dropped heartbeat only means friends see
    // this user go offline a little early, which is self-correcting.
    (void)requestWithRefresh("POST", "/v1/presence/heartbeat", body.dump());
}


DirectoryResult KronosApi::fetchUserDirectory(int limit, const std::string& cursor) {
    DirectoryResult result;
    std::string path = "/v1/users?limit=" + std::to_string(limit);
    if (!cursor.empty()) path += "&cursor=" + cursor;

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
    auto users = parsed.is_discarded() ? parsed.end() : parsed.find("users");
    if (parsed.is_discarded() || users == parsed.end() || !users->is_array()) {
        result.error = "The Kronos service returned a directory this build could not parse.";
        return result;
    }
    for (const auto& node : *users) {
        DirectoryUser entry;
        entry.id = jsonStringOr(node, "id");
        entry.username = jsonStringOr(node, "username");
        entry.displayName = jsonStringOr(node, "display_name");
        entry.directoryName = jsonStringOr(node, "directory_name");
        if (entry.directoryName.empty()) {
            // Older server without the field: fall back locally rather
            // than rendering a blank row.
            entry.directoryName = entry.username.empty() ? entry.displayName : entry.username;
        }
        auto hasUsername = node.find("has_username");
        entry.hasUsername = hasUsername != node.end() && hasUsername->is_boolean() && hasUsername->get<bool>();
        entry.status = jsonStringOr(node, "status", "offline");
        entry.currentGameId = jsonStringOr(node, "current_game_id");
        result.users.push_back(std::move(entry));
    }
    result.nextCursor = jsonStringOr(parsed, "next_cursor");
    auto available = parsed.find("presence_available");
    result.presenceAvailable = available != parsed.end() && available->is_boolean() && available->get<bool>();
    result.success = true;
    return result;
}

PresenceSummary KronosApi::fetchPresenceSummary() {
    PresenceSummary summary;
    HttpResponse response = requestWithRefresh("GET", "/v1/presence/summary", {});
    if (!response.transportOk) {
        summary.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return summary;
    }
    if (response.status < 200 || response.status >= 300) {
        summary.error = extractError(response.body, response.status);
        return summary;
    }
    nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded()) {
        summary.error = "The Kronos service returned a summary this build could not parse.";
        return summary;
    }
    auto readInt = [&parsed](const char* key) {
        auto it = parsed.find(key);
        return (it != parsed.end() && it->is_number_integer()) ? it->get<int>() : 0;
    };
    auto availableIt = parsed.find("available");
    summary.available = availableIt != parsed.end() && availableIt->is_boolean() && availableIt->get<bool>();
    summary.offline = readInt("offline");
    summary.onlineLauncher = readInt("online_launcher");
    summary.inStudio = readInt("in_studio");
    summary.inGame = readInt("in_game");
    summary.totalOnline = readInt("total_online");
    summary.registeredAccounts = readInt("registered_accounts");
    summary.success = true;
    return summary;
}

namespace {
AvatarConfig parseAvatarConfig(const std::string& body) {
    AvatarConfig config;
    nlohmann::json parsed = nlohmann::json::parse(body, nullptr, false);
    if (parsed.is_discarded()) {
        config.error = "The Kronos service returned an avatar config this build could not parse.";
        return config;
    }
    auto readInt = [&parsed](const char* key, int fallback) {
        auto it = parsed.find(key);
        return (it != parsed.end() && it->is_number_integer()) ? it->get<int>() : fallback;
    };
    auto readFloat = [&parsed](const char* key, float fallback) {
        auto it = parsed.find(key);
        return (it != parsed.end() && it->is_number()) ? it->get<float>() : fallback;
    };
    config.skinToneIndex = readInt("skin_tone_index", -1);
    config.headShapeIndex = readInt("head_shape_index", 0);
    config.bodyHeight = readFloat("body_height", 1.0f);
    config.bodyWidth = readFloat("body_width", 1.0f);
    config.bodyLimbScale = readFloat("body_limb_scale", 1.0f);
    config.bodyTorsoLength = readFloat("body_torso_length", 1.0f);
    config.bodyShoulderWidth = readFloat("body_shoulder_width", 1.0f);
    config.clothingFitIndex = readInt("clothing_fit_index", 0);
    auto items = parsed.find("equipped_items");
    if (items != parsed.end() && items->is_object()) {
        for (auto it = items->begin(); it != items->end(); ++it) {
            if (it.value().is_string()) config.equippedItems[it.key()] = it.value().get<std::string>();
        }
    }
    config.success = true;
    return config;
}
} // namespace

AvatarConfig KronosApi::fetchAvatarConfig(const std::string& userId) {
    std::string path = userId.empty() ? "/v1/avatar/me" : "/v1/avatar/" + userId;
    HttpResponse response = requestWithRefresh("GET", path, {});
    if (!response.transportOk) {
        AvatarConfig config;
        config.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return config;
    }
    if (response.status < 200 || response.status >= 300) {
        AvatarConfig config;
        config.error = extractError(response.body, response.status);
        return config;
    }
    return parseAvatarConfig(response.body);
}

AvatarConfig KronosApi::saveAvatarConfig(const AvatarConfig& config) {
    nlohmann::json items = nlohmann::json::object();
    for (const auto& [category, itemId] : config.equippedItems) items[category] = itemId;
    nlohmann::json body{
        {"skin_tone_index", config.skinToneIndex},
        {"head_shape_index", config.headShapeIndex},
        {"body_height", config.bodyHeight},
        {"body_width", config.bodyWidth},
        {"body_limb_scale", config.bodyLimbScale},
        {"body_torso_length", config.bodyTorsoLength},
        {"body_shoulder_width", config.bodyShoulderWidth},
        {"clothing_fit_index", config.clothingFitIndex},
        {"equipped_items", items},
    };

    HttpResponse response = requestWithRefresh("PUT", "/v1/avatar/me", body.dump());
    if (!response.transportOk) {
        AvatarConfig result;
        result.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        AvatarConfig result;
        result.error = extractError(response.body, response.status);
        return result;
    }
    return parseAvatarConfig(response.body);
}

PublishResult KronosApi::publishGame(const PublishRequest& request) {
    PublishResult result;

    nlohmann::json body{{"slug", request.slug}, {"title", request.title}};
    if (!request.description.empty()) body["description"] = request.description;
    if (!request.thumbnailUrl.empty()) body["thumbnail_url"] = request.thumbnailUrl;
    if (!request.sceneSha256.empty()) body["scene_sha256"] = request.sceneSha256;

    HttpResponse response = requestWithRefresh("POST", "/v1/catalog/games/publish", body.dump());
    if (!response.transportOk) {
        result.error = response.error.empty() ? "Could not reach the Kronos service." : response.error;
        return result;
    }
    if (response.status < 200 || response.status >= 300) {
        // The backend's validation messages are already written for a
        // human, so they are surfaced verbatim rather than replaced with
        // a generic "publish failed".
        result.error = extractError(response.body, response.status);
        return result;
    }

    nlohmann::json parsed = nlohmann::json::parse(response.body, nullptr, false);
    if (parsed.is_discarded()) {
        result.error = "The Kronos service returned a response this build could not parse.";
        return result;
    }
    result.status = jsonStringOr(parsed, "status", "published");
    auto game = parsed.find("game");
    if (game != parsed.end() && game->is_object()) {
        result.gameId = jsonStringOr(*game, "id");
        result.slug = jsonStringOr(*game, "slug");
    }
    result.success = true;
    return result;
}

} // namespace engine::core
