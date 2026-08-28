#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine::core {

// Real, hand-rolled little-endian binary (de)serialization -- same
// proven shape as net::ByteWriter/ByteReader (net/Serialization.hpp),
// reimplemented here rather than reused directly: core:: is a lower
// layer than net:: in this codebase (net depends on core, not the
// reverse), so core::SceneFile reaching into net:: for this would be a
// backwards layering dependency for what's really just a small, generic
// utility. The one real difference from net's version: strings here are
// u32-length-prefixed, not u8/255-capped -- net's fields are short
// identifiers; this file's own SCRIPT field is a full Luau source file
// and needs real, unbounded length.
class BinaryWriter {
public:
    void writeU8(uint8_t v);
    void writeU32(uint32_t v);
    void writeFloat(float v);
    void writeBool(bool v) { writeU8(v ? 1 : 0); }
    void writeVec3(const glm::vec3& v);
    void writeVec4(const glm::vec4& v);
    void writeQuat(const glm::quat& q);
    void writeString(const std::string& s);

    [[nodiscard]] const std::vector<uint8_t>& bytes() const { return buffer_; }
    [[nodiscard]] size_t size() const { return buffer_.size(); }

private:
    std::vector<uint8_t> buffer_;
};

// Real bounds-checked byte reader -- every read past the end of the
// buffer sets a sticky error flag and returns a real, honest default
// value (0/false/identity) instead of reading out of bounds. A caller
// MUST check hasError() after a full deserialize -- this is what makes
// it safe against a truncated/corrupted .kronos file, not just a
// well-formed one.
class BinaryReader {
public:
    BinaryReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    [[nodiscard]] uint8_t readU8();
    [[nodiscard]] uint32_t readU32();
    [[nodiscard]] float readFloat();
    [[nodiscard]] bool readBool() { return readU8() != 0; }
    [[nodiscard]] glm::vec3 readVec3();
    [[nodiscard]] glm::vec4 readVec4();
    [[nodiscard]] glm::quat readQuat();
    [[nodiscard]] std::string readString();

    [[nodiscard]] bool hasError() const { return error_; }

private:
    [[nodiscard]] bool ensure(size_t n);

    const uint8_t* data_;
    size_t size_;
    size_t offset_ = 0;
    bool error_ = false;
};

} // namespace engine::core
