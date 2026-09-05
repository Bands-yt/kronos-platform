#include "core/CubeLut.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

namespace engine::core {

namespace {
constexpr uint32_t kMinLutSize = 2;  // Vulkan's own real minimum 3D image extent
constexpr uint32_t kMaxLutSize = 256; // generous real ceiling -- guards a corrupt file's claimed size

// Strips a trailing '#'-introduced comment and surrounding whitespace --
// real .cube files from every major tool put comments at line start, but
// the format spec allows them anywhere on a line.
std::string stripCommentAndTrim(const std::string& line) {
    std::string result = line.substr(0, line.find('#'));
    const size_t begin = result.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return "";
    const size_t end = result.find_last_not_of(" \t\r\n");
    return result.substr(begin, end - begin + 1);
}
} // namespace

bool parseCubeLutFile(const std::string& path, CubeLutData& outData, std::string& outError) {
    std::ifstream file(path);
    if (!file.is_open()) {
        outError = "Could not open \"" + path + "\".";
        return false;
    }

    uint32_t size = 0;
    float domainMin[3] = {0.0f, 0.0f, 0.0f};
    float domainMax[3] = {1.0f, 1.0f, 1.0f};
    std::vector<float> values; // flat r,g,b,r,g,b,... in file order

    std::string rawLine;
    while (std::getline(file, rawLine)) {
        const std::string line = stripCommentAndTrim(rawLine);
        if (line.empty()) continue;

        std::istringstream tokens(line);
        std::string keyword;
        tokens >> keyword;

        if (keyword == "TITLE") {
            continue; // real, honestly ignored -- purely descriptive
        }
        if (keyword == "LUT_1D_SIZE") {
            outError = "\"" + path + "\": LUT_1D_SIZE is a 1D LUT -- not supported (this engine only samples a 3D LUT).";
            return false;
        }
        if (keyword == "LUT_3D_SIZE") {
            if (!(tokens >> size)) {
                outError = "\"" + path + "\": LUT_3D_SIZE line has no readable size.";
                return false;
            }
            if (size < kMinLutSize || size > kMaxLutSize) {
                outError = "\"" + path + "\": LUT_3D_SIZE " + std::to_string(size) + " is outside the supported [" +
                            std::to_string(kMinLutSize) + ", " + std::to_string(kMaxLutSize) + "] range.";
                return false;
            }
            continue;
        }
        if (keyword == "DOMAIN_MIN" || keyword == "DOMAIN_MAX") {
            float* target = keyword == "DOMAIN_MIN" ? domainMin : domainMax;
            if (!(tokens >> target[0] >> target[1] >> target[2])) {
                outError = "\"" + path + "\": " + keyword + " line does not have three readable values.";
                return false;
            }
            continue;
        }

        // Anything else is a real data line -- three floats, no keyword.
        float r = 0.0f, g = 0.0f, b = 0.0f;
        std::istringstream dataTokens(line);
        if (!(dataTokens >> r >> g >> b)) {
            outError = "\"" + path + "\": unrecognised line \"" + line + "\" (expected a keyword or three floats).";
            return false;
        }
        values.push_back(r);
        values.push_back(g);
        values.push_back(b);
    }

    if (size == 0) {
        outError = "\"" + path + "\": no LUT_3D_SIZE line found.";
        return false;
    }
    for (int axis = 0; axis < 3; ++axis) {
        if (!std::isfinite(domainMin[axis]) || !std::isfinite(domainMax[axis]) || domainMin[axis] >= domainMax[axis]) {
            outError = "\"" + path + "\": DOMAIN_MIN/DOMAIN_MAX must be finite with min < max on every axis.";
            return false;
        }
    }

    const size_t expected = static_cast<size_t>(size) * size * size * 3;
    if (values.size() != expected) {
        outError = "\"" + path + "\": expected " + std::to_string(expected / 3) + " data lines for LUT_3D_SIZE " +
                    std::to_string(size) + ", found " + std::to_string(values.size() / 3) + ".";
        return false;
    }

    outData.size = size;
    outData.rgb = std::move(values);
    return true;
}

CubeLutData generateIdentityCubeLut(uint32_t size) {
    CubeLutData data;
    if (size < kMinLutSize) size = kMinLutSize;
    if (size > kMaxLutSize) size = kMaxLutSize;
    data.size = size;
    data.rgb.reserve(static_cast<size_t>(size) * size * size * 3);

    const float denom = static_cast<float>(size - 1);
    for (uint32_t b = 0; b < size; ++b) {
        for (uint32_t g = 0; g < size; ++g) {
            for (uint32_t r = 0; r < size; ++r) {
                data.rgb.push_back(static_cast<float>(r) / denom);
                data.rgb.push_back(static_cast<float>(g) / denom);
                data.rgb.push_back(static_cast<float>(b) / denom);
            }
        }
    }
    return data;
}

} // namespace engine::core
