#include "core/Mesh.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

#include "core/Logger.hpp"

namespace engine::core {

VkVertexInputBindingDescription Vertex::bindingDescription() {
    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(Vertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return binding;
}

std::vector<VkVertexInputAttributeDescription> Vertex::attributeDescriptions() {
    std::vector<VkVertexInputAttributeDescription> attrs(4);
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
    attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, tangent)};
    return attrs;
}

// Real per-triangle tangent accumulation (Lengyel's method: solve for the
// tangent/bitangent that reproduce each triangle's UV gradient from its
// edge vectors) + per-vertex Gram-Schmidt re-orthogonalization against the
// normal -- the same core algorithm MikkTSpace (the reference
// implementation Blender/glTF/most DCC tools use) is built on, applied
// here without MikkTSpace's additional per-face-corner splitting for
// hard UV seams (this engine has no imported-mesh UV-seam data yet to
// need that refinement for -- see AssetConverter's still-stubbed mesh
// path). Runs on every generated/imported mesh uniformly (called from
// uploadFromHost() below), not hand-derived per procedural primitive --
// one implementation to get right and test, not four/five. Declared in
// Mesh.hpp (not file-local) specifically so tests/test_main.cpp can
// exercise it directly against a hand-built triangle, the same reasoning
// CascadeSplitMath.hpp's extraction used.
void computeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> tan1(vertices.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> tan2(vertices.size(), glm::vec3(0.0f));

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i + 1], i2 = indices[i + 2];
        const Vertex& v0 = vertices[i0];
        const Vertex& v1 = vertices[i1];
        const Vertex& v2 = vertices[i2];

        glm::vec3 edge1 = v1.position - v0.position;
        glm::vec3 edge2 = v2.position - v0.position;
        glm::vec2 deltaUV1 = v1.uv - v0.uv;
        glm::vec2 deltaUV2 = v2.uv - v0.uv;

        float denom = deltaUV1.x * deltaUV2.y - deltaUV2.x * deltaUV1.y;
        if (std::fabs(denom) < 1e-8f) continue; // degenerate UV triangle (e.g. a UV seam pinch) -- contributes nothing rather than dividing by ~0

        float r = 1.0f / denom;
        glm::vec3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * r;
        glm::vec3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * r;

        tan1[i0] += tangent;
        tan1[i1] += tangent;
        tan1[i2] += tangent;
        tan2[i0] += bitangent;
        tan2[i1] += bitangent;
        tan2[i2] += bitangent;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3& n = vertices[i].normal;
        const glm::vec3& t = tan1[i];

        glm::vec3 orthoTangent = t - n * glm::dot(n, t);
        float len = glm::length(orthoTangent);

        glm::vec3 tangent;
        if (len > 1e-8f) {
            tangent = orthoTangent / len;
        } else {
            // No triangle contributed a usable tangent at this vertex
            // (isolated vertex, or every adjacent triangle had a
            // degenerate UV gradient) -- fall back to *some* vector
            // perpendicular to the normal rather than an arbitrary axis
            // that might be parallel to it.
            glm::vec3 up = std::fabs(n.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
            tangent = glm::normalize(glm::cross(up, n));
        }

        float handedness = (glm::dot(glm::cross(n, t), tan2[i]) < 0.0f) ? -1.0f : 1.0f;
        vertices[i].tangent = glm::vec4(tangent, handedness);
    }
}

namespace {

bool createBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memUsage,
                   VmaAllocationCreateFlags flags, VkBuffer& outBuffer, VmaAllocation& outAllocation) {
    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = memUsage;
    allocInfo.flags = flags;

    return vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &outBuffer, &outAllocation, nullptr) == VK_SUCCESS;
}

// One-shot upload: staging (host-visible) -> device-local, via a single
// primary command buffer submitted and waited on synchronously. Not
// pipelined against anything else -- fine for the small number of
// procedural meshes this skeleton creates at startup; a real asset
// pipeline would batch uploads and not stall the queue per-mesh.
bool uploadToDeviceLocalBuffer(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                const void* data, VkDeviceSize size, VkBufferUsageFlags usage,
                                VkBuffer& outBuffer, VmaAllocation& outAllocation) {
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    if (!createBuffer(allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                       VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
                       stagingBuffer, stagingAllocation)) {
        return false;
    }

    VmaAllocationInfo stagingInfo{};
    vmaGetAllocationInfo(allocator, stagingAllocation, &stagingInfo);
    std::memcpy(stagingInfo.pMappedData, data, static_cast<size_t>(size));

    if (!createBuffer(allocator, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | usage, VMA_MEMORY_USAGE_AUTO, 0,
                       outBuffer, outAllocation)) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = cmdPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    // Kronos (Phase 1 stability audit fix): real result check -- under
    // command-pool exhaustion this used to leave `cmd` at VK_NULL_HANDLE
    // and fall straight into vkBeginCommandBuffer(cmd, ...) on it, real
    // undefined behavior. Real cleanup of everything already allocated
    // above before bailing out.
    if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &cmd) != VK_SUCCESS) {
        vmaDestroyBuffer(allocator, outBuffer, outAllocation);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkBufferCopy copyRegion{0, 0, size};
    vkCmdCopyBuffer(cmd, stagingBuffer, outBuffer, 1, &copyRegion);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cmd);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
    return true;
}

} // namespace

