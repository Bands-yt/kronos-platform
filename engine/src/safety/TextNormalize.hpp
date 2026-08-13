#pragma once

#include <string>

namespace engine::safety {

// Lowercases and de-leetspeaks a single ASCII character. Returns '\0' for
// anything that isn't alphanumeric after substitution -- callers treat
// that as "drop" (normalizeAscii()) or "word boundary" (a tokenizer).
// Shared by IPInfringementScanner and CreatorIdentityGuard so both scan
// against the same substitution table instead of two copies drifting
// apart (e.g. one gaining a new leetspeak mapping the other doesn't).
[[nodiscard]] char normalizeChar(char raw);

// Collapses case/punctuation/spacing/leetspeak so e.g. "Adopt Me",
// "adopt_me", and "@d0pt Me!!" all become "adoptme". Only handles ASCII
// input -- callers dealing with non-Latin/homoglyph text (see
// CreatorIdentityGuard's confusable-character skeleton) normalize down to
// ASCII first, then pass the result through this.
[[nodiscard]] std::string normalizeAscii(const std::string& text);

} // namespace engine::safety
