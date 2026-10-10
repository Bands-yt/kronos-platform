#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <glm/glm.hpp>

// Byte writer and bounds-checked reader for the Roblox network messages.
namespace engine::core::wire {

class Writer {
public:
    explicit Writer(std::vector<uint8_t>& out) : out_(out) {}
    void u8(uint8_t v) { out_.push_back(v); }
    void u16(uint16_t v) { raw(&v, sizeof v); }
    void u32(uint32_t v) { raw(&v, sizeof v); }
    void f32(float v) { raw(&v, sizeof v); }
    void f64(double v) { raw(&v, sizeof v); }
    void str(const std::string& s) {
        u32(static_cast<uint32_t>(s.size()));
        raw(s.data(), s.size());
    }
    void vec3(const glm::vec3& v) {
        f32(v.x);
        f32(v.y);
        f32(v.z);
    }

private:
    void raw(const void* data, size_t size) {
        const auto* bytes = static_cast<const uint8_t*>(data);
        out_.insert(out_.end(), bytes, bytes + size);
    }
    std::vector<uint8_t>& out_;
};

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : p_(data), end_(data + size) {}
    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] size_t left() const { return static_cast<size_t>(end_ - p_); }
    uint8_t u8() {
        uint8_t v = 0;
        raw(&v, sizeof v);
        return v;
    }
    uint16_t u16() {
        uint16_t v = 0;
        raw(&v, sizeof v);
        return v;
    }
    uint32_t u32() {
        uint32_t v = 0;
        raw(&v, sizeof v);
        return v;
    }
    float f32() {
        float v = 0;
        raw(&v, sizeof v);
        return v;
    }
    double f64() {
        double v = 0;
        raw(&v, sizeof v);
        return v;
    }
    std::string str() {
        const uint32_t size = u32();
        if (!ok_ || size > left()) {
            ok_ = false;
            return {};
        }
        std::string s(reinterpret_cast<const char*>(p_), size);
        p_ += size;
        return s;
    }
    glm::vec3 vec3() {
        const float x = f32();
        const float y = f32();
        const float z = f32();
        return {x, y, z};
    }

private:
    void raw(void* out, size_t size) {
        if (!ok_ || size > left()) {
            ok_ = false;
            return;
        }
        std::memcpy(out, p_, size);
        p_ += size;
    }
    const uint8_t* p_;
    const uint8_t* end_;
    bool ok_ = true;
};

} // namespace engine::core::wire
