#include "migration/PropertyDecoder.hpp"

#include <cmath>
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>

namespace engine::migration {
namespace {

const std::string* find(const PropertyMap& properties, const std::string& key) {
    const auto it = properties.find(key);
    return it == properties.end() ? nullptr : &it->second;
}

// Parses a float, refusing anything that is not fully numeric as well as
// NaN and infinity. An imported document is untrusted, and a NaN reaching
// a Transform corrupts every descendant through the scene graph rather
// than failing where it was read.
bool parseFloat(const std::string* text, float& out) {
    if (text == nullptr || text->empty()) return false;
    char* end = nullptr;
    const double value = std::strtod(text->c_str(), &end);
    if (end == text->c_str()) return false;
    while (end != nullptr && *end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) ++end;
    if (end != nullptr && *end != '\0') return false;
    if (!std::isfinite(value)) return false;
    out = static_cast<float>(value);
    return true;
}

bool readComponent(const PropertyMap& properties, const std::string& name, const char* field, float& out) {
    return parseFloat(find(properties, name + "." + field), out);
}

} // namespace

bool hasProperty(const PropertyMap& properties, const std::string& name) {
    return properties.find(name) != properties.end() || properties.find("@type." + name) != properties.end();
}

std::string propertyType(const PropertyMap& properties, const std::string& name) {
    const std::string* type = find(properties, "@type." + name);
    return type == nullptr ? std::string{} : *type;
}

float decodeFloat(const PropertyMap& properties, const std::string& name, float fallback) {
    float value = fallback;
    return parseFloat(find(properties, name), value) ? value : fallback;
}

int decodeInt(const PropertyMap& properties, const std::string& name, int fallback) {
    float value = 0.0f;
    return parseFloat(find(properties, name), value) ? static_cast<int>(value) : fallback;
}

bool decodeBool(const PropertyMap& properties, const std::string& name, bool fallback) {
    const std::string* text = find(properties, name);
    if (text == nullptr) return fallback;
    if (*text == "true" || *text == "1") return true;
    if (*text == "false" || *text == "0") return false;
    return fallback;
}

std::string decodeString(const PropertyMap& properties, const std::string& name, const std::string& fallback) {
    const std::string* text = find(properties, name);
    return text == nullptr ? fallback : *text;
}

glm::vec3 decodeVector3(const PropertyMap& properties, const std::string& name, glm::vec3 fallback) {
    glm::vec3 result = fallback;
    // All three or none: a half-decoded vector (X read, Y missing) would
    // place a part somewhere the document never described.
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!readComponent(properties, name, "X", x)) return fallback;
    if (!readComponent(properties, name, "Y", y)) return fallback;
    if (!readComponent(properties, name, "Z", z)) return fallback;
    result = glm::vec3(x, y, z);
    return result;
}

glm::vec3 decodeColor3(const PropertyMap& properties, const std::string& name, glm::vec3 fallback) {
    // Packed form first: Color3uint8 is written as a single integer, so it
    // has no R/G/B children to read.
    const std::string type = propertyType(properties, name);
    const std::string* packed = find(properties, name);
    if (type == "Color3uint8" && packed != nullptr && !packed->empty()) {
        char* end = nullptr;
        const unsigned long long raw = std::strtoull(packed->c_str(), &end, 10);
        if (end != packed->c_str()) {
            const auto r = static_cast<float>((raw >> 16) & 0xFFu);
            const auto g = static_cast<float>((raw >> 8) & 0xFFu);
            const auto b = static_cast<float>(raw & 0xFFu);
            return glm::vec3(r, g, b) / 255.0f;
        }
        return fallback;
    }

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (!readComponent(properties, name, "R", r)) return fallback;
    if (!readComponent(properties, name, "G", g)) return fallback;
    if (!readComponent(properties, name, "B", b)) return fallback;
    return glm::vec3(r, g, b);
}

glm::vec3 decodeCFramePosition(const PropertyMap& properties, const std::string& name, glm::vec3 fallback) {
    return decodeVector3(properties, name, fallback);
}

glm::quat decodeCFrameRotation(const PropertyMap& properties, const std::string& name, glm::quat fallback) {
    float m[9];
    static const char* kFields[9] = {"R00", "R01", "R02", "R10", "R11", "R12", "R20", "R21", "R22"};
    for (int i = 0; i < 9; ++i) {
        if (!readComponent(properties, name, kFields[i], m[i])) return fallback;
    }

    // Roblox writes rows; glm::mat3's constructor takes columns. Feeding
    // the row order straight in transposes the basis, which inverts every
    // rotation in the place.
    const glm::mat3 basis(glm::vec3(m[0], m[3], m[6]),  // column 0
                           glm::vec3(m[1], m[4], m[7]),  // column 1
                           glm::vec3(m[2], m[5], m[8])); // column 2

    // Reject anything that is not actually a rotation. A degenerate or
    // mirrored basis turns into a garbage quaternion that is far harder to
    // diagnose downstream than simply not rotating.
    for (int c = 0; c < 3; ++c) {
        const float length = glm::length(basis[c]);
        if (!std::isfinite(length) || std::fabs(length - 1.0f) > 0.05f) return fallback;
    }
    if (glm::determinant(basis) < 0.0f) return fallback; // mirrored, not a rotation

    return glm::normalize(glm::quat_cast(basis));
}

} // namespace engine::migration