bool Mesh::uploadFromHost(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                           const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    // Real tangents for every mesh that reaches the GPU, computed once
    // here rather than by each caller -- see computeTangents()'s comment.
    // A local mutable copy since `vertices` comes in const (every
    // procedural generator constructs its vertex list without bothering
    // to fill in a real tangent -- see Vertex::tangent's default member
    // initializer).
    std::vector<Vertex> tangentSpaceVertices = vertices;
    computeTangents(tangentSpaceVertices, indices);

    localBoundsMin_ = glm::vec3(std::numeric_limits<float>::max());
    localBoundsMax_ = glm::vec3(std::numeric_limits<float>::lowest());
    for (const Vertex& v : tangentSpaceVertices) {
        localBoundsMin_ = glm::min(localBoundsMin_, v.position);
        localBoundsMax_ = glm::max(localBoundsMax_, v.position);
    }
    if (tangentSpaceVertices.empty()) {
        localBoundsMin_ = glm::vec3(0.0f);
        localBoundsMax_ = glm::vec3(0.0f);
    }

    VkDeviceSize vertexBytes = sizeof(Vertex) * tangentSpaceVertices.size();
    VkDeviceSize indexBytes = sizeof(uint32_t) * indices.size();

    if (!uploadToDeviceLocalBuffer(allocator, device, cmdPool, queue, tangentSpaceVertices.data(), vertexBytes,
                                    VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, vertexBuffer_, vertexAllocation_)) {
        logError("Mesh", "vertex buffer upload failed.");
        return false;
    }
    if (!uploadToDeviceLocalBuffer(allocator, device, cmdPool, queue, indices.data(), indexBytes,
                                    VK_BUFFER_USAGE_INDEX_BUFFER_BIT, indexBuffer_, indexAllocation_)) {
        logError("Mesh", "index buffer upload failed.");
        vmaDestroyBuffer(allocator, vertexBuffer_, vertexAllocation_);
        return false;
    }

    indexCount_ = static_cast<uint32_t>(indices.size());
    return true;
}

void Mesh::destroy(VmaAllocator allocator) {
    if (vertexBuffer_) vmaDestroyBuffer(allocator, vertexBuffer_, vertexAllocation_);
    if (indexBuffer_) vmaDestroyBuffer(allocator, indexBuffer_, indexAllocation_);
    vertexBuffer_ = VK_NULL_HANDLE;
    indexBuffer_ = VK_NULL_HANDLE;
    indexCount_ = 0;
}

