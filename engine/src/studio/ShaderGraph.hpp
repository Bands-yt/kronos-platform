#pragma once

#include <string>
#include <vector>

namespace engine::studio {

// Data model for the node-based surface shader graph. Headless and
// UI-free; ShaderGraphCodegen turns it into GLSL, and the generated
// surface function plugs into the shared forward shader (see
// shaders/kronos/forward_main.glsl, KRONOS_SURFACE_GRAPH).

enum class ShaderDataType { Float, Vec2, Vec3, Vec4 };
[[nodiscard]] const char* shaderDataTypeName(ShaderDataType type);
[[nodiscard]] const char* shaderDataTypeGlslTypeName(ShaderDataType type);

// Append only: tests and older code index pins by position, and the
// serialized form stores kinds by key (shaderNodeKindKey).
enum class ShaderNodeKind {
    InputWorldPosition,
    InputWorldNormal,
    InputUV,
    ConstantFloat,
    ConstantVec3,
    ConstantVec4,
    AddFloat,
    AddVec4,
    MultiplyFloat,
    MultiplyVec4,
    TextureSample, // samples the entity's own albedo texture
    PbrOutput,     // the single sink; unconnected inputs keep the material's value
    InputTime,
    InputViewDirection,
    MaterialColor, // the entity's material colour after textures and vertex colour
    SubtractFloat,
    DivideFloat,
    Sine,
    Power,
    OneMinus,
    Saturate,
    Smoothstep,
    LerpVec4,
    ScaleVec4,
    ScaleVec3,
    Fresnel,
    Noise,
    Checker,
    SplitVec2,
    SplitVec3,
    SplitVec4,
    CombineVec4,
    Vec3ToVec4,
    Vec4ToVec3,
};
inline constexpr int kShaderNodeKindCount = static_cast<int>(ShaderNodeKind::Vec4ToVec3) + 1;

[[nodiscard]] const char* shaderNodeKindName(ShaderNodeKind kind);
[[nodiscard]] const char* shaderNodeKindKey(ShaderNodeKind kind);
[[nodiscard]] const char* shaderNodeKindCategory(ShaderNodeKind kind);
[[nodiscard]] bool shaderNodeKindFromKey(const std::string& key, ShaderNodeKind& out);

struct ShaderPin {
    int id = 0; // unique across the graph; doubles as the imnodes attribute id
    int nodeId = 0;
    bool isOutput = false;
    ShaderDataType type = ShaderDataType::Float;
    std::string label;
};

struct ShaderNode {
    int id = 0;
    ShaderNodeKind kind = ShaderNodeKind::ConstantFloat;
    float positionX = 0.0f;
    float positionY = 0.0f;
    float constantValue[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    std::vector<int> pinIds; // inputs first, then outputs
};

struct ShaderLink {
    int id = 0;
    int outputPinId = 0;
    int inputPinId = 0;
};

class ShaderGraph {
public:
    int addNode(ShaderNodeKind kind, float positionX, float positionY);
    void removeNode(int nodeId);

    [[nodiscard]] bool addLink(int outputPinId, int inputPinId, std::string& outError);
    void removeLink(int linkId);
    void clear();

    [[nodiscard]] const std::vector<ShaderNode>& nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<ShaderPin>& pins() const { return pins_; }
    [[nodiscard]] const std::vector<ShaderLink>& links() const { return links_; }

    [[nodiscard]] ShaderNode* findNode(int nodeId);
    [[nodiscard]] const ShaderNode* findNode(int nodeId) const;
    [[nodiscard]] const ShaderPin* findPin(int pinId) const;
    [[nodiscard]] const ShaderLink* findLinkInto(int inputPinId) const;

    // Position of a pin within its node's pinIds, or -1.
    [[nodiscard]] int pinIndex(int pinId) const;

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static bool deserialize(const std::string& text, ShaderGraph& out, std::string& outError);

    // Material Color -> PBR Output: renders exactly like the plain material.
    [[nodiscard]] static ShaderGraph makeDefault();

private:
    int nextId_ = 1;
    std::vector<ShaderNode> nodes_;
    std::vector<ShaderPin> pins_;
    std::vector<ShaderLink> links_;
};

} // namespace engine::studio
