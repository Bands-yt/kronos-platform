#include "core/UpdateCheck.hpp"

#include <cctype>
#include <cstdio>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace engine::core {

namespace {

size_t curlWriteCallback(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}

// Real, small numeric-field reader: consumes digits from `text` starting
// at `pos`, returns false if there wasn't at least one real digit there
// (which is what makes "v", "banana" and "1..2" honestly unparseable
// rather than silently zero).
bool readNumber(const std::string& text, size_t& pos, int& out) {
    size_t start = pos;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos])) != 0) ++pos;
    if (pos == start) return false;
    out = std::stoi(text.substr(start, pos - start));
    return true;
}

} // namespace

SemanticVersion parseVersion(const std::string& text) {
    SemanticVersion version;

    size_t pos = 0;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    if (pos < text.size() && (text[pos] == 'v' || text[pos] == 'V')) ++pos;

    if (!readNumber(text, pos, version.major)) return version;

    // Real, deliberate leniency: "1" and "1.2" are accepted as 1.0.0 and
    // 1.2.0. A real tag in this repo always carries all three, but a
    // shortened one is unambiguous rather than malformed, and treating
    // it as unparseable would be the stricter-but-less-useful call.
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        if (!readNumber(text, pos, version.minor)) return version;
        if (pos < text.size() && text[pos] == '.') {
            ++pos;
            if (!readNumber(text, pos, version.patch)) return version;
        }
    }

    if (pos < text.size() && text[pos] == '-') {
        size_t prereleaseStart = pos + 1;
        // SemVer build metadata ('+...') carries no precedence, so it is
        // dropped here rather than folded into the prerelease tag, where
        // it would wrongly affect comparison.
        size_t buildMetadata = text.find('+', prereleaseStart);
        version.prerelease = buildMetadata == std::string::npos
                                 ? text.substr(prereleaseStart)
                                 : text.substr(prereleaseStart, buildMetadata - prereleaseStart);
    }

    version.valid = true;
    return version;
}

int compareVersions(const SemanticVersion& a, const SemanticVersion& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;

    // SemVer §11.3: a real prerelease has LOWER precedence than the
    // otherwise-identical final release.
    bool aPre = !a.prerelease.empty();
    bool bPre = !b.prerelease.empty();
    if (aPre != bPre) return aPre ? -1 : 1;
    if (!aPre) return 0;

    // Both are prereleases. Real, honest simplification: a plain
    // lexicographic compare, not SemVer's own full dot-separated
    // identifier rules (which compare numeric identifiers numerically).
    // Every real tag this project has ever cut uses a single alphabetic
    // tag ("alpha", "beta"), where the two rules agree; a real
    // "alpha.10" vs "alpha.9" would differ, and is called out here
    // rather than silently mis-ordered.
    if (a.prerelease == b.prerelease) return 0;
    return a.prerelease < b.prerelease ? -1 : 1;
}

UpdateCheckResult checkForUpdate(const std::string& currentVersion, const std::string& repoOwner,
                                  const std::string& repoName) {
    UpdateCheckResult result;

    SemanticVersion current = parseVersion(currentVersion);
    if (!current.valid) {
        result.error = "could not parse the running build's own version string (\"" + currentVersion + "\")";
        return result;
    }

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error = "curl_easy_init() failed";
        return result;
    }

    std::string url = "https://api.github.com/repos/" + repoOwner + "/" + repoName + "/releases?per_page=20";
    std::string responseBody;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    // GitHub's API rejects requests with no User-Agent outright.
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "kronos-launcher-update-check");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    // Real Windows-CA-bundle robustness -- see KronosApi.cpp's
    // verifyJoinTicketWithKronos() for the full explanation. GitHub's
    // API is a different host than the Kronos backend, but the same
    // vcpkg-curl-on-Windows gap applies equally to every outbound HTTPS
    // call this client makes, not just the ones talking to Kronos.
    curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
    // A real startup check must never hang the launcher's own update
    // prompt indefinitely on a real half-open connection.
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    CURLcode code = curl_easy_perform(curl);
    long httpStatus = 0;
    if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        result.error = std::string("update check failed: ") + curl_easy_strerror(code);
        return result;
    }
    if (httpStatus != 200) {
        result.error = "update check failed: HTTP " + std::to_string(httpStatus);
        return result;
    }

    nlohmann::json releases = nlohmann::json::parse(responseBody, nullptr, false);
    if (releases.is_discarded() || !releases.is_array()) {
        result.error = "update check failed: GitHub returned a response this build couldn't parse";
        return result;
    }

    SemanticVersion best;
    for (const auto& release : releases) {
        if (release.value("draft", false)) continue;
        std::string tag = release.value("tag_name", std::string());
        SemanticVersion candidate = parseVersion(tag);
        if (!candidate.valid) continue;
        if (best.valid && compareVersions(candidate, best) <= 0) continue;
        best = candidate;
        result.latestTag = tag;
        result.releaseUrl = release.value("html_url", std::string());
    }

    if (!best.valid) {
        result.error = "update check failed: no published release carried a parseable version tag";
        return result;
    }

    result.checked = true;
    result.updateAvailable = compareVersions(best, current) > 0;
    return result;
}

} // namespace engine::core
