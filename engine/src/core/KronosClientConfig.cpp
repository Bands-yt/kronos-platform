#include "core/KronosClientConfig.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/Logger.hpp"

namespace engine::core {

namespace {

constexpr const char* kDefaultApiUrl = "http://localhost:8080";

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
