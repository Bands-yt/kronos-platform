#include "core/BinaryIO.hpp"

#include <algorithm>
#include <cstring>
#include <limits>

namespace engine::core {

// --- BinaryWriter --------------------------------------------------------

void BinaryWriter::writeU8(uint8_t v) { buffer_.push_back(v); }

void BinaryWriter::writeU32(uint32_t v) {
    // Explicit little-endian byte order, independent of host endianness --
    // the same real reason net::ByteWriter::writeU32() does this (see
    // that function's own comment): a .kronos file may be written on one
    // machine and read on another.
    buffer_.push_back(static_cast<uint8_t>(v & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buffer_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

void BinaryWriter::writeFloat(float v) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeU32(bits);
}

void BinaryWriter::writeVec3(const glm::vec3& v) {
    writeFloat(v.x);
    writeFloat(v.y);
    writeFloat(v.z);
}

void BinaryWriter::writeVec4(const glm::vec4& v) {
    writeFloat(v.x);
    writeFloat(v.y);
    writeFloat(v.z);
    writeFloat(v.w);
}

void BinaryWriter::writeQuat(const glm::quat& q) {
    writeFloat(q.x);
    writeFloat(q.y);
    writeFloat(q.z);
    writeFloat(q.w);
}

void BinaryWriter::writeString(const std::string& s) {
    uint32_t length = static_cast<uint32_t>(std::min<size_t>(s.size(), std::numeric_limits<uint32_t>::max()));
    writeU32(length);
    buffer_.insert(buffer_.end(), s.begin(), s.begin() + length);
}

// --- BinaryReader ----------------------------------------------------------

bool BinaryReader::ensure(size_t n) {
    if (error_ || offset_ + n > size_) {
        error_ = true;
        return false;
    }
    return true;
}

uint8_t BinaryReader::readU8() {
    if (!ensure(1)) return 0;
    return data_[offset_++];
}

uint32_t BinaryReader::readU32() {
    if (!ensure(4)) return 0;
    uint32_t v = static_cast<uint32_t>(data_[offset_]) | (static_cast<uint32_t>(data_[offset_ + 1]) << 8) |
                 (static_cast<uint32_t>(data_[offset_ + 2]) << 16) | (static_cast<uint32_t>(data_[offset_ + 3]) << 24);
    offset_ += 4;
    return v;
}

float BinaryReader::readFloat() {
    uint32_t bits = readU32();
    float v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

glm::vec3 BinaryReader::readVec3() {
    float x = readFloat();
    float y = readFloat();
    float z = readFloat();
    return {x, y, z};
}

glm::vec4 BinaryReader::readVec4() {
    float x = readFloat();
    float y = readFloat();
    float z = readFloat();
    float w = readFloat();
    return {x, y, z, w};
}

glm::quat BinaryReader::readQuat() {
    float x = readFloat();
    float y = readFloat();
    float z = readFloat();
    float w = readFloat();
    return {w, x, y, z}; // glm::quat's own constructor is (w, x, y, z)
}

std::string BinaryReader::readString() {
    uint32_t length = readU32();
    if (!ensure(length)) return {};
    std::string s(reinterpret_cast<const char*>(data_ + offset_), length);
    offset_ += length;
    return s;
}

} // namespace engine::core