Mesh Mesh::createBox(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue, glm::vec3 h) {
    // 24 vertices (4 per face x 6 faces) so each face gets flat, correct
    // normals instead of the smoothed-corner look shared-vertex cubes get.
    std::vector<Vertex> vertices = {
        // +X
        {{h.x, -h.y, -h.z}, {1, 0, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {1, 0, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {1, 0, 0}, {1, 1}}, {{h.x, h.y, -h.z}, {1, 0, 0}, {0, 1}},
        // -X
        {{-h.x, -h.y, h.z}, {-1, 0, 0}, {0, 0}}, {{-h.x, -h.y, -h.z}, {-1, 0, 0}, {1, 0}},
        {{-h.x, h.y, -h.z}, {-1, 0, 0}, {1, 1}}, {{-h.x, h.y, h.z}, {-1, 0, 0}, {0, 1}},
        // +Y
        {{-h.x, h.y, -h.z}, {0, 1, 0}, {0, 0}}, {{h.x, h.y, -h.z}, {0, 1, 0}, {1, 0}},
        {{h.x, h.y, h.z}, {0, 1, 0}, {1, 1}}, {{-h.x, h.y, h.z}, {0, 1, 0}, {0, 1}},
        // -Y
        {{-h.x, -h.y, h.z}, {0, -1, 0}, {0, 0}}, {{h.x, -h.y, h.z}, {0, -1, 0}, {1, 0}},
        {{h.x, -h.y, -h.z}, {0, -1, 0}, {1, 1}}, {{-h.x, -h.y, -h.z}, {0, -1, 0}, {0, 1}},
        // +Z
        {{h.x, -h.y, h.z}, {0, 0, 1}, {0, 0}}, {{-h.x, -h.y, h.z}, {0, 0, 1}, {1, 0}},
        {{-h.x, h.y, h.z}, {0, 0, 1}, {1, 1}}, {{h.x, h.y, h.z}, {0, 0, 1}, {0, 1}},
        // -Z
        {{-h.x, -h.y, -h.z}, {0, 0, -1}, {0, 0}}, {{h.x, -h.y, -h.z}, {0, 0, -1}, {1, 0}},
        {{h.x, h.y, -h.z}, {0, 0, -1}, {1, 1}}, {{-h.x, h.y, -h.z}, {0, 0, -1}, {0, 1}},
    };

    std::vector<uint32_t> indices;
    indices.reserve(36);
    for (uint32_t face = 0; face < 6; ++face) {
        uint32_t base = face * 4;
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }

    Mesh mesh;
    (void)mesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices);
    return mesh;
}

Mesh Mesh::createPlane(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                        float halfWidth, float halfDepth) {
    std::vector<Vertex> vertices = {
        {{-halfWidth, 0, -halfDepth}, {0, 1, 0}, {0, 0}},
        {{halfWidth, 0, -halfDepth}, {0, 1, 0}, {1, 0}},
        {{halfWidth, 0, halfDepth}, {0, 1, 0}, {1, 1}},
        {{-halfWidth, 0, halfDepth}, {0, 1, 0}, {0, 1}},
    };
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    Mesh mesh;
    (void)mesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices);
    return mesh;
}

Mesh Mesh::createQuad(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue, float halfSize) {
    std::vector<Vertex> vertices = {
        {{-halfSize, -halfSize, 0.0f}, {0, 0, 1}, {0, 0}},
        {{halfSize, -halfSize, 0.0f}, {0, 0, 1}, {1, 0}},
        {{halfSize, halfSize, 0.0f}, {0, 0, 1}, {1, 1}},
        {{-halfSize, halfSize, 0.0f}, {0, 0, 1}, {0, 1}},
    };
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};

    Mesh mesh;
    (void)mesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices);
    return mesh;
}

Mesh Mesh::createCapsule(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue, float radius,
                          float halfHeight, uint32_t radialSegments, uint32_t capRings) {
    radialSegments = std::max(3u, radialSegments);
    capRings = std::max(1u, capRings);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    const uint32_t verticesPerRing = radialSegments + 1; // last vertex duplicates the first, for a clean UV seam
    const float twoPi = 6.28318530718f;
    const float halfPi = 1.57079632679f;

    // ringCenterY/ringSign together place each ring on the correct
    // hemisphere: +halfHeight/+1 for the top cap, -halfHeight/-1 for the
    // bottom. `t` sweeps 0 (pole) -> 1 (equator) for both, so the two
    // loops are identical apart from that sign -- this is what guarantees
    // the equator rings (t=1) land at exactly halfHeight with exactly
    // horizontal normals, matching the cylinder wall with no seam.
    auto emitHemisphere = [&](float ringCenterY, float ringSign) {
        for (uint32_t r = 0; r <= capRings; ++r) {
            float t = static_cast<float>(r) / static_cast<float>(capRings); // 0 at pole, 1 at equator
            float phi = halfPi * (1.0f - t);
            float y = ringCenterY + ringSign * radius * std::sin(phi);
            float ringRadius = radius * std::cos(phi);

            for (uint32_t s = 0; s <= radialSegments; ++s) {
                float theta = twoPi * static_cast<float>(s) / static_cast<float>(radialSegments);
                float cosT = std::cos(theta), sinT = std::sin(theta);
                glm::vec3 pos(ringRadius * cosT, y, ringRadius * sinT);
                glm::vec3 normal(std::cos(phi) * cosT, ringSign * std::sin(phi), std::cos(phi) * sinT);
                glm::vec2 uv(static_cast<float>(s) / static_cast<float>(radialSegments), t);
                vertices.push_back({pos, normal, uv});
            }
        }
    };

    // Top hemisphere: pole first (r=0) down to the equator (r=capRings),
    // which becomes the cylinder's top ring.
    emitHemisphere(halfHeight, 1.0f);
    // Bottom hemisphere generated pole-first too, then reversed below so
    // its own equator ring comes *first* -- connecting directly to the
    // top hemisphere's equator ring is what forms the cylindrical body,
    // with no separate cylinder-wall vertices needed (see header comment).
    size_t bottomStart = vertices.size();
    emitHemisphere(-halfHeight, -1.0f);
    size_t bottomRingCount = capRings + 1;
    for (size_t half = 0; half < bottomRingCount / 2; ++half) {
        size_t ringA = bottomStart + half * verticesPerRing;
        size_t ringB = bottomStart + (bottomRingCount - 1 - half) * verticesPerRing;
        for (uint32_t s = 0; s < verticesPerRing; ++s) {
            std::swap(vertices[ringA + s], vertices[ringB + s]);
        }
    }

    uint32_t totalRings = 2 * (capRings + 1);
    for (uint32_t r = 0; r + 1 < totalRings; ++r) {
        uint32_t base = r * verticesPerRing;
        uint32_t nextBase = (r + 1) * verticesPerRing;
        for (uint32_t s = 0; s < radialSegments; ++s) {
            indices.insert(indices.end(), {base + s, base + s + 1, nextBase + s,
                                            base + s + 1, nextBase + s + 1, nextBase + s});
        }
    }

    Mesh mesh;
    (void)mesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices);
    return mesh;
}

