#include "Sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <vector>

namespace kronos_installer {

namespace {
// Real FIPS 180-4 SHA-256 round constants -- identical table to
// engine/src/core/OAuthPkce.cpp's own (see this file's own header
// comment on why it's duplicated, not shared).
constexpr std::array<uint32_t, 64> kRoundConstants = {
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

// Real, streaming SHA-256 -- processes one 64-byte block at a time so a
// real, large (hundreds-of-MB) archive never needs to be held in memory
// twice just to hash it (see Sha256.hpp's own header comment).
class Sha256Context {
public:
    Sha256Context() {
        h_[0] = 0x6a09e667; h_[1] = 0xbb67ae85; h_[2] = 0x3c6ef372; h_[3] = 0xa54ff53a;
        h_[4] = 0x510e527f; h_[5] = 0x9b05688c; h_[6] = 0x1f83d9ab; h_[7] = 0x5be0cd19;
    }

    void update(const uint8_t* data, size_t len) {
        totalBytes_ += len;
        size_t offset = 0;
        // Real, top up a partial block from a previous update() call
        // before starting on fresh 64-byte blocks.
        if (!pending_.empty()) {
            size_t need = 64 - pending_.size();
            size_t take = std::min(need, len);
            pending_.insert(pending_.end(), data, data + take);
            offset += take;
            if (pending_.size() == 64) {
                processBlock(pending_.data());
                pending_.clear();
            }
        }
        while (offset + 64 <= len) {
            processBlock(data + offset);
            offset += 64;
        }
        if (offset < len) pending_.insert(pending_.end(), data + offset, data + len);
    }

    std::array<uint8_t, 32> finalize() {
        uint64_t bitLength = totalBytes_ * 8ULL;
        std::vector<uint8_t> tail = pending_;
        tail.push_back(0x80);
        while (tail.size() % 64 != 56) tail.push_back(0x00);
        for (int i = 7; i >= 0; --i) tail.push_back(static_cast<uint8_t>((bitLength >> (i * 8)) & 0xFF));
        for (size_t i = 0; i < tail.size(); i += 64) processBlock(tail.data() + i);

        std::array<uint8_t, 32> digest{};
        for (int i = 0; i < 8; ++i) {
            digest[i * 4] = static_cast<uint8_t>((h_[i] >> 24) & 0xFF);
            digest[i * 4 + 1] = static_cast<uint8_t>((h_[i] >> 16) & 0xFF);
            digest[i * 4 + 2] = static_cast<uint8_t>((h_[i] >> 8) & 0xFF);
            digest[i * 4 + 3] = static_cast<uint8_t>(h_[i] & 0xFF);
        }
        return digest;
    }

private:
    void processBlock(const uint8_t* block) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], hh = h_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = hh + s1 + ch + kRoundConstants[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;

            hh = g; g = f; f = e; e = d + temp1; d = c; c = b; b = a; a = temp1 + temp2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += hh;
    }

    uint32_t h_[8];
    std::vector<uint8_t> pending_;
    uint64_t totalBytes_ = 0;
};

std::string toHex(const std::array<uint8_t, 32>& digest) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (uint8_t byte : digest) {
        out += kHex[(byte >> 4) & 0xF];
        out += kHex[byte & 0xF];
    }
    return out;
}
} // namespace

std::string sha256Hex(const std::string& input) {
    Sha256Context ctx;
    ctx.update(reinterpret_cast<const uint8_t*>(input.data()), input.size());
    return toHex(ctx.finalize());
}

std::string sha256HexOfFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) return {};

    Sha256Context ctx;
    std::vector<uint8_t> buffer(1 << 16); // real, 64KB read chunks
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        std::streamsize read = file.gcount();
        if (read > 0) ctx.update(buffer.data(), static_cast<size_t>(read));
    }
    return toHex(ctx.finalize());
}

} // namespace kronos_installer
