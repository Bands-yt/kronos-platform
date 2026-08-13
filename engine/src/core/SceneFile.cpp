#include "core/SceneFile.hpp"

#include <fstream>
#include <sstream>

namespace engine::core {

namespace {

int meshSourceKindToIndex(MeshSourceKind kind) { return static_cast<int>(kind); }

MeshSourceKind meshSourceKindFromIndex(int index) {
    switch (index) {
        case 0: return MeshSourceKind::Box;
        case 1: return MeshSourceKind::Plane;
        case 2: return MeshSourceKind::Capsule;
        case 3: return MeshSourceKind::Quad;
        case 4: return MeshSourceKind::Obj;
        default: return MeshSourceKind::Box; // unrecognized on load -- fail soft, matching Prefab::loadFromFile
    }
}

} // namespace

bool SceneFile::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "SCENE 1\n";
    out << "CAMERA " << cameraPosition.x << ' ' << cameraPosition.y << ' ' << cameraPosition.z << ' '
        << cameraYawDegrees << ' ' << cameraPitchDegrees << ' ' << cameraFovDegrees << "\n";

    for (const auto& e : entities) {
        out << "ENTITY " << e.name << "\n";
        // Only emitted for a real, non-root entity -- an absent PARENT
        // line means root on load, same "field just doesn't appear" outer
        // convention hasRenderable/hasMeshSource/hasParticleEmitter above
        // already use for their own optional blocks.
        if (!e.parentName.empty()) out << "PARENT " << e.parentName << "\n";
        out << "TRANSFORM " << e.position.x << ' ' << e.position.y << ' ' << e.position.z << ' ' << e.rotation.x
            << ' ' << e.rotation.y << ' ' << e.rotation.z << ' ' << e.rotation.w << ' ' << e.scale.x << ' '
            << e.scale.y << ' ' << e.scale.z << "\n";

        if (e.hasRenderable) {
            out << "RENDERABLE " << e.baseColor.x << ' ' << e.baseColor.y << ' ' << e.baseColor.z << ' '
                << e.baseColor.w << ' ' << e.metallic << ' ' << e.roughness << ' ' << e.normalIntensity << ' '
                << e.emissiveColor.x << ' ' << e.emissiveColor.y << ' ' << e.emissiveColor.z << ' '
                << e.emissiveIntensity << ' ' << (e.castsShadow ? 1 : 0) << ' ' << (e.instanced ? 1 : 0) << "\n";

            if (e.hasMeshSource) {
                // path is last on the line (never quoted -- loadFromFile
                // reads it as "everything after the numeric fields", same
                // trailing-string convention as PluginManifest's fields)
                // so a real path with spaces round-trips correctly.
                out << "MESHSOURCE " << meshSourceKindToIndex(e.meshSource.kind) << ' ' << e.meshSource.params.x
                    << ' ' << e.meshSource.params.y << ' ' << e.meshSource.params.z << ' '
                    << (e.meshSource.path.empty() ? "-" : e.meshSource.path) << "\n";
            }
        }

        if (e.hasParticleEmitter) {
            const auto& s = e.emitter;
            out << "EMITTER " << (s.enabled ? 1 : 0) << ' ' << (s.looping ? 1 : 0) << ' ' << s.emissionRate << ' '
                << s.particleLifetime << ' ' << s.particleLifetimeVariance << ' ' << s.velocityMin.x << ' '
                << s.velocityMin.y << ' ' << s.velocityMin.z << ' ' << s.velocityMax.x << ' ' << s.velocityMax.y
                << ' ' << s.velocityMax.z << ' ' << s.gravity.x << ' ' << s.gravity.y << ' ' << s.gravity.z << ' '
                << s.sizeStart << ' ' << s.sizeEnd << ' ' << s.colorStart.x << ' ' << s.colorStart.y << ' '
                << s.colorStart.z << ' ' << s.colorStart.w << ' ' << s.colorEnd.x << ' ' << s.colorEnd.y << ' '
                << s.colorEnd.z << ' ' << s.colorEnd.w << "\n";
        }

        if (e.hasLight) {
            const auto& l = e.light;
            out << "LIGHT " << (l.enabled ? 1 : 0) << ' ' << l.color.x << ' ' << l.color.y << ' ' << l.color.z
                << ' ' << l.intensity << ' ' << l.radius << "\n";
        }
    }

