#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace engine::core {

// Kronos ("Backend service layer" -- C++ client integration): the real
// HTTP client the Launcher and Studio use to talk to the Kronos backend.
//
// Design notes:
//   * Every call here BLOCKS on a real network round trip. None of them
//     may be called from the render thread -- see RuntimeShell's own
//     background-thread + mutex-guarded-result pattern, already used for
//     Google sign-in and the update check, and reused for this.
//   * Tokens are never written to LocalProfile or any plaintext file.
//     The refresh token goes to core::CredentialStore (libsecret on
//     Linux, DPAPI on Windows), which already exists for exactly this.
//     The access token is short-lived and deliberately kept in memory
//     only -- persisting a 15-minute credential buys nothing and widens
//     the window in which it can be stolen off disk.

struct KronosUser {
    std::string id;
    std::string email;
    std::string displayName;
    bool emailVerified = false;
};

struct KronosAuthResult {
    bool success = false;
    KronosUser user;
    std::string error;
};

struct CatalogueGame {
    std::string id;
    std::string slug;
    std::string title;
    std::string description;
    std::string thumbnailUrl;
    std::string creatorName;
    // -1 means "the server could not tell us right now" -- deliberately
    // distinct from a real 0, so the UI can say "unavailable" rather than
    // confidently drawing a wrong number. See the backend's own
    // player_counts_available flag.
    int activePlayers = -1;
};

struct CatalogueResult {
    bool success = false;
    std::vector<CatalogueGame> games;
    std::string nextCursor;
    bool playerCountsAvailable = false;
    std::string error;
};

struct ServerAllocation {
    bool success = false;
    std::string host;
    uint16_t port = 0;
    std::string region;
    // Presented to the game server on connect; it proves the backend
    // really allocated this player to this server.
    std::string joinTicket;
    std::string error;
};

struct KronosFriend {
    std::string id;
    std::string username;
    std::string displayName;
    // "offline" | "online_launcher" | "in_game", straight from the server.
    std::string status;
    std::string currentGameId;
    std::string currentServerId;
    // Present only when the friend is genuinely in a game -- the server
    // mints one for that specific server and nothing else.
    std::string joinTicket;
};

struct FriendsResult {
    bool success = false;
    std::vector<KronosFriend> friends;
    std::vector<KronosFriend> incomingRequests;
    std::vector<KronosFriend> outgoingRequests;
    // False when the server could not read presence. The UI must then say
    // so rather than drawing everybody as offline.
    bool presenceAvailable = false;
    std::string error;
};

struct UserSearchResult {
    std::string id;
    std::string username;
    std::string displayName;
    // "none" | "request_sent" | "request_received" | "friends" | "blocked"
    std::string relationship;
};

// One page of the account directory.
struct DirectoryUser {
    std::string id;
    std::string username;
    std::string displayName;
    // "offline" | "online_launcher" | "in_studio" | "in_game"
    std::string status;
    std::string currentGameId;
};

struct DirectoryResult {
    bool success = false;
    std::vector<DirectoryUser> users;
    std::string nextCursor;   // empty when this was the last page
    bool presenceAvailable = false;
    std::string error;
};

struct PresenceSummary {
    bool success = false;
    bool available = false;
    int offline = 0;
    int onlineLauncher = 0;
    int inStudio = 0;
    int inGame = 0;
    int totalOnline = 0;
    int registeredAccounts = 0;
    std::string error;
};

struct PublishRequest {
    std::string slug;
    std::string title;
    std::string description;
    std::string thumbnailUrl;
    std::string sceneSha256;
};

struct PublishResult {
    bool success = false;
    // "published" for a new place, "updated" when re-publishing your own.
    std::string status;
    std::string gameId;
    std::string slug;
    std::string error;
};

struct UserSearchResponse {
    bool success = false;
    std::vector<UserSearchResult> results;
    std::string error;
};

// Kronos ("via join tickets"): SERVER-side ticket verification against a
// real running Kronos backend. Free function rather than a KronosApi
// method because the process calling it is a dedicated game server with
// no user session of its own -- it is vouching for somebody else's
// ticket, not acting as an account.
//
// Returns true only if the backend positively confirms the ticket AND
// confirms it was issued for `serverKey`. Every other outcome -- an
// expired ticket, a ticket for a different server, an unreachable
// backend -- returns false, because a server that cannot verify must
// not admit the player.
[[nodiscard]] bool verifyJoinTicketWithKronos(const std::string& baseUrl, const std::string& serverKey,
                                               const std::string& ticket, uint64_t& outUserId);

