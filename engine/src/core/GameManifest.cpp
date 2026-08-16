#include "core/GameManifest.hpp"

#include <fstream>
#include <sstream>

namespace engine::core {

bool GameManifest::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "GAMEMANIFEST 1\n";
    out << "NAME " << name << "\n";
    out << "DESCRIPTION " << description << "\n";
    for (const std::string& tag : genreTags) out << "GENRETAG " << tag << "\n";
    out << "THUMBNAILCOLOR " << thumbnailColor.x << ' ' << thumbnailColor.y << ' ' << thumbnailColor.z << ' '
        << thumbnailColor.w << "\n";
    out << "EFFORTSCORE " << effortScore << "\n";
    out << "LAUNCHKIND " << gameLaunchKindName(launchKind) << "\n";
    out << "PROJECTPATH " << projectPath << "\n";
    out << "CLIFLAG " << cliFlag << "\n";
    out << "SAFETYSTATUS " << gameSafetyStatusName(safetyStatus) << "\n";
    out << "UNSAFEREASON " << unsafeReason << "\n";
    out << "END\n";
    return out.good();
}

bool GameManifest::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("GAMEMANIFEST", 0) != 0) return false;

    GameManifest loaded;
    loaded.genreTags.clear();
    std::string line;
    while (std::getline(in, line)) {
        if (line == "END") {
            *this = loaded;
            return true;
        }
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        std::string rest;
        std::getline(iss, rest);
        if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());

        if (key == "NAME") loaded.name = rest;
        else if (key == "DESCRIPTION") loaded.description = rest;
        else if (key == "GENRETAG") loaded.genreTags.push_back(rest);
        else if (key == "THUMBNAILCOLOR") {
            std::istringstream values(rest);
            values >> loaded.thumbnailColor.x >> loaded.thumbnailColor.y >> loaded.thumbnailColor.z >>
                loaded.thumbnailColor.w;
        } else if (key == "EFFORTSCORE") {
            loaded.effortScore = std::stof(rest);
        } else if (key == "LAUNCHKIND") {
            loaded.launchKind = rest == "CliFlag" ? GameLaunchKind::CliFlag : GameLaunchKind::ProjectPath;
        } else if (key == "PROJECTPATH") {
            loaded.projectPath = rest;
        } else if (key == "CLIFLAG") {
            loaded.cliFlag = rest;
        } else if (key == "SAFETYSTATUS") {
            if (rest == "UnderReview") loaded.safetyStatus = GameSafetyStatus::UnderReview;
            else if (rest == "Unsafe") loaded.safetyStatus = GameSafetyStatus::Unsafe;
            else loaded.safetyStatus = GameSafetyStatus::Safe;
        } else if (key == "UNSAFEREASON") {
            loaded.unsafeReason = rest;
        }
        // Unknown keys are skipped, not an error -- forward-compatible with
        // a future manifest field this reader predates, same convention
        // PluginManifest/AnimationClip/Prefab's loaders already use.
    }
    return false; // reached EOF without an END terminator -- malformed/truncated file
}

} // namespace engine::core
