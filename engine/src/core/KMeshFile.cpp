#include "core/KMeshFile.hpp"

#include <fstream>
#include <sstream>

namespace engine::core {

bool saveKMesh(const std::string& path, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "KMESH 1\n";
    out << "VERTEXCOUNT " << vertices.size() << "\n";
    for (const Vertex& v : vertices) {
        out << "V " << v.position.x << " " << v.position.y << " " << v.position.z << " " << v.normal.x << " "
            << v.normal.y << " " << v.normal.z << " " << v.uv.x << " " << v.uv.y << "\n";
    }
    out << "INDEXCOUNT " << indices.size() << "\n";
    for (size_t f = 0; f * 3 < indices.size(); ++f) {
        out << "F " << indices[f * 3] << " " << indices[f * 3 + 1] << " " << indices[f * 3 + 2] << "\n";
    }
    out << "END\n";
    return out.good();
}

KMeshLoadResult loadKMesh(const std::string& path) {
    KMeshLoadResult result;
    std::ifstream in(path);
    if (!in.is_open()) {
        result.error = "Could not open " + path;
        return result;
    }

    std::string header;
    if (!std::getline(in, header) || header.rfind("KMESH", 0) != 0) {
        result.error = path + " is not a real .kmesh file (missing KMESH header)";
        return result;
    }

    std::string line;
    size_t expectedVertices = 0, expectedIndices = 0;
    while (std::getline(in, line)) {
        if (line == "END") {
            result.succeeded = true;
            return result;
        }
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        if (key == "VERTEXCOUNT") {
            iss >> expectedVertices;
            result.vertices.reserve(expectedVertices);
        } else if (key == "V") {
            Vertex v;
            iss >> v.position.x >> v.position.y >> v.position.z >> v.normal.x >> v.normal.y >> v.normal.z >>
                v.uv.x >> v.uv.y;
            if (!iss) {
                result.succeeded = false;
                result.error = "Malformed V line in " + path;
                return result;
            }
            result.vertices.push_back(v);
        } else if (key == "INDEXCOUNT") {
            iss >> expectedIndices;
            result.indices.reserve(expectedIndices);
        } else if (key == "F") {
            uint32_t a, b, c;
            iss >> a >> b >> c;
            if (!iss) {
                result.succeeded = false;
                result.error = "Malformed F line in " + path;
                return result;
            }
            result.indices.insert(result.indices.end(), {a, b, c});
        }
        // Unknown keys are skipped, not an error -- forward-compatible,
        // same convention PluginManifest::loadFromFile() already uses.
    }

    result.error = "Reached EOF in " + path + " without an END terminator";
    return result; // succeeded stays false -- malformed/truncated file
}

} // namespace engine::core
