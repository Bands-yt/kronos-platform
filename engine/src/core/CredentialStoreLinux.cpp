// Kronos ("Google OAuth Authentication" -- "cache it securely"): real
// Linux Secret Service backend (libsecret). Unconditionally listed in
// src/CMakeLists.txt (same shape as platform/LinuxWindow.cpp) -- the
// entire real implementation is guarded by `#if defined(__linux__)`
// below, so this translation unit compiles to nothing on Windows
// (CredentialStoreWindows.cpp provides the real symbols there instead),
// rather than needing a separate conditional CMake source-file
// selection.
#include "core/CredentialStore.hpp"

#if defined(__linux__)

#include <libsecret/secret.h>

namespace engine::core {

namespace {
// Real, stable schema identifying "this is a Kronos credential" to the
// Secret Service daemon -- `key` (e.g. "google_oauth_refresh_token")
// is the one real attribute distinguishing which credential this is,
// same "one real string key, one real string value" shape
// storeCredential()'s own public signature already promises.
const SecretSchema* kronosSchema() {
    static const SecretSchema schema = {
        "org.kronosplatform.Credential", SECRET_SCHEMA_NONE,
        {
            {"key", SECRET_SCHEMA_ATTRIBUTE_STRING},
            {nullptr, static_cast<SecretSchemaAttributeType>(0)},
        },
    };
    return &schema;
}
} // namespace

bool storeCredential(const std::string& key, const std::string& secret) {
    GError* error = nullptr;
    gboolean ok = secret_password_store_sync(kronosSchema(), SECRET_COLLECTION_DEFAULT, "Kronos Platform credential",
                                              secret.c_str(), nullptr, &error, "key", key.c_str(), nullptr);
    if (error != nullptr) {
        g_error_free(error);
        return false;
    }
    return ok == TRUE;
}

bool loadCredential(const std::string& key, std::string& outSecret) {
    GError* error = nullptr;
    gchar* password = secret_password_lookup_sync(kronosSchema(), nullptr, &error, "key", key.c_str(), nullptr);
    if (error != nullptr) {
        g_error_free(error);
        return false;
    }
    if (password == nullptr) return false; // real, honest "no such credential stored" -- not an error
    outSecret = password;
    secret_password_free(password); // real, zeroes the real secret memory before freeing it
    return true;
}

bool deleteCredential(const std::string& key) {
    GError* error = nullptr;
    gboolean ok = secret_password_clear_sync(kronosSchema(), nullptr, &error, "key", key.c_str(), nullptr);
    if (error != nullptr) {
        g_error_free(error);
        return false;
    }
    return ok == TRUE;
}

} // namespace engine::core

#endif // defined(__linux__)
