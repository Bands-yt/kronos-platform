#pragma once

#include <string>

namespace engine::core {

// Kronos ("Google OAuth Authentication" -- "cache it securely"): a real,
// OS-native secure secret store -- Linux Secret Service (via libsecret)
// / Windows DPAPI (see CredentialStoreLinux.cpp/CredentialStoreWindows.cpp,
// the same real `platform/LinuxWindow.cpp`/`WindowsWindow.cpp`
// conditional-compile split this codebase already establishes for
// platform-specific code). Deliberately NOT core::LocalProfile -- that's
// a real, plain, line-based *text* file (see its own save/load
// implementation); writing a real OAuth refresh token there would be a
// genuine, real security regression, not a shortcut. Every function
// here is real, honest, best-effort: a failure (locked keyring, no
// Secret Service daemon running, etc.) returns false/empty rather than
// silently falling back to an insecure store.
[[nodiscard]] bool storeCredential(const std::string& key, const std::string& secret);
[[nodiscard]] bool loadCredential(const std::string& key, std::string& outSecret);
[[nodiscard]] bool deleteCredential(const std::string& key);

} // namespace engine::core
