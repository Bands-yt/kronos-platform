#include "core/ModifierStack.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <utility>

namespace engine::core {

namespace {

EditableMesh applyMirror(const EditableMesh& mesh, const MirrorModifierParams& params) {
    const std::vector<Vertex>& srcVerts = mesh.vertices();
    const std::vector<uint32_t>& srcIndices = mesh.indices();

    std::vector<Vertex> vertices(srcVerts);
    std::vector<uint32_t> indices(srcIndices);

    const uint32_t base = static_cast<uint32_t>(vertices.size());
    const int axis = std::clamp(params.axis, 0, 2);
    for (const Vertex& v : srcVerts) {
        Vertex mirrored = v;
        mirrored.position[axis] = -mirrored.position[axis];
        mirrored.normal[axis] = -mirrored.normal[axis];
        vertices.push_back(mirrored);
    }
    // Reversed winding (a, c, b instead of a, b, c) -- mirroring one axis
    // flips handedness, so keeping the original winding order would make
    // every mirrored triangle face inward.
    for (size_t i = 0; i + 2 < srcIndices.size(); i += 3) {
        indices.push_back(base + srcIndices[i]);
        indices.push_back(base + srcIndices[i + 2]);
        indices.push_back(base + srcIndices[i + 1]);
    }

    EditableMesh result = EditableMesh::fromVertexData(std::move(vertices), std::move(indices));
    if (params.mergeAtCenter) result.mergeVertices(params.mergeThreshold);
    return result;
}

EditableMesh applyArray(const EditableMesh& mesh, const ArrayModifierParams& params) {
    const std::vector<Vertex>& srcVerts = mesh.vertices();
    const std::vector<uint32_t>& srcIndices = mesh.indices();
    const int count = std::max(params.count, 1);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(srcVerts.size() * static_cast<size_t>(count));
    indices.reserve(srcIndices.size() * static_cast<size_t>(count));

    for (int copy = 0; copy < count; ++copy) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        const glm::vec3 offset = params.offset * static_cast<float>(copy);
        for (const Vertex& v : srcVerts) {
            Vertex moved = v;
            moved.position += offset;
            vertices.push_back(moved);
        }
        for (uint32_t idx : srcIndices) indices.push_back(base + idx);
    }
    return EditableMesh::fromVertexData(std::move(vertices), std::move(indices));
}

EditableMesh applySolidify(const EditableMesh& mesh, const SolidifyModifierParams& params) {
    const std::vector<Vertex>& srcVerts = mesh.vertices();
    const std::vector<uint32_t>& srcIndices = mesh.indices();
    const size_t faceCount = mesh.faceCount();

    // Real boundary detection: an edge used by exactly 1 face gets a real
    // connecting wall below; an edge shared by 2 (the normal interior
    // case) needs none -- the offset inner shell already seals it.
    std::map<std::pair<uint32_t, uint32_t>, int> edgeFaceCount;
    for (size_t f = 0; f < faceCount; ++f) {
        std::array<uint32_t, 3> verts = mesh.faceVertexIndices(f);
        for (int i = 0; i < 3; ++i) {
            uint32_t a = verts[static_cast<size_t>(i)];
            uint32_t b = verts[static_cast<size_t>((i + 1) % 3)];
            ++edgeFaceCount[{std::min(a, b), std::max(a, b)}];
        }
    }

    std::vector<Vertex> vertices(srcVerts);
    std::vector<uint32_t> indices(srcIndices);
    const uint32_t innerBase = static_cast<uint32_t>(vertices.size());

    for (size_t f = 0; f < faceCount; ++f) {
        std::array<uint32_t, 3> verts = mesh.faceVertexIndices(f);
        const glm::vec3 offset = mesh.faceNormal(f) * -params.thickness;
        for (int i = 0; i < 3; ++i) {
            Vertex inner = srcVerts[verts[static_cast<size_t>(i)]];
            inner.position += offset;
            inner.normal = -inner.normal;
            vertices.push_back(inner);
        }
        const uint32_t ia = innerBase + static_cast<uint32_t>(f) * 3 + 0;
        const uint32_t ib = innerBase + static_cast<uint32_t>(f) * 3 + 1;
        const uint32_t ic = innerBase + static_cast<uint32_t>(f) * 3 + 2;
        // Reversed winding for the inner shell (faces inward).
        indices.push_back(ia);
        indices.push_back(ic);
        indices.push_back(ib);
    }

    // Real boundary walls: for every edge used by exactly 1 face, connect
    // that edge's 2 original vertices to their own offset counterparts
    // (looked up via the one face that owns this boundary edge -- this
    // mesh's per-face vertex storage means that face's own inner-shell
    // triple, appended above, is the only copy that edge has).
    for (size_t f = 0; f < faceCount; ++f) {
        std::array<uint32_t, 3> verts = mesh.faceVertexIndices(f);
        for (int i = 0; i < 3; ++i) {
            uint32_t a = verts[static_cast<size_t>(i)];
            uint32_t b = verts[static_cast<size_t>((i + 1) % 3)];
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            if (edgeFaceCount[key] != 1) continue; // interior edge, no wall needed

            const uint32_t innerA = innerBase + static_cast<uint32_t>(f) * 3 + static_cast<uint32_t>(i);
            const uint32_t innerB = innerBase + static_cast<uint32_t>(f) * 3 + static_cast<uint32_t>((i + 1) % 3);
            // Quad (a, b, innerB, innerA) as 2 triangles, wound to face
            // outward, consistent with the original face's own winding.
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(innerB);
            indices.push_back(a);
            indices.push_back(innerB);
            indices.push_back(innerA);
        }
    }

    return EditableMesh::fromVertexData(std::move(vertices), std::move(indices));
}

EditableMesh applySubdivision(const EditableMesh& mesh, const SubdivisionModifierParams& params) {
    EditableMesh result = mesh;
    for (int level = 0; level < params.levels; ++level) {
        // subdivideFace() replaces the target face IN PLACE and appends
        // its other 3 new faces at the END of the list (see its own
        // implementation comment: "Face count: +3") -- capturing
        // faceCount() once, before this level's loop, and iterating that
        // fixed range still reaches every one of this level's starting
        // faces exactly once as the mesh grows underneath the loop.
        const size_t facesBefore = result.faceCount();
        for (size_t f = 0; f < facesBefore; ++f) result.subdivideFace(f);
    }
    return result;
}

} // namespace

void ModifierStack::removeModifier(size_t index) {
    if (index < modifiers_.size()) modifiers_.erase(modifiers_.begin() + static_cast<long>(index));
}

void ModifierStack::swapModifiers(size_t a, size_t b) {
    if (a < modifiers_.size() && b < modifiers_.size()) std::swap(modifiers_[a], modifiers_[b]);
}

EditableMesh ModifierStack::evaluate(const EditableMesh& baseMesh) const {
    EditableMesh result = baseMesh;
    for (const Modifier& modifier : modifiers_) {
        if (!modifier.enabled) continue;
        switch (modifier.type) {
            case ModifierType::Mirror: result = applyMirror(result, modifier.mirror); break;
            case ModifierType::Array: result = applyArray(result, modifier.array); break;
            case ModifierType::Solidify: result = applySolidify(result, modifier.solidify); break;
            case ModifierType::Subdivision: result = applySubdivision(result, modifier.subdivision); break;
            case ModifierType::Boolean: {
                EditableMesh operand = EditableMesh::createBox(modifier.boolean.boxHalfExtents, modifier.boolean.boxOffset);
                result = booleanOp(result, operand, modifier.boolean.operation);
                break;
            }
        }
    }
    return result;
}

} // namespace engine::core
