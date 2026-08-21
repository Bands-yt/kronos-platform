#pragma once

#include <string>

namespace kronos_installer {

// Kronos ("Bootstrap Installer" -- "verify the integrity of the
// files"): the exact same real, verified-correct SHA-256 algorithm
// engine/src/core/OAuthPkce.cpp already has (cross-checked there
// against this machine's own `sha256sum` for both the empty-string and
// "abc" standard test vectors) -- intentionally duplicated, not shared
// via a dependency on engine_core, since this installer must stay
// fully independent of the engine build (see installer/CMakeLists.txt's
// own header comment on why). Returns a real lowercase hex digest
// string directly (this project's one real caller wants to compare it
// against a real, downloaded `.sha256` file's own hex text, not a raw
// byte string), unlike OAuthPkce's own sha256() which returns raw
// bytes for its own different real caller (PKCE's base64url encoding).
[[nodiscard]] std::string sha256Hex(const std::string& input);

// Real, streaming-friendly variant -- a large real downloaded archive
// (hundreds of MB) shouldn't need to be held twice in memory (once as
// the file on disk, once as an in-memory std::string just to hash it).
// Returns an empty string on a real file-open failure.
[[nodiscard]] std::string sha256HexOfFile(const std::string& path);

} // namespace kronos_installer
