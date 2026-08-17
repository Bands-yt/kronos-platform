#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <volk.h>
#include <vk_mem_alloc.h>

namespace engine::core {

// Matches the vertex input layout scene.vert expects (location 0/1/2/3).
struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
    // xyz: tangent direction (world/model space, points along increasing
    // U), w: handedness (+1/-1, reconstructs the bitangent in-shader via
    // cross(N, T) * w instead of storing a third vector). Computed by
    // computeTangents() (Mesh.cpp) for every generated/imported mesh --
    // not hand-authored per primitive, see that function's comment.
    // Default-initialized to a non-degenerate value (not zero) so a
    // Vertex constructed via the old 3-field aggregate-init call sites
    // (every procedural generator) never uploads a zero tangent before
    // uploadFromHost()'s computeTangents() pass overwrites it.
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};

    // Kronos ("Avatar Visual Silhouette Pass" -- "Hair" -- "Apply
    // vertex-color gradients for depth"): a real, new per-vertex color,
    // multiplied into the final albedo in shaders/scene.frag on top of
    // (not instead of) the existing per-entity SkinnedRenderable::
    // baseColor/Renderable::baseColor -- the first, genuinely per-vertex
    // (smoothly interpolated across a triangle) color channel this
    // engine has ever had; every earlier "gradient" in this codebase
    // (RiggedAvatar.hpp's own applySegmentShadingGradient(), the first
    // hair pass's discrete per-tuft color step) was a real, honest,
    // *discrete* per-piece approximation specifically because this field
    // didn't exist yet. Defaults to opaque white -- see the same "old
    // 3-field aggregate-init call sites... default-initialized to a
    // non-degenerate value" precedent `tangent` above already
    // establishes: every existing procedural generator across this
    // codebase (appendBox/appendSphere/appendProfiledBarrel/etc.), which
    // never sets this field, keeps compiling and rendering byte-
    // identical to before (white * baseColor == baseColor). Only
    // AvatarHair.cpp's own appendHairBlob() sets this to something other
    // than white today.
    glm::vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    static VkVertexInputBindingDescription bindingDescription();
    static std::vector<VkVertexInputAttributeDescription> attributeDescriptions();
};

// Fills in `vertices[i].tangent` for every vertex from `indices`' triangle
// winding + UVs -- real per-triangle accumulation + Gram-Schmidt
// orthogonalization (Mesh.cpp's comment has the full algorithm writeup).
// Exposed here (not file-local to Mesh.cpp) so it's independently
// testable and reusable by a future real asset importer, not just
// Mesh::uploadFromHost()'s automatic call for every procedural generator.
void computeTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

