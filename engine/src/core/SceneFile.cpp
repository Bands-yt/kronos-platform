#include "core/SceneFile.hpp"

#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>

#include "core/BinaryIO.hpp"

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
        case 5: return MeshSourceKind::Gltf;
        case 6: return MeshSourceKind::Fbx;
        default: return MeshSourceKind::Box; // unrecognized on load -- fail soft, matching Prefab::loadFromFile
    }
}

// Kronos ("Game Catalogue Overhaul", Phase 1) -- same index-based
// per-kind convention as meshSourceKindToIndex/FromIndex above, applied
// to RigidBody/ColliderShape.
int rigidBodyMotionTypeToIndex(RigidBodyMotionType type) { return static_cast<int>(type); }

RigidBodyMotionType rigidBodyMotionTypeFromIndex(int index) {
    switch (index) {
        case 0: return RigidBodyMotionType::Static;
        case 1: return RigidBodyMotionType::Kinematic;
        case 2: return RigidBodyMotionType::Dynamic;
        default: return RigidBodyMotionType::Static; // unrecognized on load -- fail soft
    }
}

int colliderShapeKindToIndex(ColliderShapeKind kind) { return static_cast<int>(kind); }

ColliderShapeKind colliderShapeKindFromIndex(int index) {
    switch (index) {
        case 0: return ColliderShapeKind::Box;
        case 1: return ColliderShapeKind::Sphere;
        case 2: return ColliderShapeKind::Capsule;
        case 3: return ColliderShapeKind::Mesh;
        default: return ColliderShapeKind::Box; // unrecognized on load -- fail soft
    }
}

// Small, real, local base64 codec -- SCRIPT is the one field in this
// whole line-oriented text format that can legitimately contain embedded
// newlines (a script's own source), which every other field's plain
// space-separated convention can't represent safely. No external
// dependency needed for something this small and self-contained.
constexpr char kBase64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::string& input) {
    std::string out;
    out.reserve(((input.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < input.size()) {
        uint32_t chunk = (static_cast<uint8_t>(input[i]) << 16) | (static_cast<uint8_t>(input[i + 1]) << 8) |
                          static_cast<uint8_t>(input[i + 2]);
        out += kBase64Chars[(chunk >> 18) & 0x3F];
        out += kBase64Chars[(chunk >> 12) & 0x3F];
        out += kBase64Chars[(chunk >> 6) & 0x3F];
        out += kBase64Chars[chunk & 0x3F];
        i += 3;
    }
    size_t remaining = input.size() - i;
    if (remaining == 1) {
        uint32_t chunk = static_cast<uint8_t>(input[i]) << 16;
        out += kBase64Chars[(chunk >> 18) & 0x3F];
        out += kBase64Chars[(chunk >> 12) & 0x3F];
        out += "==";
    } else if (remaining == 2) {
        uint32_t chunk = (static_cast<uint8_t>(input[i]) << 16) | (static_cast<uint8_t>(input[i + 1]) << 8);
        out += kBase64Chars[(chunk >> 18) & 0x3F];
        out += kBase64Chars[(chunk >> 12) & 0x3F];
        out += kBase64Chars[(chunk >> 6) & 0x3F];
        out += '=';
    }
    return out;
}

std::string base64Decode(const std::string& input) {
    int reverseTable[256];
    for (int& v : reverseTable) v = -1;
    for (int i = 0; i < 64; ++i) reverseTable[static_cast<unsigned char>(kBase64Chars[i])] = i;

    std::string out;
    out.reserve((input.size() / 4) * 3);
    uint32_t buffer = 0;
    int bitsCollected = 0;
    for (char c : input) {
        if (c == '=' || c == '\r' || c == '\n') continue;
        int value = reverseTable[static_cast<unsigned char>(c)];
        if (value < 0) continue; // real, honest skip of anything malformed rather than aborting the whole decode
        buffer = (buffer << 6) | static_cast<uint32_t>(value);
        bitsCollected += 6;
        if (bitsCollected >= 8) {
            bitsCollected -= 8;
            out += static_cast<char>((buffer >> bitsCollected) & 0xFF);
        }
    }
    return out;
}

constexpr uint32_t kBinaryMagic = 0x4E435343; // "KSCN" as a little-endian u32 (bytes 'K','S','C','N')
constexpr uint32_t kBinaryVersion = 1;

bool hasKronosExtension(const std::string& path) {
    constexpr std::string_view kExt = ".kronos";
    return path.size() >= kExt.size() && path.compare(path.size() - kExt.size(), kExt.size(), kExt) == 0;
}

} // namespace

bool SceneFile::saveToFile(const std::string& path) const {
    if (hasKronosExtension(path)) return saveToBinaryFile(path);

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

        if (e.hasRigidBody) {
            out << "RIGIDBODY " << rigidBodyMotionTypeToIndex(e.motionType) << "\n";

            if (e.hasColliderShape) {
                // Same trailing-path convention as MESHSOURCE above -- path
                // is last on the line, "-" when empty, so a real path with
                // spaces round-trips correctly.
                out << "COLLIDER " << colliderShapeKindToIndex(e.colliderShape.kind) << ' '
                    << e.colliderShape.params.x << ' ' << e.colliderShape.params.y << ' '
                    << e.colliderShape.params.z << ' ' << (e.colliderShape.path.empty() ? "-" : e.colliderShape.path)
                    << "\n";
            }
        }

        if (e.hasScript) {
            out << "SCRIPT " << (e.scriptAutoRun ? 1 : 0) << ' ' << base64Encode(e.scriptSource) << "\n";
        }
    }

    out << "END\n";
    return out.good();
}

