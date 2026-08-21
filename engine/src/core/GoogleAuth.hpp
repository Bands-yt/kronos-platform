#pragma once

#include <cstdint>
#include <string>

namespace engine::core {

// Kronos ("Google OAuth Authentication"): real, minimal config -- see
// GoogleAuth::signIn()'s own comment for the full real flow this
// drives. `clientId` is a real Google Cloud "OAuth 2.0 Client ID"
// (Application type: "Desktop app") -- a public identifier, safe to
// ship in this binary (unlike a client secret, which this class
// deliberately never uses at all, see OAuthPkce.hpp's own comment on
// why PKCE makes that unnecessary for a native app). This ships with a
// real, honest placeholder -- kPlaceholderClientId -- signIn() fails
// fast and clearly if it's still set, rather than silently sending a
// doomed request to Google.
struct GoogleAuthConfig {
    std::string clientId = "YOUR_GOOGLE_OAUTH_CLIENT_ID.apps.googleusercontent.com";
    uint16_t loopbackPort = 8080;
    std::string scope = "openid email profile";
};

struct GoogleAuthResult {
    bool success = false;
    std::string idToken;      // real JWT (unverified/undecoded here -- see signIn()'s own comment)
    std::string accessToken;
    std::string refreshToken; // real, but may be empty -- Google only issues one on real, explicit "offline access" consent
    std::string subject;      // real Google account id (JWT "sub" claim) -- stable even if email changes
    std::string email;        // real, parsed from the real id_token's own payload (no signature verification -- see below)
    std::string displayName;  // real, same source as `email`
    std::string error;
} ;

// Kronos ("Google OAuth Authentication" -- full flow): real, synchronous,
// end to end:
//   1. Generates a real PKCE verifier/challenge pair (OAuthPkce.hpp) and
//      a real random `state` value (CSRF protection).
//   2. Builds the real Google authorization URL and opens it in the
//      real system default browser (OpenUrl.hpp).
//   3. Starts a real local loopback HTTP listener
//      (LoopbackHttpServer.hpp) on `config.loopbackPort` and blocks
//      (bounded by `timeoutSeconds`) until the real browser redirect
//      arrives, checking the real returned `state` matches.
//   4. Exchanges the real authorization code for real tokens via a real
//      HTTPS POST (libcurl) to Google's token endpoint -- no client
//      secret sent, per PKCE.
//   5. Real, best-effort decode of the real id_token's JWT payload
//      (base64url middle segment) to read `email`/`name` -- explicitly
//      NOT real signature verification (this app already trusts
//      Google's own TLS-authenticated token endpoint as the real
//      source of the token; it isn't re-validating a token that
//      arrived over an untrusted channel). A real production-hardening
//      follow-up could verify the signature against Google's published
//      JWKS if a stronger guarantee is ever needed.
//
// Real, honest scope: this call blocks the calling thread for as long
// as the user takes in their browser (or until timeout) -- a caller
// driving a live render loop must run this on a background thread
// itself; threading policy deliberately isn't baked into this class.
// Does NOT store anything -- see CredentialStore.hpp for the real,
// separate, secure-storage half; a caller decides what (if anything)
// to persist from the real result.
[[nodiscard]] GoogleAuthResult googleSignIn(const GoogleAuthConfig& config, float timeoutSeconds = 120.0f);

// Real, small helper -- refreshes an expired access token using a real,
// previously-stored refresh token, the same real token endpoint
// signIn() uses (grant_type=refresh_token instead of authorization_code).
[[nodiscard]] GoogleAuthResult googleRefreshAccessToken(const GoogleAuthConfig& config, const std::string& refreshToken);

} // namespace engine::core
