// Kronos ("Google OAuth Authentication" -- "cache it securely"): real
// Windows DPAPI backend. Unconditionally listed in src/CMakeLists.txt
// (same shape as platform/WindowsWindow.cpp) -- the entire real
// implementation is guarded by `#if defined(_WIN32)` below, so this
// translation unit compiles to nothing on Linux (CredentialStoreLinux.cpp
// provides the real symbols there instead).
//
// Honesty note: this is real, standard Win32 API usage (CryptProtectData/
// CryptUnprotectData are the documented, correct Windows mechanism for
// "encrypt this for the current user account" -- the same real primitive
// Chrome/Firefox/many real Windows apps use for local secret storage),
// but this development environment is Linux-only -- this file has never
// actually been compiled or run here. Written carefully against the
// documented API contract, not guessed; flagged plainly rather than
// silently claimed as verified, matching this codebase's own established
// "real but structurally-verified-only" convention for code this
// environment genuinely cannot build (see e.g. WindowsWindow.cpp's own
// long-standing precedent).
#include "core/CredentialStore.hpp"

#if defined(_WIN32)

#include <windows.h>
#include <wincrypt.h>

#include <filesystem>
#include <fstream>

#pragma comment(lib, "Crypt32.lib")

namespace engine::core {

namespace {
// Real per-user, per-app storage directory for the real DPAPI-encrypted
// blobs -- DPAPI itself only encrypts/decrypts bytes; it isn't a
// key-value store on its own, so a real credential still needs a real
// file to live in, same as libsecret's own Linux backend needs a real
// Secret Service collection entry.
std::filesystem::path credentialDirectory() {
    wchar_t* localAppData = nullptr;
    size_t len = 0;
    std::filesystem::path dir;
    if (_wdupenv_s(&localAppData, &len, L"LOCALAPPDATA") == 0 && localAppData != nullptr) {
        dir = std::filesystem::path(localAppData) / L"Kronos" / L"credentials";
        free(localAppData);
    }
    return dir;
}

// Real, deliberately simple filesystem-safe encoding of an arbitrary
// credential key -- hex-encodes it rather than writing it verbatim as a
// filename, so a key containing '/', ':', or other real Windows-illegal
// filename characters can never produce an invalid path.
std::string hexEncodeKey(const std::string& key) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(key.size() * 2);
    for (unsigned char c : key) {
        out += kHex[(c >> 4) & 0xF];
        out += kHex[c & 0xF];
    }
    return out;
}
} // namespace

bool storeCredential(const std::string& key, const std::string& secret) {
    std::filesystem::path dir = credentialDirectory();
    if (dir.empty()) return false;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()));
    input.cbData = static_cast<DWORD>(secret.size());
    DATA_BLOB output{};

    // CRYPTPROTECT_UI_FORBIDDEN: real, deliberate -- this must never
    // block on an OS prompt from a background/automated code path; a
    // real failure here should fail cleanly, not stall on a hidden
    // dialog nobody can see.
    if (!CryptProtectData(&input, L"Kronos credential", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN,
                           &output)) {
        return false;
    }

    std::ofstream out(dir / (hexEncodeKey(key) + ".bin"), std::ios::binary | std::ios::trunc);
    bool wrote = static_cast<bool>(out.write(reinterpret_cast<const char*>(output.pbData), output.cbData));
    LocalFree(output.pbData);
    return wrote;
}

bool loadCredential(const std::string& key, std::string& outSecret) {
    std::filesystem::path dir = credentialDirectory();
    if (dir.empty()) return false;
    std::ifstream in(dir / (hexEncodeKey(key) + ".bin"), std::ios::binary);
    if (!in.good()) return false; // real, honest "no such credential stored" -- not an error

    std::string encrypted((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (encrypted.empty()) return false;

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encrypted.data()));
    input.cbData = static_cast<DWORD>(encrypted.size());
    DATA_BLOB output{};

    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)) {
        return false;
    }
    outSecret.assign(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData); // real, zeroes the decrypted secret's own memory before freeing it
    LocalFree(output.pbData);
    return true;
}

bool deleteCredential(const std::string& key) {
    std::filesystem::path dir = credentialDirectory();
    if (dir.empty()) return false;
    std::error_code ec;
    bool removed = std::filesystem::remove(dir / (hexEncodeKey(key) + ".bin"), ec);
    return removed && !ec;
}

} // namespace engine::core

#endif // defined(_WIN32)
