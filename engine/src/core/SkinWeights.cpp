#include "core/SkinWeights.hpp"

#include <cmath>

namespace engine::core {

void SkinWeights::normalizeWeights() {
    for (auto& vertex : perVertex) {
        float sum = vertex.weights[0] + vertex.weights[1] + vertex.weights[2] + vertex.weights[3];
        if (sum <= 0.0f) continue; // all-zero (or negative, shouldn't happen) -- nothing sane to rescale to
        if (std::fabs(sum - 1.0f) < 1e-4f) continue; // already normalized
        for (float& weight : vertex.weights) weight /= sum;
    }
}

bool SkinWeights::validate(size_t expectedVertexCount, int jointCount, std::string& outError) const {
    if (perVertex.size() != expectedVertexCount) {
        outError = "SkinWeights has " + std::to_string(perVertex.size()) + " vertices but the mesh has " +
                    std::to_string(expectedVertexCount);
        return false;
    }

    for (size_t v = 0; v < perVertex.size(); ++v) {
        const VertexSkinWeights& vw = perVertex[v];
        float sum = 0.0f;
        for (int k = 0; k < 4; ++k) {
            int jointIndex = vw.jointIndices[static_cast<size_t>(k)];
            float weight = vw.weights[static_cast<size_t>(k)];

            if (jointIndex == -1) continue; // unused slot -- its weight is expected to be 0, not itself checked

            if (jointIndex < 0 || jointIndex >= jointCount) {
                outError = "vertex " + std::to_string(v) + " references joint index " + std::to_string(jointIndex) +
                            ", but the skeleton only has " + std::to_string(jointCount) + " joints";
                return false;
            }
            if (!std::isfinite(weight) || weight < 0.0f) {
                outError = "vertex " + std::to_string(v) + " has an invalid weight (" + std::to_string(weight) +
                            ") for joint index " + std::to_string(jointIndex);
                return false;
            }
            sum += weight;
        }

        if (sum <= 0.0f) {
            outError = "vertex " + std::to_string(v) + " has zero total weight -- no joint influences it";
            return false;
        }
    }
    return true;
}

} // namespace engine::core
