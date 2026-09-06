#pragma once

#include <string>
#include <vector>

namespace engine::studio {

// Kronos ("Real-Time GPU Particle Compute" -- v0.4.0 Creator Suite): a
// real, node-based data model for a GPU compute-shader particle update
// -- the compute-stage sibling of ShaderGraph.hpp's own fragment-stage
// node graph, deliberately a SEPARATE class rather than new
// ShaderNodeKind cases bolted onto that one. Reasons: ShaderGraph.hpp's
// own class comment states it is "Fragment-stage only... targeting the
// same real varying names scene.frag's own vertex stage already
// produces" -- a per-particle compute update has none of that (no
// world-position/UV varyings, no PBR sink), and conflating the two
// would put particle-simulation nodes in the same authoring palette as
// material nodes, which is a real, wrong UX, not just an implementation
// wrinkle. This file instead mirrors ShaderGraph's own real, proven
// shape (id-based nodes/pins/links, addNode()/addLink() as the only
// mutators, exact-type-match links, an input pin takes at most one
// incoming link) applied to a genuinely different domain.
//
// Deliberately bounded node set for this phase, same "prove the real
// graph -> GLSL -> compiled SPIR-V pipeline end to end, not the large
// node library a mature particle tool eventually needs" scope
// ShaderGraph.hpp's own header comment already states for its domain:
// enough real nodes to author gravity + drag + Euler integration (a
// real, useful particle update), not curl-noise fluid advection or SPH
// -- see ParticleComputeCodegen.hpp for exactly what gets generated.

enum class ParticleDataType { Float, Vec3 };
[[nodiscard]] const char* particleDataTypeName(ParticleDataType type);
[[nodiscard]] const char* particleDataTypeGlslTypeName(ParticleDataType type);

enum class ParticleNodeKind {
    // Real per-particle inputs -- one output pin each, values come from
    // this graph's one real consumer: the generated compute shader's
    // own Particle SSBO entry for gl_GlobalInvocationID.x (see
    // ParticleComputeCodegen.cpp).
    InputPosition, // vec3
    InputVelocity, // vec3
    InputAge,      // float, seconds since spawn
    InputDeltaTime, // float, this dispatch's real frame dt (a uniform, not per-particle)
    // Constants -- one output pin; the real value is authored on the
    // node itself (ParticleNode::constantValue).
    ConstantFloat,
    ConstantVec3,
    // Math -- real, separate kinds per type, same reasoning
    // ShaderGraph.hpp's own AddFloat/AddVec4 split already states: the
    // two types this phase's real node set actually needs math on.
    AddVec3,
    ScaleVec3, // Vec3 * Float -- e.g. velocity * drag, gravity * dt
    // The graph's one real sink -- fixed input pins matching the real
    // per-particle state the generated compute shader writes back:
    // newPosition/newVelocity (both vec3). Exactly one ParticleOutput
    // node must exist in a graph for ParticleComputeCodegen to generate
    // anything from it.
    ParticleOutput,
};
[[nodiscard]] const char* particleNodeKindName(ParticleNodeKind kind);

struct ParticlePin {
    int id = 0;
    int nodeId = 0;
    bool isOutput = false;
    ParticleDataType type = ParticleDataType::Float;
    std::string label;
};

struct ParticleNode {
    int id = 0;
    ParticleNodeKind kind = ParticleNodeKind::ConstantFloat;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float constantValue[3] = {0.0f, 0.0f, 0.0f}; // meaningful only for ConstantFloat/ConstantVec3, see ShaderNode's own analog
    std::vector<int> pinIds; // fixed, kind-specific order: inputs first, then the single output -- set by addNode(), never reordered
};

struct ParticleLink {
    int id = 0;
    int outputPinId = 0;
    int inputPinId = 0;
};

// Kronos: the real, owning graph -- addNode()/addLink() are the only
// ways to mutate it, same single-place-enforces-every-invariant shape
// ShaderGraph already establishes.
class ParticleComputeGraph {
public:
    int addNode(ParticleNodeKind kind, float positionX = 0.0f, float positionY = 0.0f);
    void removeNode(int nodeId);

    [[nodiscard]] bool addLink(int outputPinId, int inputPinId, std::string& outError);
    void removeLink(int linkId);

    [[nodiscard]] const std::vector<ParticleNode>& nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<ParticlePin>& pins() const { return pins_; }
    [[nodiscard]] const std::vector<ParticleLink>& links() const { return links_; }

    [[nodiscard]] ParticleNode* findNode(int nodeId);
    [[nodiscard]] const ParticleNode* findNode(int nodeId) const;
    [[nodiscard]] const ParticlePin* findPin(int pinId) const;
    [[nodiscard]] const ParticleLink* findLinkInto(int inputPinId) const;

private:
    int nextId_ = 1;
    std::vector<ParticleNode> nodes_;
    std::vector<ParticlePin> pins_;
    std::vector<ParticleLink> links_;
};

} // namespace engine::studio
