#include "studio/PluginManifest.hpp"

#include <fstream>
#include <sstream>

namespace engine::studio {

bool PluginManifest::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "PLUGIN 1\n";
    out << "NAME " << name << "\n";
    out << "VERSION " << version << "\n";
    out << "AUTHOR " << author << "\n";
    out << "DESCRIPTION " << description << "\n";
    out << "ENTRY " << entryScript << "\n";
    out << "END\n";
    return out.good();
}

bool PluginManifest::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("PLUGIN", 0) != 0) return false;

    PluginManifest loaded;
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
        else if (key == "VERSION") loaded.version = rest;
        else if (key == "AUTHOR") loaded.author = rest;
        else if (key == "DESCRIPTION") loaded.description = rest;
        else if (key == "ENTRY") loaded.entryScript = rest;
        // Unknown keys are skipped, not an error -- forward-compatible
        // with a future manifest field this reader predates, same
        // convention AnimationClip/Prefab's loaders already use.
    }
    return false; // reached EOF without an END terminator -- malformed/truncated file
}

} // namespace engine::studio
