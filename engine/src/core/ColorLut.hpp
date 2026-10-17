#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace engine::core {

struct ColorLut3D {
    int size = 0;
    std::vector<std::array<float, 3>> data; // size^3 entries, red changes fastest (.cube order)

    [[nodiscard]] bool valid() const { return size >= 2 && data.size() == static_cast<std::size_t>(size) * size * size; }

    [[nodiscard]] std::array<float, 3> sample(float r, float g, float b) const {
        if (!valid()) return {r, g, b};
        auto idx = [&](float v) { return std::clamp(v, 0.0f, 1.0f) * static_cast<float>(size - 1); };
        const float fr = idx(r), fg = idx(g), fb = idx(b);
        const int r0 = static_cast<int>(fr), g0 = static_cast<int>(fg), b0 = static_cast<int>(fb);
        const int r1 = std::min(r0 + 1, size - 1), g1 = std::min(g0 + 1, size - 1), b1 = std::min(b0 + 1, size - 1);
        const float tr = fr - static_cast<float>(r0), tg = fg - static_cast<float>(g0), tb = fb - static_cast<float>(b0);
        auto at = [&](int x, int y, int z) -> const std::array<float, 3>& { return data[(z * size + y) * size + x]; };
        std::array<float, 3> out{};
        for (int k = 0; k < 3; ++k) {
            const float c00 = at(r0, g0, b0)[k] * (1 - tr) + at(r1, g0, b0)[k] * tr;
            const float c10 = at(r0, g1, b0)[k] * (1 - tr) + at(r1, g1, b0)[k] * tr;
            const float c01 = at(r0, g0, b1)[k] * (1 - tr) + at(r1, g0, b1)[k] * tr;
            const float c11 = at(r0, g1, b1)[k] * (1 - tr) + at(r1, g1, b1)[k] * tr;
            out[k] = (c00 * (1 - tg) + c10 * tg) * (1 - tb) + (c01 * (1 - tg) + c11 * tg) * tb;
        }
        return out;
    }
};

// Reads an Adobe/Resolve .cube 3D LUT. Returns false (and an empty LUT) when it is malformed.
inline bool parseCubeLut(const std::string& text, ColorLut3D& out, std::string& error) {
    out = {};
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("LUT_3D_SIZE", 0) == 0) {
            out.size = std::atoi(line.c_str() + 11);
            if (out.size < 2 || out.size > 128) {
                error = "LUT_3D_SIZE must be between 2 and 128";
                out = {};
                return false;
            }
            out.data.reserve(static_cast<std::size_t>(out.size) * out.size * out.size);
            continue;
        }
        if (line.rfind("LUT_1D_SIZE", 0) == 0) {
            error = "1D LUTs are not supported";
            out = {};
            return false;
        }
        if (std::isalpha(static_cast<unsigned char>(line[0]))) continue; // TITLE, DOMAIN_MIN, ...
        std::istringstream row(line);
        std::array<float, 3> v{};
        if (!(row >> v[0] >> v[1] >> v[2])) {
            error = "bad LUT row: " + line;
            out = {};
            return false;
        }
        out.data.push_back(v);
    }
    if (!out.valid()) {
        error = "LUT has the wrong number of rows";
        out = {};
        return false;
    }
    return true;
}

} // namespace engine::core