bool SceneFile::loadFromFile(const std::string& path) {
    if (hasKronosExtension(path)) return loadFromBinaryFile(path);

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
        } else if (line.rfind("RIGIDBODY ", 0) == 0 && current != nullptr) {
            current->hasRigidBody = true;
            std::istringstream iss(line.substr(10));
            int motionTypeIndex = 0;
            iss >> motionTypeIndex;
            current->motionType = rigidBodyMotionTypeFromIndex(motionTypeIndex);
        } else if (line.rfind("COLLIDER ", 0) == 0 && current != nullptr) {
            current->hasColliderShape = true;
            std::istringstream iss(line.substr(9));
            int kindIndex = 0;
            iss >> kindIndex >> current->colliderShape.params.x >> current->colliderShape.params.y >>
                current->colliderShape.params.z;
            current->colliderShape.kind = colliderShapeKindFromIndex(kindIndex);
            std::string rest;
            std::getline(iss, rest);
            if (!rest.empty() && rest.front() == ' ') rest.erase(rest.begin());
            current->colliderShape.path = (rest.empty() || rest == "-") ? std::string() : rest;
        } else if (line.rfind("SCRIPT ", 0) == 0 && current != nullptr) {
            current->hasScript = true;
            std::string rest = line.substr(7);
            std::istringstream iss(rest);
            int autoRunInt = 1;
            std::string encoded;
            iss >> autoRunInt >> encoded;
            current->scriptAutoRun = autoRunInt != 0;
            current->scriptSource = base64Decode(encoded);
        } else if (line == "END") {
            break;
        }
        // Any other/unrecognized line is skipped -- forward-compatible with
        // a future field addition, same convention as AnimationClip.
    }

    *this = std::move(loaded);
    return true;
}