class KronosApi {
public:
    explicit KronosApi(std::string baseUrl);

    void setBaseUrl(std::string baseUrl);
    [[nodiscard]] const std::string& baseUrl() const { return baseUrl_; }

    // --- authentication ---------------------------------------------------
    [[nodiscard]] KronosAuthResult signUp(const std::string& email, const std::string& password,
                                           const std::string& displayName);
    [[nodiscard]] KronosAuthResult logIn(const std::string& email, const std::string& password);

    // Exchanges a Google ID token for a real Kronos session. The backend
    // verifies the token's signature against Google's JWKS -- the client
    // deliberately does NOT decide who the user is.
    [[nodiscard]] KronosAuthResult logInWithGoogle(const std::string& googleIdToken);

    // Uses the stored refresh token to obtain a fresh access token.
    // Called at launch to restore a previous session without another
    // password prompt, and whenever a request comes back 401.
    [[nodiscard]] KronosAuthResult restoreSession();

    // Kronos Client spec ("no user or password text fields ever appear in
    // the launcher"): completes a sign-in that happened entirely in the
    // system browser. The browser hands back a refresh token over the
    // loopback callback; this persists it and exchanges it for a real
    // session through the SAME /v1/auth/refresh endpoint an ordinary
    // resume uses -- so the browser flow needs no new backend surface,
    // and the launcher never sees a password at any point.
    [[nodiscard]] KronosAuthResult completeBrowserSignIn(const std::string& refreshToken);

    // Revokes the refresh token server-side and clears local state. Best
    // effort on the network call: local credentials are cleared either
    // way, so "log out" always visibly logs you out.
    void logOut();

    [[nodiscard]] bool isSignedIn() const;
    [[nodiscard]] std::optional<KronosUser> currentUser() const;

    // --- catalogue --------------------------------------------------------
    [[nodiscard]] CatalogueResult fetchGames(int limit = 24, const std::string& cursor = {},
                                              const std::string& search = {});
    [[nodiscard]] ServerAllocation allocateServer(const std::string& gameSlug);

    // --- social ----------------------------------------------------------
    [[nodiscard]] FriendsResult fetchFriends();
    [[nodiscard]] UserSearchResponse searchUsers(const std::string& queryText);
    [[nodiscard]] bool sendFriendRequest(const std::string& userId, std::string& outError);
    [[nodiscard]] bool respondToFriendRequest(const std::string& userId, bool accept, std::string& outError);
    // Real 15s presence heartbeat, per the spec. Best effort: a failed
    // heartbeat just means friends see this user go offline shortly.
    void sendPresenceHeartbeat(const std::string& status, const std::string& gameId, const std::string& serverId);

    // --- directory / discovery -------------------------------------------
    // `cursor` empty fetches the first page; pass the previous response's
    // nextCursor to continue. limit is clamped server-side to 200.
    [[nodiscard]] DirectoryResult fetchUserDirectory(int limit = 50, const std::string& cursor = {});
    [[nodiscard]] PresenceSummary fetchPresenceSummary();

    // --- publishing --------------------------------------------------------
    [[nodiscard]] PublishResult publishGame(const PublishRequest& request);

private:
    struct HttpResponse {
        bool transportOk = false;
        long status = 0;
        std::string body;
        std::string error;
    };

    [[nodiscard]] HttpResponse request(const char* method, const std::string& path, const std::string& jsonBody,
                                        bool withAuth);
    // Performs `fn`, and if it comes back 401, refreshes the session once
    // and retries. One retry only: a refresh that does not fix a 401
    // means the session is genuinely gone, and retrying further would
    // just spin.
    [[nodiscard]] HttpResponse requestWithRefresh(const char* method, const std::string& path,
                                                   const std::string& jsonBody);

    [[nodiscard]] KronosAuthResult adoptSession(const HttpResponse& response);
    // One refresh-exchange implementation, shared by the browser hand-off
    // and the saved-session resume.
    [[nodiscard]] KronosAuthResult exchangeRefreshToken(const std::string& refreshToken);
    void persistRefreshToken(const std::string& token);
    [[nodiscard]] std::string loadPersistedRefreshToken() const;
    void clearPersistedRefreshToken();

    std::string baseUrl_;

    // Guards everything below -- a background worker thread mutates these
    // while the UI thread reads isSignedIn()/currentUser() each frame.
    mutable std::mutex mutex_;
    std::string accessToken_; // in-memory only, deliberately never persisted
    std::optional<KronosUser> user_;
};

} // namespace engine::core
