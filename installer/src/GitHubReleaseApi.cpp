#include "GitHubReleaseApi.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdlib>

namespace kronos_installer {

namespace {
size_t writeCallback(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}
struct TagVersion {
    std::array<long, 3> numbers{};
    std::string prerelease;
    bool valid = false;
};

TagVersion parseTag(const std::string& tag) {
    TagVersion version;
    const char* cursor = tag.c_str();
    if (*cursor == 'v' || *cursor == 'V') ++cursor;
    for (size_t i = 0; i < version.numbers.size(); ++i) {
        char* end = nullptr;
        version.numbers[i] = std::strtol(cursor, &end, 10);
        if (end == cursor) return version;
        cursor = end;
        if (i + 1 < version.numbers.size()) {
            if (*cursor != '.') return version;
            ++cursor;
        }
    }
    if (*cursor == '-') version.prerelease = std::string(cursor + 1);
    version.valid = true;
    return version;
}

bool isNewer(const TagVersion& a, const TagVersion& b) {
    if (a.numbers != b.numbers) return a.numbers > b.numbers;
    if (a.prerelease.empty() != b.prerelease.empty()) return a.prerelease.empty();
    return a.prerelease > b.prerelease;
}

} // namespace

LatestRelease fetchLatestRelease(const std::string& owner, const std::string& repo,
                                 const std::string& requiredAssetSuffix) {
    LatestRelease result;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error = "curl_easy_init() failed";
        return result;
    }

    std::string url = "https://api.github.com/repos/" + owner + "/" + repo + "/releases?per_page=30";
    std::string responseBody;

    struct curl_slist* headers = nullptr;
    // Real, both required by GitHub's own real API contract -- a bare
    // request with neither header is real-rejected (403) or served an
    // unexpected media type, not a hypothetical concern.
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");
    headers = curl_slist_append(headers, "User-Agent: kronos-bootstrap-installer");
    headers = curl_slist_append(headers, "X-GitHub-Api-Version: 2022-11-28");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    CURLcode code = curl_easy_perform(curl);
    long httpStatus = 0;
    if (code == CURLE_OK) curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        result.error = std::string("network error: ") + curl_easy_strerror(code);
        return result;
    }
    if (httpStatus != 200) {
        result.error = "GitHub API returned HTTP " + std::to_string(httpStatus) +
                        " -- is there a real published release yet?";
        return result;
    }

    try {
        nlohmann::json releases = nlohmann::json::parse(responseBody);
        if (!releases.is_array()) {
            result.error = "GitHub returned an unexpected response";
            return result;
        }
        TagVersion best;
        for (const auto& releaseJson : releases) {
            if (releaseJson.value("draft", false)) continue;
            LatestRelease candidate;
            candidate.tagName = releaseJson.value("tag_name", std::string());
            TagVersion version = parseTag(candidate.tagName);
            if (!version.valid || (best.valid && !isNewer(version, best))) continue;
            if (releaseJson.contains("assets") && releaseJson["assets"].is_array()) {
                for (const auto& assetJson : releaseJson["assets"]) {
                    ReleaseAsset asset;
                    asset.name = assetJson.value("name", std::string());
                    asset.downloadUrl = assetJson.value("browser_download_url", std::string());
                    asset.sizeBytes = assetJson.value("size", uint64_t{0});
                    candidate.assets.push_back(std::move(asset));
                }
            }
            if (findAssetBySuffix(candidate, requiredAssetSuffix) == nullptr) continue;
            best = version;
            result.tagName = std::move(candidate.tagName);
            result.assets = std::move(candidate.assets);
        }
        if (!best.valid) {
            result.error = "no published release has a " + requiredAssetSuffix + " archive yet";
            return result;
        }
    } catch (const nlohmann::json::exception& e) {
        result.error = std::string("could not parse GitHub's response: ") + e.what();
        return result;
    }

    result.success = true;
    return result;
}

const ReleaseAsset* findAssetBySuffix(const LatestRelease& release, const std::string& suffix) {
    for (const ReleaseAsset& asset : release.assets) {
        if (asset.name.size() >= suffix.size() && asset.name.compare(asset.name.size() - suffix.size(),
                                                                        suffix.size(), suffix) == 0) {
            return &asset;
        }
    }
    return nullptr;
}

} // namespace kronos_installer
