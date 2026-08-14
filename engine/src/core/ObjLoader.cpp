#include "core/ObjLoader.hpp"

#include <fstream>
#include <sstream>
#include <unordered_map>

namespace engine::core {

namespace {

struct IndexKey {
    int v = 0;
    int vt = 0;
    int vn = 0;
    bool operator==(const IndexKey& other) const { return v == other.v && vt == other.vt && vn == other.vn; }
};

struct IndexKeyHash {
    size_t operator()(const IndexKey& key) const {
        size_t h = std::hash<int>()(key.v);
        h ^= std::hash<int>()(key.vt) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>()(key.vn) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// Resolves OBJ's 1-based (or negative/relative-to-end) index convention
// into a 0-based index, or -1 if the component wasn't present in this
// face token at all (e.g. "f 1//3", no vt). `count` is how many of that
// attribute have been parsed so far (relative indices count backward from
// there).
int resolveIndex(int rawIndex, size_t count) {
    if (rawIndex == 0) return -1; // not present
    if (rawIndex > 0) return rawIndex - 1;
    return static_cast<int>(count) + rawIndex; // negative: relative to the end
}

// Parses one "v[/vt][/vn]" face-vertex token. Returns false (and leaves
// `error` set) on anything that isn't a valid integer where one's
// expected -- this is untrusted file input, never allowed to throw an
// uncaught exception out of loadObj().
bool parseFaceToken(const std::string& token, size_t vCount, size_t vtCount, size_t vnCount, int& outV, int& outVt,
                     int& outVn, std::string& error) {
    outV = outVt = outVn = 0;
    size_t firstSlash = token.find('/');
    auto parseInt = [&](const std::string& s, int& out) -> bool {
        if (s.empty()) return true; // empty component (e.g. the middle of "1//2") -- leave at 0 ("not present")
        try {
            size_t consumed = 0;
            out = std::stoi(s, &consumed);
            return consumed == s.size();
        } catch (const std::exception&) {
            return false;
        }
    };

    int rawV = 0, rawVt = 0, rawVn = 0;
    bool ok;
    if (firstSlash == std::string::npos) {
        ok = parseInt(token, rawV);
    } else {
        ok = parseInt(token.substr(0, firstSlash), rawV);
        size_t secondSlash = token.find('/', firstSlash + 1);
        if (secondSlash == std::string::npos) {
            ok = ok && parseInt(token.substr(firstSlash + 1), rawVt);
        } else {
            ok = ok && parseInt(token.substr(firstSlash + 1, secondSlash - firstSlash - 1), rawVt);
            ok = ok && parseInt(token.substr(secondSlash + 1), rawVn);
        }
    }
    if (!ok || rawV == 0) {
        error = "malformed face token \"" + token + "\"";
        return false;
    }

    outV = resolveIndex(rawV, vCount);
    outVt = rawVt != 0 ? resolveIndex(rawVt, vtCount) : -1;
    outVn = rawVn != 0 ? resolveIndex(rawVn, vnCount) : -1;
    return true;
}

} // namespace

ObjLoadResult loadObj(const std::string& path) {
    ObjLoadResult result;

    std::ifstream in(path);
    if (!in.is_open()) {
        result.error = "could not open file";
        return result;
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec2> texcoords;
    std::vector<glm::vec3> normals;
    std::unordered_map<IndexKey, uint32_t, IndexKeyHash> combinedIndex;

    std::string line;
    size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        std::istringstream iss(line);
        std::string tag;
        iss >> tag;

        if (tag == "v") {
            glm::vec3 p(0.0f);
            if (!(iss >> p.x >> p.y >> p.z)) {
                result.error = "malformed vertex at line " + std::to_string(lineNumber);
                return result;
            }
            positions.push_back(p);
        } else if (tag == "vt") {
            glm::vec2 uv(0.0f);
            if (!(iss >> uv.x >> uv.y)) {
                result.error = "malformed texture coordinate at line " + std::to_string(lineNumber);
                return result;
            }
            texcoords.push_back(uv);
        } else if (tag == "vn") {
            glm::vec3 n(0.0f, 1.0f, 0.0f);
            if (!(iss >> n.x >> n.y >> n.z)) {
                result.error = "malformed normal at line " + std::to_string(lineNumber);
                return result;
            }
            float len = glm::length(n);
            normals.push_back(len > 1e-8f ? n / len : glm::vec3(0.0f, 1.0f, 0.0f));
        } else if (tag == "f") {
            std::vector<uint32_t> faceVertexIndices;
            std::string token;
            while (iss >> token) {
                int vi = 0, vti = 0, vni = 0;
                if (!parseFaceToken(token, positions.size(), texcoords.size(), normals.size(), vi, vti, vni,
                                     result.error)) {
                    result.error += " at line " + std::to_string(lineNumber);
                    return result;
                }
                if (vi < 0 || static_cast<size_t>(vi) >= positions.size()) {
                    result.error = "face references out-of-range vertex at line " + std::to_string(lineNumber);
                    return result;
                }

                IndexKey key{vi, vti, vni};
                auto it = combinedIndex.find(key);
                if (it != combinedIndex.end()) {
                    faceVertexIndices.push_back(it->second);
                    continue;
                }

                Vertex vertex{};
                vertex.position = positions[static_cast<size_t>(vi)];
                if (vti >= 0 && static_cast<size_t>(vti) < texcoords.size()) vertex.uv = texcoords[static_cast<size_t>(vti)];
                if (vni >= 0 && static_cast<size_t>(vni) < normals.size()) vertex.normal = normals[static_cast<size_t>(vni)];
                // else: no vn for this face -- left at Vertex's default
                // normal (0,0,1-ish via the aggregate default, overwritten
                // by the flat-normal pass below if the file has no vn
                // data anywhere).

                uint32_t newIndex = static_cast<uint32_t>(result.vertices.size());
                result.vertices.push_back(vertex);
                combinedIndex.emplace(key, newIndex);
                faceVertexIndices.push_back(newIndex);
            }

            // Fan triangulation from the face's first vertex -- correct
            // for the convex polygons real exporters emit; a concave
            // n-gon would triangulate incorrectly, a known, stated
            // limitation of fan triangulation in general, not specific to
            // this parser.
            for (size_t i = 1; i + 1 < faceVertexIndices.size(); ++i) {
                result.indices.push_back(faceVertexIndices[0]);
                result.indices.push_back(faceVertexIndices[i]);
                result.indices.push_back(faceVertexIndices[i + 1]);
            }
        }
        // Everything else (comments, o/g/s grouping, mtllib/usemtl,
        // blank lines, vp parameter-space vertices) is deliberately
        // ignored -- see this file's header comment on scope.
    }

    if (result.vertices.empty() || result.indices.empty()) {
        result.error = "no usable triangle geometry found";
        return result;
    }

    // Real flat face normals for files with no vn data at all, computed
    // the same per-triangle-accumulate way Mesh::createBox's flat-shaded
    // faces already work -- not a fallback placeholder value.
    if (normals.empty()) {
        std::vector<glm::vec3> accumulated(result.vertices.size(), glm::vec3(0.0f));
        for (size_t i = 0; i + 2 < result.indices.size(); i += 3) {
            uint32_t i0 = result.indices[i];
            uint32_t i1 = result.indices[i + 1];
            uint32_t i2 = result.indices[i + 2];
            // cross(edge2, edge1), not cross(edge1, edge2): this engine's
            // winding convention (verified against Mesh::createBox's own
            // hand-authored, known-correct face normals -- e.g. its +Y
            // face is wound v0,v1,v2 = (-x,+y,-z),(+x,+y,-z),(+x,+y,+z),
            // for which cross(v1-v0, v2-v0) works out to point -Y, the
            // *inward* direction) puts the outward normal along
            // cross(edge2, edge1) for a triangle's declared (v0,v1,v2)
            // order, not the more commonly-seen cross(edge1, edge2).
            // Gotten backward once already in this file's first version
            // -- caught by testComputeTangents()-adjacent test coverage
            // asserting a known flat triangle's fallback normal direction
            // (tests/test_main.cpp), not by inspection.
            glm::vec3 edge1 = result.vertices[i1].position - result.vertices[i0].position;
            glm::vec3 edge2 = result.vertices[i2].position - result.vertices[i0].position;
            glm::vec3 faceNormal = glm::cross(edge2, edge1);
            float len = glm::length(faceNormal);
            if (len > 1e-8f) faceNormal /= len;
            accumulated[i0] += faceNormal;
            accumulated[i1] += faceNormal;
            accumulated[i2] += faceNormal;
        }
        for (size_t i = 0; i < result.vertices.size(); ++i) {
            float len = glm::length(accumulated[i]);
            result.vertices[i].normal = len > 1e-8f ? accumulated[i] / len : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    result.succeeded = true;
    return result;
}

bool saveObj(const std::string& path, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "# Exported by Kronos Studio\n";
    for (const Vertex& v : vertices) {
        out << "v " << v.position.x << " " << v.position.y << " " << v.position.z << "\n";
    }
    for (const Vertex& v : vertices) {
        out << "vt " << v.uv.x << " " << v.uv.y << "\n";
    }
    for (const Vertex& v : vertices) {
        out << "vn " << v.normal.x << " " << v.normal.y << " " << v.normal.z << "\n";
    }
    // 1-indexed, v/vt/vn all sharing the same per-vertex index -- real
    // and correct for any mesh this engine produces (every Vertex
    // already carries its own position+uv+normal together, never
    // split), which is the common case loadObj() itself also produces.
    for (size_t f = 0; f * 3 < indices.size(); ++f) {
        uint32_t a = indices[f * 3] + 1, b = indices[f * 3 + 1] + 1, c = indices[f * 3 + 2] + 1;
        out << "f " << a << "/" << a << "/" << a << " " << b << "/" << b << "/" << b << " " << c << "/" << c << "/"
            << c << "\n";
    }
    return out.good();
}

} // namespace engine::core