// Real binary format -- exact field-for-field parity with the text
// format above, kept in sync by hand rather than sharing one
// implementation: the text format's own field order/optionality was
// designed around "KEY value per line, human-diffable," and forcing a
// single (de)serialization path down to bytes-vs-text would make
// neither format read cleanly. A real round-trip test
// (tests/test_main.cpp) is what actually guarantees the two stay in
// parity, not a shared code path.
bool SceneFile::saveToBinaryFile(const std::string& path) const {
    BinaryWriter w;
    w.writeU32(kBinaryMagic);
    w.writeU32(kBinaryVersion);

    w.writeVec3(cameraPosition);
    w.writeFloat(cameraYawDegrees);
    w.writeFloat(cameraPitchDegrees);
    w.writeFloat(cameraFovDegrees);

    w.writeU32(static_cast<uint32_t>(entities.size()));
    for (const auto& e : entities) {
        w.writeString(e.name);
        w.writeString(e.parentName);
        w.writeVec3(e.position);
        w.writeQuat(e.rotation);
        w.writeVec3(e.scale);

        w.writeBool(e.hasRenderable);
        if (e.hasRenderable) {
            w.writeVec4(e.baseColor);
            w.writeFloat(e.metallic);
            w.writeFloat(e.roughness);
            w.writeFloat(e.normalIntensity);
            w.writeVec3(e.emissiveColor);
            w.writeFloat(e.emissiveIntensity);
            w.writeBool(e.castsShadow);
            w.writeBool(e.instanced);

            w.writeBool(e.hasMeshSource);
            if (e.hasMeshSource) {
                w.writeU8(static_cast<uint8_t>(meshSourceKindToIndex(e.meshSource.kind)));
                w.writeVec3(e.meshSource.params);
                w.writeString(e.meshSource.path);
            }
        }

        w.writeBool(e.hasParticleEmitter);
        if (e.hasParticleEmitter) {
            const auto& s = e.emitter;
            w.writeBool(s.enabled);
            w.writeBool(s.looping);
            w.writeFloat(s.emissionRate);
            w.writeFloat(s.particleLifetime);
            w.writeFloat(s.particleLifetimeVariance);
            w.writeVec3(s.velocityMin);
            w.writeVec3(s.velocityMax);
            w.writeVec3(s.gravity);
            w.writeFloat(s.sizeStart);
            w.writeFloat(s.sizeEnd);
            w.writeVec4(s.colorStart);
            w.writeVec4(s.colorEnd);
        }

        w.writeBool(e.hasLight);
        if (e.hasLight) {
            w.writeBool(e.light.enabled);
            w.writeVec3(e.light.color);
            w.writeFloat(e.light.intensity);
            w.writeFloat(e.light.radius);
        }

        w.writeBool(e.hasRigidBody);
        if (e.hasRigidBody) {
            w.writeU8(static_cast<uint8_t>(rigidBodyMotionTypeToIndex(e.motionType)));
            w.writeBool(e.hasColliderShape);
            if (e.hasColliderShape) {
                w.writeU8(static_cast<uint8_t>(colliderShapeKindToIndex(e.colliderShape.kind)));
                w.writeVec3(e.colliderShape.params);
                w.writeString(e.colliderShape.path);
            }
        }

        w.writeBool(e.hasScript);
        if (e.hasScript) {
            w.writeBool(e.scriptAutoRun);
            // Real, length-prefixed raw bytes -- unlike the text format's
            // own SCRIPT line, a binary length-prefixed string handles
            // embedded newlines/arbitrary bytes natively, no base64
            // workaround needed here.
            w.writeString(e.scriptSource);
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    const auto& bytes = w.bytes();
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return out.good();
}

bool SceneFile::loadFromBinaryFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    BinaryReader r(bytes.data(), bytes.size());
    if (r.readU32() != kBinaryMagic) return false; // not a real .kronos file at all
    uint32_t version = r.readU32();
    if (version != kBinaryVersion) return false; // real, honest refusal -- no older version to migrate from yet

    SceneFile loaded;
    loaded.cameraPosition = r.readVec3();
    loaded.cameraYawDegrees = r.readFloat();
    loaded.cameraPitchDegrees = r.readFloat();
    loaded.cameraFovDegrees = r.readFloat();

    uint32_t entityCount = r.readU32();
    // Real, honest sanity cap -- a corrupted/truncated file could read
    // back a huge entityCount from garbage bytes; without this, the
    // loop below would try to reserve/emplace an absurd number of
    // entities before ever hitting hasError() on the reads themselves.
    constexpr uint32_t kMaxReasonableEntityCount = 1'000'000;
    if (entityCount > kMaxReasonableEntityCount) return false;
    loaded.entities.reserve(entityCount);

    for (uint32_t i = 0; i < entityCount && !r.hasError(); ++i) {
        SceneEntityRecord e;
        e.name = r.readString();
        e.parentName = r.readString();
        e.position = r.readVec3();
        e.rotation = r.readQuat();
        e.scale = r.readVec3();

        e.hasRenderable = r.readBool();
        if (e.hasRenderable) {
            e.baseColor = r.readVec4();
            e.metallic = r.readFloat();
            e.roughness = r.readFloat();
            e.normalIntensity = r.readFloat();
            e.emissiveColor = r.readVec3();
            e.emissiveIntensity = r.readFloat();
            e.castsShadow = r.readBool();
            e.instanced = r.readBool();

            e.hasMeshSource = r.readBool();
            if (e.hasMeshSource) {
                e.meshSource.kind = meshSourceKindFromIndex(r.readU8());
                e.meshSource.params = r.readVec3();
                e.meshSource.path = r.readString();
            }
        }

        e.hasParticleEmitter = r.readBool();
        if (e.hasParticleEmitter) {
            auto& s = e.emitter;
            s.enabled = r.readBool();
            s.looping = r.readBool();
            s.emissionRate = r.readFloat();
            s.particleLifetime = r.readFloat();
            s.particleLifetimeVariance = r.readFloat();
            s.velocityMin = r.readVec3();
            s.velocityMax = r.readVec3();
            s.gravity = r.readVec3();
            s.sizeStart = r.readFloat();
            s.sizeEnd = r.readFloat();
            s.colorStart = r.readVec4();
            s.colorEnd = r.readVec4();
        }

        e.hasLight = r.readBool();
        if (e.hasLight) {
            e.light.enabled = r.readBool();
            e.light.color = r.readVec3();
            e.light.intensity = r.readFloat();
            e.light.radius = r.readFloat();
        }

        e.hasRigidBody = r.readBool();
        if (e.hasRigidBody) {
            e.motionType = rigidBodyMotionTypeFromIndex(r.readU8());
            e.hasColliderShape = r.readBool();
            if (e.hasColliderShape) {
                e.colliderShape.kind = colliderShapeKindFromIndex(r.readU8());
                e.colliderShape.params = r.readVec3();
                e.colliderShape.path = r.readString();
            }
        }

        e.hasScript = r.readBool();
        if (e.hasScript) {
            e.scriptAutoRun = r.readBool();
            e.scriptSource = r.readString();
        }

        loaded.entities.push_back(std::move(e));
    }

    if (r.hasError()) return false; // real, honest refusal on any truncated/corrupted read, not a partial scene

    *this = std::move(loaded);
    return true;
}

} // namespace engine::core
