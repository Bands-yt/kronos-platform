#include "GitHubReleaseApi.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace kronos_installer {

namespace {
size_t writeCallback(char* data, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(data, size * nmemb);
    return size * nmemb;
}
} // namespace

LatestRelease fetchLatestRelease(const std::string& owner, const std::string& repo) {
    LatestRelease result;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.error = "curl_easy_init() failed";
        return result;
    }

    std::string url = "https://api.github.com/repos/" + owner + "/" + repo + "/releases/latest";
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
        nlohmann::json json = nlohmann::json::parse(responseBody);
        if (json.contains("tag_name")) result.tagName = json["tag_name"].get<std::string>();
        if (json.contains("assets") && json["assets"].is_array()) {
            for (const auto& assetJson : json["assets"]) {
                ReleaseAsset asset;
                if (assetJson.contains("name")) asset.name = assetJson["name"].get<std::string>();
                if (assetJson.contains("browser_download_url"))
                    asset.downloadUrl = assetJson["browser_download_url"].get<std::string>();
                if (assetJson.contains("size")) asset.sizeBytes = assetJson["size"].get<uint64_t>();
                result.assets.push_back(std::move(asset));
            }
        }
    } catch (const nlohmann::json::parse_error& e) {
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
