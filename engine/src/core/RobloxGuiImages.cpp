#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <system_error>

#include <nlohmann/json.hpp>

#include "core/RobloxGui.hpp"

namespace engine::core::gui {
namespace {

namespace fs = std::filesystem;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool isImageExtension(const fs::path& path) {
    const std::string ext = lower(path.extension().string());
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
}

// A file under `root` (no escaping with "..") that exists and is an image.
std::string inside(const fs::path& root, const std::string& relative) {
    if (relative.empty()) return {};
    const fs::path rel(relative);
    if (rel.is_absolute() || rel.has_root_name()) return {};
    std::error_code ec;
    const fs::path base = fs::weakly_canonical(root, ec);
    if (ec) return {};
    const fs::path full = fs::weakly_canonical(base / rel, ec);
    if (ec || !isImageExtension(full)) return {};
    const auto [end, ignored] = std::mismatch(base.begin(), base.end(), full.begin(), full.end());
    if (end != base.end() || !fs::is_regular_file(full, ec)) return {};
    return full.string();
}

std::string mappedId(const fs::path& root, const std::string& id) {
    for (const char* name : {"asset_map.json", "assets/asset_map.json"}) {
        std::ifstream in(root / name);
        if (!in) continue;
        const nlohmann::json map = nlohmann::json::parse(in, nullptr, false);
        if (!map.is_object()) continue;
        const auto found = map.find(id);
        if (found != map.end() && found->is_string()) {
            if (std::string file = inside(root, found->get<std::string>()); !file.empty()) return file;
        }
    }
    return {};
}

} // namespace

std::string assetIdOf(const std::string& image) {
    const std::string text = lower(image);
    std::string digits;
    if (text.rfind("rbxassetid://", 0) == 0) {
        digits = text.substr(13);
        while (!digits.empty() && digits.front() == '/') digits.erase(digits.begin());
    } else if (text.find("roblox.com/asset") != std::string::npos) {
        const size_t id = text.find("id=");
        if (id != std::string::npos) digits = text.substr(id + 3);
    } else {
        return {};
    }
    const size_t end = digits.find_first_not_of("0123456789");
    digits = digits.substr(0, end);
    return digits;
}

std::string imageFile(const std::string& image, const std::vector<std::filesystem::path>& roots) {
    if (image.empty()) return {};
    if (const std::string id = assetIdOf(image); !id.empty()) {
        for (const fs::path& root : roots) {
            if (std::string file = mappedId(root, id); !file.empty()) return file;
            for (const char* folder : {"assets/", "assets/images/", "images/", ""}) {
                for (const char* ext : {".png", ".jpg", ".jpeg"}) {
                    if (std::string file = inside(root, folder + id + ext); !file.empty()) return file;
                }
            }
        }
        return {};
    }
    if (image.find("://") != std::string::npos) return {}; // rbxasset:// and web links aren't fetched
    for (const fs::path& root : roots) {
        if (std::string file = inside(root, image); !file.empty()) return file;
    }
    return {};
}

std::vector<std::string> missingImages(ECS& ecs, const std::vector<std::filesystem::path>& roots) {
    std::set<std::string> missing;
    for (InstanceRef ref : instances::descendants(ecs, kGameInstance)) {
        const std::string cls = instances::className(ecs, ref);
        if (cls != "ImageLabel" && cls != "ImageButton") continue;
        const PropertyDef* def = instances::findProperty(cls, "Image");
        InstanceValue value;
        if (def == nullptr || !instances::getProperty(ecs, ref, *def, value) || value.text.empty()) continue;
        if (imageFile(value.text, roots).empty()) missing.insert(value.text);
    }
    return {missing.begin(), missing.end()};
}

} // namespace engine::core::gui
