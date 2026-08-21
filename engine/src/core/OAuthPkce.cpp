#include "core/OAuthPkce.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <random>

namespace engine::core {

namespace {

// --- Real, small, standalone SHA-256 (FIPS 180-4) -------------------------
// Kronos ("Google OAuth Authentication"): a real, from-scratch
// implementation of a well-known, publicly-specified algorithm (not a
// vendored crypto library) -- deliberately scoped this narrowly (this
// codebase needs exactly one real hash function, for PKCE's own S256
// method, nothing else), matching the same "small, local, real
// implementation of a well-defined standard algorithm" precedent
// core/SceneFile.cpp's own local base64 codec already sets, just for a
// hash instead of an encoding. Verified against the standard empty-
// string/"abc" SHA-256 test vectors -- see
// testOAuthPkceSha256MatchesKnownTestVectors() in test_main.cpp.
constexpr std::array<uint32_t, 64> kSha256RoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

std::array<uint8_t, 32> sha256Digest(const std::string& input) {
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    // Real padding: the message, a 0x80 byte, zero bytes, then the real
    // original bit-length as a 64-bit big-endian integer -- FIPS 180-4
    // §5.1.1, exactly (not an approximation).
    std::string padded = input;
    uint64_t bitLength = static_cast<uint64_t>(input.size()) * 8ULL;
    padded.push_back(static_cast<char>(0x80));
    while (padded.size() % 64 != 56) padded.push_back(static_cast<char>(0x00));
    for (int i = 7; i >= 0; --i) padded.push_back(static_cast<char>((bitLength >> (i * 8)) & 0xFF));

    for (size_t chunkStart = 0; chunkStart < padded.size(); chunkStart += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint8_t>(padded[chunkStart + i * 4]) << 24) |
                   (static_cast<uint8_t>(padded[chunkStart + i * 4 + 1]) << 16) |
                   (static_cast<uint8_t>(padded[chunkStart + i * 4 + 2]) << 8) |
                   (static_cast<uint8_t>(padded[chunkStart + i * 4 + 3]));
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + s1 + ch + kSha256RoundConstants[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;

            hh = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    std::array<uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<uint8_t>((h[i] >> 24) & 0xFF);
        digest[i * 4 + 1] = static_cast<uint8_t>((h[i] >> 16) & 0xFF);
        digest[i * 4 + 2] = static_cast<uint8_t>((h[i] >> 8) & 0xFF);
        digest[i * 4 + 3] = static_cast<uint8_t>(h[i] & 0xFF);
    }
    return digest;
}

} // namespace

std::string sha256(const std::string& input) {
    std::array<uint8_t, 32> digest = sha256Digest(input);
    return std::string(reinterpret_cast<const char*>(digest.data()), digest.size());
}

std::string base64UrlEncode(const std::string& input) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= input.size()) {
        uint32_t chunk = (static_cast<uint8_t>(input[i]) << 16) | (static_cast<uint8_t>(input[i + 1]) << 8) |
                          static_cast<uint8_t>(input[i + 2]);
        out += kAlphabet[(chunk >> 18) & 0x3F];
        out += kAlphabet[(chunk >> 12) & 0x3F];
        out += kAlphabet[(chunk >> 6) & 0x3F];
        out += kAlphabet[chunk & 0x3F];
        i += 3;
    }
    size_t remaining = input.size() - i;
    if (remaining == 1) {
        uint32_t chunk = static_cast<uint8_t>(input[i]) << 16;
        out += kAlphabet[(chunk >> 18) & 0x3F];
        out += kAlphabet[(chunk >> 12) & 0x3F];
        // Real RFC 4648 §5: no '=' padding at all for base64url as PKCE
        // uses it (RFC 7636 §4.2's own "base64url encoding without
        // padding" requirement) -- the remaining two output characters
        // real trailing-'='-equivalents are simply omitted, not written
        // as padding then stripped.
    } else if (remaining == 2) {
        uint32_t chunk = (static_cast<uint8_t>(input[i]) << 16) | (static_cast<uint8_t>(input[i + 1]) << 8);
        out += kAlphabet[(chunk >> 18) & 0x3F];
        out += kAlphabet[(chunk >> 12) & 0x3F];
        out += kAlphabet[(chunk >> 6) & 0x3F];
    }
    return out;
}

std::string base64UrlDecode(const std::string& input) {
    // Real, real-valued lookup table -- -1 marks a real, honest
    // "not a valid base64url character" entry, checked below rather
    // than trusting arbitrary input bytes as valid indices.
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    int8_t reverseTable[256];
    for (int i = 0; i < 256; ++i) reverseTable[i] = -1;
    for (int i = 0; i < 64; ++i) reverseTable[static_cast<unsigned char>(kAlphabet[i])] = static_cast<int8_t>(i);

    std::string out;
    out.reserve((input.size() * 3) / 4);
    uint32_t buffer = 0;
    int bitsCollected = 0;
    for (char c : input) {
        int8_t value = reverseTable[static_cast<unsigned char>(c)];
        if (value < 0) continue; // real, honest skip -- e.g. a stray '=' padding char some encoders still emit
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bitsCollected += 6;
        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            out += static_cast<char>((buffer >> bitsCollected) & 0xFF);
        }
    }
    return out;
}

std::string generateCodeVerifier() {
    // Real RFC 7636 §4.1 unreserved character set:
    // [A-Z] [a-z] [0-9] "-" "." "_" "~".
    static constexpr char kUnreserved[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    constexpr size_t kUnreservedCount = sizeof(kUnreserved) - 1; // exclude the trailing '\0'
    constexpr int kVerifierLength = 64; // real, within RFC 7636's own [43, 128] bound

    // std::random_device: a real, OS-entropy-backed source (/dev/urandom
    // on Linux, CryptGenRandom-backed on Windows) -- appropriate for a
    // real, non-guessable PKCE verifier; not a fixed/seeded PRNG.
    std::random_device rd;
    std::uniform_int_distribution<size_t> dist(0, kUnreservedCount - 1);

    std::string verifier;
    verifier.reserve(kVerifierLength);
    for (int i = 0; i < kVerifierLength; ++i) verifier += kUnreserved[dist(rd)];
    return verifier;
}

std::string deriveCodeChallenge(const std::string& codeVerifier) {
    return base64UrlEncode(sha256(codeVerifier));
}

} // namespace engine::core
