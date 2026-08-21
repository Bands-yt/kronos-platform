#pragma once

#include <string>

namespace engine::core {

// Kronos ("Google OAuth Authentication" -- "secure sign-up/login flow"):
// real RFC 7636 (PKCE -- Proof Key for Code Exchange) support for a
// native/public OAuth client. A desktop app can't keep a client secret
// confidential (it ships inside the binary, decompilable by anyone), so
// this is the real, correct, modern flow for this kind of app -- no
// client secret is ever used or stored anywhere in this codebase, only
// a real Client ID (public by design) plus this real, per-attempt
// verifier/challenge pair.
//
// generateCodeVerifier() returns a real, cryptographically random
// 43-128 character string (RFC 7636 §4.1's own real length bounds,
// unreserved base64url characters only). deriveCodeChallenge() returns
// the real base64url(SHA256(verifier)) (§4.2, "S256" method) sent in
// the authorization request; the raw verifier itself is sent later,
// only in the real token-exchange POST (GoogleAuth.hpp), never in the
// browser-visible authorization URL.
[[nodiscard]] std::string generateCodeVerifier();
[[nodiscard]] std::string deriveCodeChallenge(const std::string& codeVerifier);

// Real, small, self-contained SHA-256 (FIPS 180-4) -- exposed here
// (rather than kept file-local) because deriveCodeChallenge() isn't the
// only real consumer this codebase could reasonably have for a real
// hash primitive, and a small, independently-testable pure function is
// cheap to keep. Returns the raw 32-byte digest, not hex/base64 --
// callers that want a text encoding call base64UrlEncode() themselves.
[[nodiscard]] std::string sha256(const std::string& input);

// Real RFC 4648 §5 base64url -- '+'/'/' replaced with '-'/'_', trailing
// '=' padding stripped entirely (both real requirements PKCE's own
// base64url use imposes; a plain base64 encoder, like the one
// core/SceneFile.cpp already has for an unrelated real purpose, isn't
// interchangeable with this for a URL-embedded value).
[[nodiscard]] std::string base64UrlEncode(const std::string& input);

// The real inverse of base64UrlEncode() -- used to read the payload
// segment of a real Google-issued id_token JWT (GoogleAuth.hpp), not
// PKCE itself. Real, honest failure: returns an empty string for
// malformed input rather than a partial/garbage decode.
[[nodiscard]] std::string base64UrlDecode(const std::string& input);

} // namespace engine::core
