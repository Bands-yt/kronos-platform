#include "core/KronosClientConfig.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/Logger.hpp"

namespace engine::core {

namespace {

// Kronos production backend -- the real domain, not the bare IP this
// used to be. Caddy's automatic HTTPS is live for kronosplatform.com
// (verified: a real TLS 1.3 handshake, a real Let's Encrypt leaf
// certificate with CN=kronosplatform.com, and it now 308-redirects
// plain HTTP to HTTPS on this same host).
//
// That redirect is exactly why the bare IP could no longer be the
// default here. Caddy has no certificate to present for a connection
// with no matching SNI/Host -- there is only ever one cert, issued for
// the domain -- so a TLS ClientHello sent to the IP directly (or
// following the redirect Caddy issues, which points right back at the
// same IP) gets the handshake aborted with a real
// "tlsv1 alert internal error", which libcurl reports up as
// CURLE_SSL_CONNECT_ERROR -- curl_easy_strerror() for that code is the
// literal string "SSL connect error". Confirmed directly: `curl -v
// https://159.65.17.24/healthz` reproduces that exact failure;
// `curl https://kronosplatform.com/healthz` succeeds cleanly. The fix
// is the hostname, not anything about TLS verification -- CURLOPT_
// SSL_VERIFYPEER/VERIFYHOST in KronosApi.cpp were never the problem and
// stay exactly as strict as they already were.
constexpr const char* kDefaultApiUrl = "https://kronosplatform.com";

std::string fromEnvironment(const char* name) {
    const char* value = std::getenv(name);
    return (value != nullptr && value[0] != '\0') ? std::string(value) : std::string();
}

// Strips one trailing slash so callers can always concatenate "/v1/..."
// without producing a double slash.
std::string normalizeBaseUrl(std::string url) {
    while (!url.empty() && url.back() == '/') url.pop_back();
    return url;
}

} // namespace

KronosClientConfig loadKronosClientConfig(const std::string& executableDir) {
    KronosClientConfig config;

    std::string fileApi;
    std::string fileAuth;
    const std::string path = executableDir + "/config.json";

    std::ifstream in(path);
    if (in) {
        std::ostringstream contents;
        contents << in.rdbuf();
        // Non-throwing parse: a malformed config must not take the client
        // down, it must fall through to the next source.
        nlohmann::json parsed = nlohmann::json::parse(contents.str(), nullptr, false);
        if (parsed.is_discarded()) {
            logWarn("Kronos", "config.json at \"%s\" is not valid JSON -- ignoring it.", path.c_str());
        } else {
            auto readString = [&parsed](const char* key) -> std::string {
                auto it = parsed.find(key);
                return (it != parsed.end() && it->is_string()) ? it->get<std::string>() : std::string();
            };
            fileApi = readString("api_url");
            fileAuth = readString("auth_url");
        }
    }

    std::string envApi = fromEnvironment("KRONOS_API_URL");
    std::string envAuth = fromEnvironment("KRONOS_AUTH_URL");

    if (!fileApi.empty()) {
        config.apiUrl = fileApi;
        config.source = "config.json";
    } else if (!envApi.empty()) {
        config.apiUrl = envApi;
        config.source = "KRONOS_API_URL";
    } else {
        config.apiUrl = kDefaultApiUrl;
        config.source = "built-in default";
    }
    config.apiUrl = normalizeBaseUrl(config.apiUrl);

    // The auth page defaults to living on the API host, so a deployment
    // only has to set one value unless it genuinely splits them.
    if (!fileAuth.empty()) {
        config.authUrl = fileAuth;
    } else if (!envAuth.empty()) {
        config.authUrl = envAuth;
    } else {
        config.authUrl = config.apiUrl + "/auth/start";
    }
    config.authUrl = normalizeBaseUrl(config.authUrl);

    return config;
}

} // namespace engine::core