    out << "END\n";
    return out.good();
}

bool SceneFile::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("SCENE", 0) != 0) return false;

    SceneFile loaded;
    SceneEntityRecord* current = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("CAMERA ", 0) == 0) {
            std::istringstream iss(line.substr(7));
            iss >> loaded.cameraPosition.x >> loaded.cameraPosition.y >> loaded.cameraPosition.z >>
                loaded.cameraYawDegrees >> loaded.cameraPitchDegrees >> loaded.cameraFovDegrees;
        } else if (line.rfind("ENTITY ", 0) == 0) {
            loaded.entities.emplace_back();
            loaded.entities.back().name = line.substr(7);
            current = &loaded.entities.back(); // refreshed here, never held across a later emplace_back
        } else if (line.rfind("PARENT ", 0) == 0 && current != nullptr) {
            current->parentName = line.substr(7);
        } else if (line.rfind("TRANSFORM ", 0) == 0 && current != nullptr) {
            std::istringstream iss(line.substr(10));
            iss >> current->position.x >> current->position.y >> current->position.z >> current->rotation.x >>
                current->rotation.y >> current->rotation.z >> current->rotation.w >> current->scale.x >>
                current->scale.y >> current->scale.z;
        } else if (line.rfind("RENDERABLE ", 0) == 0 && current != nullptr) {
            current->hasRenderable = true;
            std::istringstream iss(line.substr(11));
            int castsShadowInt = 1;
            int instancedInt = 0;
            iss >> current->baseColor.x >> current->baseColor.y >> current->baseColor.z >> current->baseColor.w >>
                current->metallic >> current->roughness >> current->normalIntensity >> current->emissiveColor.x >>
                current->emissiveColor.y >> current->emissiveColor.z >> current->emissiveIntensity >>
                castsShadowInt >> instancedInt;
            current->castsShadow = castsShadowInt != 0;
            current->instanced = instancedInt != 0;
        } else if (line.rfind("MESHSOURCE ", 0) == 0 && current != nullptr) {
            current->hasMeshSource = true;
            std::istringstream iss(line.substr(11));
            int kindIndex = 0;
            iss >> kindIndex >> current->meshSource.params.x >> current->meshSource.params.y >>
                current->meshSource.params.z;
            current->meshSource.kind = meshSourceKindFromIndex(kindIndex);
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            current->meshSource.path = (rest.empty() || rest == "-") ? std::string() : rest;
        } else if (line.rfind("EMITTER ", 0) == 0 && current != nullptr) {
            current->hasParticleEmitter = true;
            auto& s = current->emitter;
            std::istringstream iss(line.substr(8));
            int enabledInt = 1;
            int loopingInt = 1;
            iss >> enabledInt >> loopingInt >> s.emissionRate >> s.particleLifetime >> s.particleLifetimeVariance >>
                s.velocityMin.x >> s.velocityMin.y >> s.velocityMin.z >> s.velocityMax.x >> s.velocityMax.y >>
                s.velocityMax.z >> s.gravity.x >> s.gravity.y >> s.gravity.z >> s.sizeStart >> s.sizeEnd >>
                s.colorStart.x >> s.colorStart.y >> s.colorStart.z >> s.colorStart.w >> s.colorEnd.x >>
                s.colorEnd.y >> s.colorEnd.z >> s.colorEnd.w;
            s.enabled = enabledInt != 0;
            s.looping = loopingInt != 0;
        } else if (line.rfind("LIGHT ", 0) == 0 && current != nullptr) {
            current->hasLight = true;
            std::istringstream iss(line.substr(6));
            int enabledInt = 1;
            iss >> enabledInt >> current->light.color.x >> current->light.color.y >> current->light.color.z >>
                current->light.intensity >> current->light.radius;
            current->light.enabled = enabledInt != 0;
        } else if (line == "END") {
            break;
        }
        // Any other/unrecognized line is skipped -- forward-compatible with
        // a future field addition, same convention as AnimationClip.
    }

    *this = std::move(loaded);
    return true;
}

} // namespace engine::core
