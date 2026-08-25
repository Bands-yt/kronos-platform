#pragma once

#include <string>
#include <vector>

namespace engine::studio {

// Kronos ("Studio Revamp" -- "Node-Based Visual Shader Graph" Phase 2):
// real data model for the shader graph -- the imnodes-based editor
// panel (a real, separate, later addition) is deliberately a thin
// drawing/interaction layer over this; every real question ("what does
// this graph mean," "is this connection legal") is answered here,
// headlessly testable with no ImGui/live editor needed (see
// ShaderGraphCodegen.hpp for the GLSL-generation half). No ImGui/ImVec2
// dependency in this header on purpose, same "computational logic
// stays UI-framework-free" shape core::PropertyTrack already has, even
// though this file lives under studio/ (Studio-authoring-only, not a
// runtime-gameplay concern) rather than core/.
//
// Deliberately bounded node set for this phase, proving the real
// graph -> GLSL -> compiled-SPIR-V pipeline end to end (see
// RuntimeShaderCompiler.hpp) rather than building the large node
// library a mature shader graph tool eventually needs. Fragment-stage
// only, targeting the same real varying names scene.frag's own vertex
// stage already produces (inWorldPos/inWorldNormal/inUV -- see that
// file's own layout(location=...) declarations) -- NOT yet wired into
// core::Renderer's live pipeline (a real, separate, later step this
// phase does not attempt).

enum class ShaderDataType { Float, Vec2, Vec3, Vec4 };
[[nodiscard]] const char* shaderDataTypeName(ShaderDataType type);
[[nodiscard]] const char* shaderDataTypeGlslTypeName(ShaderDataType type);

enum class ShaderNodeKind {
    // Real inputs -- one output pin, no input pins. Values come from
    // this engine's own real vertex-stage varyings (see this header's
    // own comment above).
    InputWorldPosition, // vec3, from inWorldPos
    InputWorldNormal,   // vec3, from inWorldNormal
    InputUV,            // vec2, from inUV
    // Constants -- one output pin; the real value is authored on the
    // node itself (ShaderNode::constantValue below), not via an input pin.
    ConstantFloat,
    ConstantVec3,
    ConstantVec4,
    // Math -- two input pins of the same real type, one output pin of
    // that same type. Real, separate kinds per type (not one generic
    // "Add" auto-matching whatever's plugged in) -- addLink()'s own
    // exact-type-match rule means a single Vec4-typed Add couldn't
    // accept two Float operands anyway; Float and Vec4 are the two
    // types this phase's node set actually produces often enough
    // (scalar tuning constants, colors) to be worth a math node for --
    // Vec3 values (World Normal, a Vec3 constant) plug directly into
    // PbrOutput.emissive without needing math on them yet.
    AddFloat,
    AddVec4,
    MultiplyFloat,
    MultiplyVec4,
    // Real texture sample -- one vec2 input pin (UV), one vec4 output.
    // Phase 2 scope cut, stated plainly: samples one fixed, explicit
    // `materialAlbedo` sampler2D binding (ShaderGraphCodegen.cpp), not
    // this engine's real bindless material-texture system
    // (Renderer::getOrCreateMaterialDescriptorSet()) -- wiring a
    // generated graph shader into that real system is part of the
    // separate, later Renderer-integration step, not this data model.
    TextureSample,
    // The graph's one real sink -- fixed input pins matching
    // core::Renderable's own real PBR fields (Components.hpp):
    // baseColor (vec4), metallic (float), roughness (float), emissive
    // (vec3). Exactly one PbrOutput node must exist in a graph for
    // ShaderGraphCodegen to generate anything from it.
    PbrOutput,
};
[[nodiscard]] const char* shaderNodeKindName(ShaderNodeKind kind);

struct ShaderPin {
    int id = 0; // globally unique across the whole graph -- doubles as the imnodes attribute id
    int nodeId = 0;
    bool isOutput = false;
    ShaderDataType type = ShaderDataType::Float;
    std::string label;
};

struct ShaderNode {
    int id = 0;
    ShaderNodeKind kind = ShaderNodeKind::ConstantFloat;
    // Real node-editor canvas position -- plain floats, not ImVec2 (see
    // this header's own comment on why no ImGui dependency here).
    float positionX = 0.0f;
    float positionY = 0.0f;
    // Real, authored constant value -- meaningful only for
    // ConstantFloat/ConstantVec3/ConstantVec4 (indices used per
    // shaderDataTypeName's own component count); left at its default
    // and unused for every other kind. Plain floats (authoring-time UI
    // state edited via ImGui::DragFloatN), not a core::PropertyValue --
    // that type is for runtime-keyframed values, this is a fixed,
    // hand-authored constant.
    float constantValue[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    // Indices into ShaderGraph::pins() for this node's own pins, in a
    // fixed, real, kind-specific order (inputs first, then the single
    // output) -- set once by ShaderGraph::addNode(), never reordered.
    std::vector<int> pinIds;
};

struct ShaderLink {
    int id = 0;
    int outputPinId = 0; // source (an output pin)
    int inputPinId = 0;  // destination (an input pin)
};

// Kronos: the real, owning graph -- addNode()/addLink() are the only
// ways to mutate it, so every invariant (pin types match this node
// kind's real layout, a link only ever joins an output pin to an input
// pin of the same ShaderDataType, an input pin accepts at most one
// incoming link) is enforced in one place rather than trusted at every
// call site.
class ShaderGraph {
public:
    // Creates the node plus its real, fixed set of pins for `kind` (see
    // ShaderNodeKind's own per-kind comments) at the given canvas
    // position. Returns the new node's id.
    int addNode(ShaderNodeKind kind, float positionX, float positionY);
    // Removes the node, its own pins, and any links touching those pins
    // -- no dangling pin/link ids left behind.
    void removeNode(int nodeId);

    // Real validation before inserting: `outputPinId` must name a real
    // output pin, `inputPinId` a real input pin, both pins'
    // ShaderDataType must match exactly (no implicit float->vec3
    // widening -- an honest, explicit type system, not automatic
    // coercion which would remove the type-safety a node graph is
    // partly *for*), and the input pin must not already have an
    // incoming link (same "an input has at most one source" convention
    // every real node-graph tool uses -- replacing a connection means
    // deleting the old link first, not silently multi-driving one
    // input). Returns false and fills `outError` with which check
    // failed; does not insert on failure.
    [[nodiscard]] bool addLink(int outputPinId, int inputPinId, std::string& outError);
    void removeLink(int linkId);

    [[nodiscard]] const std::vector<ShaderNode>& nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<ShaderPin>& pins() const { return pins_; }
    [[nodiscard]] const std::vector<ShaderLink>& links() const { return links_; }

    [[nodiscard]] ShaderNode* findNode(int nodeId);
    [[nodiscard]] const ShaderNode* findNode(int nodeId) const;
    [[nodiscard]] const ShaderPin* findPin(int pinId) const;
    // The real link whose inputPinId == `inputPinId`, if any -- an
    // input pin has at most one incoming link (addLink()'s own
    // invariant), so this is never ambiguous.
    [[nodiscard]] const ShaderLink* findLinkInto(int inputPinId) const;

private:
    int nextId_ = 1;
    std::vector<ShaderNode> nodes_;
    std::vector<ShaderPin> pins_;
    std::vector<ShaderLink> links_;
};

} // namespace engine::studio
