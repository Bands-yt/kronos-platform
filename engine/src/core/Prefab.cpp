#include "core/Prefab.hpp"

#include <fstream>
#include <sstream>

namespace engine::core {

namespace {
int meshKindToIndex(PrefabMeshKind kind) { return static_cast<int>(kind); }
PrefabMeshKind meshKindFromIndex(int index) {
    switch (index) {
        case 0: return PrefabMeshKind::Box;
        case 1: return PrefabMeshKind::Plane;
        case 2: return PrefabMeshKind::Capsule;
        case 3: return PrefabMeshKind::Quad;
        default: return PrefabMeshKind::Box; // unrecognized on load -- fail soft, matching AnimationClip::loadFromFile
    }
}
} // namespace

const char* prefabMeshKindName(PrefabMeshKind kind) {
    switch (kind) {
        case PrefabMeshKind::Box: return "Box";
        case PrefabMeshKind::Plane: return "Plane";
        case PrefabMeshKind::Capsule: return "Capsule";
        case PrefabMeshKind::Quad: return "Quad";
    }
    return "Box";
}

bool Prefab::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "PREFAB 1\n";
    out << "NAME " << name << "\n";
    out << "MESHKIND " << meshKindToIndex(meshKind) << "\n";
    out << "MESHPARAMS " << meshParams.x << ' ' << meshParams.y << ' ' << meshParams.z << "\n";
    out << "SCALE " << scale.x << ' ' << scale.y << ' ' << scale.z << "\n";
    out << "BASECOLOR " << baseColor.x << ' ' << baseColor.y << ' ' << baseColor.z << ' ' << baseColor.w << "\n";
    out << "METALLIC " << metallic << "\n";
    out << "ROUGHNESS " << roughness << "\n";
    out << "EMISSIVECOLOR " << emissiveColor.x << ' ' << emissiveColor.y << ' ' << emissiveColor.z << "\n";
    out << "EMISSIVEINTENSITY " << emissiveIntensity << "\n";
    out << "CASTSSHADOW " << (castsShadow ? 1 : 0) << "\n";
    out << "INSTANCED " << (instanced ? 1 : 0) << "\n";
    out << "END\n";
    return out.good();
}

bool Prefab::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("PREFAB", 0) != 0) return false;

    Prefab loaded;
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream iss(line);
        std::string key;
        iss >> key;

        if (key == "NAME") {
            loaded.name = line.substr(5);
        } else if (key == "MESHKIND") {
            int index = 0;
            iss >> index;
            loaded.meshKind = meshKindFromIndex(index);
        } else if (key == "MESHPARAMS") {
            iss >> loaded.meshParams.x >> loaded.meshParams.y >> loaded.meshParams.z;
        } else if (key == "SCALE") {
            iss >> loaded.scale.x >> loaded.scale.y >> loaded.scale.z;
        } else if (key == "BASECOLOR") {
            iss >> loaded.baseColor.x >> loaded.baseColor.y >> loaded.baseColor.z >> loaded.baseColor.w;
        } else if (key == "METALLIC") {
            iss >> loaded.metallic;
        } else if (key == "ROUGHNESS") {
            iss >> loaded.roughness;
        } else if (key == "EMISSIVECOLOR") {
            iss >> loaded.emissiveColor.x >> loaded.emissiveColor.y >> loaded.emissiveColor.z;
        } else if (key == "EMISSIVEINTENSITY") {
            iss >> loaded.emissiveIntensity;
        } else if (key == "CASTSSHADOW") {
            int value = 1;
            iss >> value;
            loaded.castsShadow = value != 0;
        } else if (key == "INSTANCED") {
            int value = 0;
            iss >> value;
            loaded.instanced = value != 0;
        } else if (key == "END") {
            break;
        }
        // Any other/unrecognized key is skipped -- forward-compatible with
        // a future field addition, same convention as AnimationClip::loadFromFile.
    }

    *this = std::move(loaded);
    return true;
}

} // namespace engine::core