// A GPU-resident mesh: VMA-allocated, device-local vertex + index buffers,
// uploaded once via a staging buffer. Procedurally generated here
// (createBox/createPlane) -- real asset loading is
// migration::AssetConverter's job (§7 of the architecture doc, still a
// stub); Mesh is the runtime representation whatever that eventually
// produces gets uploaded into. This is the real implementation Components.hpp's
// Renderable::meshHandle was always meant to resolve against -- it was an
// intentionally opaque, unresolved handle in the first implementation
// pass specifically so this class could slot in without touching the ECS
// component layout.
class Mesh {
public:
    [[nodiscard]] bool uploadFromHost(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                       const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
    void destroy(VmaAllocator allocator);

    [[nodiscard]] VkBuffer vertexBuffer() const { return vertexBuffer_; }
    [[nodiscard]] VkBuffer indexBuffer() const { return indexBuffer_; }
    [[nodiscard]] uint32_t indexCount() const { return indexCount_; }

    // Local-space (pre-Transform) axis-aligned bounding box, computed once
    // from the vertex list in uploadFromHost() -- the CPU-side counterpart
    // to the GPU-only vertex buffer, for anything that needs to reason
    // about a mesh's extent without reading the buffer back from the GPU.
    // The one concrete use today: core::pickEntity() (ScenePicking.hpp)'s
    // ray-vs-AABB test for Studio's viewport click-to-select, which has no
    // Jolt physics body to raycast against for most entities (Studio runs
    // no Physics simulation at all -- see StudioApp.hpp's class comment).
    [[nodiscard]] glm::vec3 localBoundsMin() const { return localBoundsMin_; }
    [[nodiscard]] glm::vec3 localBoundsMax() const { return localBoundsMax_; }

    // Procedural generators -- box centered on the origin, plane in the
    // XZ plane centered on the origin. Both include real per-face normals
    // (a box needs 24 vertices, not 8, for flat-shaded faces -- shared
    // corner vertices would average normals across faces and look wrong).
    [[nodiscard]] static Mesh createBox(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                         glm::vec3 halfExtents);
    [[nodiscard]] static Mesh createPlane(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                           float halfWidth, float halfDepth);

    // A real capsule -- two hemispherical caps + a cylindrical body,
    // matching core::Physics::createCharacterCapsule's collision shape
    // parameters exactly (radius, halfHeight = cylinder half-height, not
    // including the caps) so the character's visual can finally match its
    // physics shape instead of the box placeholder engine_runtime's
    // main.cpp used. Each hemisphere's equator ring is generated with
    // exactly the same radius and purely-horizontal normals as the
    // cylinder wall, so cap and body meet with no seam or normal
    // discontinuity -- not two separately-UV'd pieces glued together.
    [[nodiscard]] static Mesh createCapsule(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                             VkQueue queue, float radius, float halfHeight,
                                             uint32_t radialSegments = 16, uint32_t capRings = 8);

    // A flat unit quad in local XY (Z=0), -halfSize..+halfSize on both
    // axes, normal fixed at +Z. Not used for lit geometry -- this is the
    // single shared mesh every particle billboard instances (see
    // Renderer::drawParticles()); the particle vertex shader rebuilds it
    // to face the camera every frame using the view matrix's right/up
    // axes, so the mesh's own orientation here doesn't matter beyond
    // "flat and centered".
    [[nodiscard]] static Mesh createQuad(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue,
                                          float halfSize);

    // A real cylinder -- flat top/bottom caps (fan-triangulated from a
    // center vertex, +-Y normals) + a smooth radial-normal side wall,
    // same "duplicate the seam vertex for a clean UV wrap" convention
    // createCapsule() already uses. Built for the Block Builder plugin's
    // primitive palette (studio::plugins::BlockBuilderPlugin) -- note
    // there is no MeshSourceKind::Cylinder (see that enum's own comment):
    // a block spawned with this mesh renders/moves/selects normally for
    // the rest of the session but won't survive a Save Scene reload with
    // its exact shape, the same honest limitation core::Terrain already
    // has for a different reason (see SceneFile.hpp's own doc comment).
    [[nodiscard]] static Mesh createCylinder(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                              VkQueue queue, float radius, float halfHeight,
                                              uint32_t radialSegments = 16);

    // A real wedge/ramp -- a box whose front face (+Z) collapses to a
    // single bottom edge, leaving one flat sloped face from the back
    // face's top edge down to the front-bottom edge (Roblox's own
    // WedgePart shape). Same MeshSourceKind caveat as createCylinder()
    // above -- no MeshSourceKind::Wedge exists, live-session only.
    [[nodiscard]] static Mesh createWedge(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool,
                                           VkQueue queue, glm::vec3 halfExtents);

private:
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation_ = nullptr;
    VkBuffer indexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation indexAllocation_ = nullptr;
    uint32_t indexCount_ = 0;

    glm::vec3 localBoundsMin_{0.0f};
    glm::vec3 localBoundsMax_{0.0f};
};

// Owns every Mesh that exists, handing out the handles
// core::Renderable::meshHandle stores. Index-based, not a map -- meshes
// are never removed individually in this skeleton (only destroyAll() at
// shutdown), so a plain vector is the honest data structure for what's
// actually used rather than a general-purpose registry with unused
// removal machinery.
class MeshLibrary {
public:
    uint32_t registerMesh(Mesh mesh);
    [[nodiscard]] const Mesh* get(uint32_t handle) const;

    // Destroys the GPU buffers currently at `handle` and replaces them
    // with `newMesh`'s (already-uploaded, see Mesh::uploadFromHost) --
    // the same handle keeps working for every Renderable that already
    // references it. Added for core::Terrain's chunk regeneration (a
    // brush stroke rebuilds a chunk's mesh in place rather than
    // registering a new handle and leaking the old one); no other caller
    // needs "replace the mesh at a stable handle" today, but the
    // operation is generic, not terrain-specific.
    void replaceMesh(uint32_t handle, Mesh newMesh, VmaAllocator allocator);

    void destroyAll(VmaAllocator allocator);

private:
    std::vector<Mesh> meshes_;
};

} // namespace engine::core