Mesh Mesh::createCylinder(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue, float radius,
                           float halfHeight, uint32_t radialSegments) {
    radialSegments = std::max(3u, radialSegments);

    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    const float twoPi = 6.28318530718f;

    // Side wall: top ring then bottom ring, radial normals, same
    // "verticesPerRing = segments+1 for a clean UV seam" convention
    // createCapsule() already uses.
    uint32_t topWallStart = 0;
    for (uint32_t s = 0; s <= radialSegments; ++s) {
        float theta = twoPi * static_cast<float>(s) / static_cast<float>(radialSegments);
        float cosT = std::cos(theta), sinT = std::sin(theta);
        float u = static_cast<float>(s) / static_cast<float>(radialSegments);
        vertices.push_back({{radius * cosT, halfHeight, radius * sinT}, {cosT, 0.0f, sinT}, {u, 0.0f}});
    }
    uint32_t bottomWallStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t s = 0; s <= radialSegments; ++s) {
        float theta = twoPi * static_cast<float>(s) / static_cast<float>(radialSegments);
        float cosT = std::cos(theta), sinT = std::sin(theta);
        float u = static_cast<float>(s) / static_cast<float>(radialSegments);
        vertices.push_back({{radius * cosT, -halfHeight, radius * sinT}, {cosT, 0.0f, sinT}, {u, 1.0f}});
    }
    for (uint32_t s = 0; s < radialSegments; ++s) {
        uint32_t top0 = topWallStart + s, top1 = topWallStart + s + 1;
        uint32_t bot0 = bottomWallStart + s, bot1 = bottomWallStart + s + 1;
        indices.insert(indices.end(), {top0, top1, bot0, top1, bot1, bot0});
    }

    // Top cap: fan from a center vertex, +Y normal. Increasing theta
    // (cos,sin sweep +X toward +Z) matches createBox()'s own +Y face
    // winding (verified against its vertex order directly).
    uint32_t topCapCenter = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, halfHeight, 0.0f}, {0, 1, 0}, {0.5f, 0.5f}});
    uint32_t topCapRimStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t s = 0; s <= radialSegments; ++s) {
        float theta = twoPi * static_cast<float>(s) / static_cast<float>(radialSegments);
        float cosT = std::cos(theta), sinT = std::sin(theta);
        vertices.push_back(
            {{radius * cosT, halfHeight, radius * sinT}, {0, 1, 0}, {0.5f + 0.5f * cosT, 0.5f + 0.5f * sinT}});
    }
    for (uint32_t s = 0; s < radialSegments; ++s) {
        indices.insert(indices.end(), {topCapCenter, topCapRimStart + s, topCapRimStart + s + 1});
    }

    // Bottom cap: same fan, wound the opposite way (decreasing theta) for
    // the -Y normal.
    uint32_t bottomCapCenter = static_cast<uint32_t>(vertices.size());
    vertices.push_back({{0.0f, -halfHeight, 0.0f}, {0, -1, 0}, {0.5f, 0.5f}});
    uint32_t bottomCapRimStart = static_cast<uint32_t>(vertices.size());
    for (uint32_t s = 0; s <= radialSegments; ++s) {
        float theta = twoPi * static_cast<float>(s) / static_cast<float>(radialSegments);
        float cosT = std::cos(theta), sinT = std::sin(theta);
        vertices.push_back(
            {{radius * cosT, -halfHeight, radius * sinT}, {0, -1, 0}, {0.5f + 0.5f * cosT, 0.5f - 0.5f * sinT}});
    }
    for (uint32_t s = 0; s < radialSegments; ++s) {
        indices.insert(indices.end(), {bottomCapCenter, bottomCapRimStart + s + 1, bottomCapRimStart + s});
    }

    Mesh mesh;
    (void)mesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices);
    return mesh;
}

Mesh Mesh::createWedge(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                        glm::vec3 h) {
    // A box whose +Z face collapses to a single bottom edge -- back face
    // (-Z) stays full height, front face has no vertical extent at all,
    // leaving one flat sloped face connecting the back-top edge to the
    // front-bottom edge. Every face below is copied from createBox()'s
    // own vertex order/winding for the faces that are unchanged (bottom,
    // back), with the two side faces reduced from a quad to a triangle
    // (front-top vertex doesn't exist) and the slope face computed fresh
    // (cross-product-verified normal direction, see inline notes).
    std::vector<Vertex> vertices = {
        // Bottom (-Y), full rectangle -- same vertex order as createBox()'s
        // own -Y face.
        {{-h.x, -h.y, h.z}, {0, -1, 0}, {0, 0}},
        {{h.x, -h.y, h.z}, {0, -1, 0}, {1, 0}},
        {{h.x, -h.y, -h.z}, {0, -1, 0}, {1, 1}},
        {{-h.x, -h.y, -h.z}, {0, -1, 0}, {0, 1}},
        // Back (-Z), full rectangle, vertical -- same vertex order as
        // createBox()'s own -Z face.
        {{-h.x, -h.y, -h.z}, {0, 0, -1}, {0, 0}},
        {{h.x, -h.y, -h.z}, {0, 0, -1}, {1, 0}},
        {{h.x, h.y, -h.z}, {0, 0, -1}, {1, 1}},
        {{-h.x, h.y, -h.z}, {0, 0, -1}, {0, 1}},
        // Left (-X), triangle: bottom-front, bottom-back, top-back (no
        // top-front vertex -- collapsed).
        {{-h.x, -h.y, h.z}, {-1, 0, 0}, {0, 0}},
        {{-h.x, -h.y, -h.z}, {-1, 0, 0}, {1, 0}},
        {{-h.x, h.y, -h.z}, {-1, 0, 0}, {1, 1}},
        // Right (+X), triangle: bottom-back, top-back, bottom-front.
        {{h.x, -h.y, -h.z}, {1, 0, 0}, {0, 0}},
        {{h.x, h.y, -h.z}, {1, 0, 0}, {1, 0}},
        {{h.x, -h.y, h.z}, {1, 0, 0}, {1, 1}},
        // Slope, normal (0, h.z, h.y) normalized -- verified via
        // cross(bottom-front-left - top-back-left, bottom-front-right -
        // top-back-left), see this function's own derivation.
        {{-h.x, h.y, -h.z}, glm::normalize(glm::vec3(0.0f, h.z, h.y)), {0, 0}},
        {{-h.x, -h.y, h.z}, glm::normalize(glm::vec3(0.0f, h.z, h.y)), {0, 1}},
        {{h.x, -h.y, h.z}, glm::normalize(glm::vec3(0.0f, h.z, h.y)), {1, 1}},
        {{h.x, h.y, -h.z}, glm::normalize(glm::vec3(0.0f, h.z, h.y)), {1, 0}},
    };

    std::vector<uint32_t> indices = {
        0, 1, 2, 0, 2, 3,    // bottom
        4, 5, 6, 4, 6, 7,    // back
        8, 9, 10,            // left (single triangle)
        11, 12, 13,          // right (single triangle)
        14, 15, 16, 14, 16, 17, // slope
    };

    Mesh mesh;
    (void)mesh.uploadFromHost(allocator, device, cmdPool, queue, vertices, indices);
    return mesh;
}

uint32_t MeshLibrary::registerMesh(Mesh mesh) {
    meshes_.push_back(std::move(mesh));
    return static_cast<uint32_t>(meshes_.size() - 1);
}

const Mesh* MeshLibrary::get(uint32_t handle) const {
    if (handle >= meshes_.size()) return nullptr;
    return &meshes_[handle];
}

void MeshLibrary::replaceMesh(uint32_t handle, Mesh newMesh, VmaAllocator allocator) {
    if (handle >= meshes_.size()) return;
    meshes_[handle].destroy(allocator);
    meshes_[handle] = std::move(newMesh);
}

void MeshLibrary::destroyAll(VmaAllocator allocator) {
    for (auto& mesh : meshes_) {
        mesh.destroy(allocator);
    }
    meshes_.clear();
}

} // namespace engine::core
