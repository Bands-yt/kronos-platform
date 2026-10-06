#pragma once

#include <string>
#include <vector>

namespace engine::studio {

// Data model for node-based visual scripts. Headless and UI-free;
// VisualScriptCompiler turns a graph into Luau and then Luau bytecode.

enum class VsType { Exec, Number, Bool, String, Vector, Entity, Any };
[[nodiscard]] const char* vsTypeName(VsType type);

// Append only: the serialized form stores kinds by key (vsNodeKindKey),
// but tests index pins by position.
enum class VsNodeKind {
    OnStart,
    OnUpdate,
    OnTouched,
    OnInteract,
    OnPlayerJoined,
    OnPlayerLeft,
    EveryInterval,
    Branch,
    Sequence,
    Wait,
    ForLoop,
    Print,
    SetPosition,
    MoveBy,
    SetRotation,
    RotateBy,
    SetScale,
    SetColor,
    SetEmissive,
    ApplyImpulse,
    SetVelocity,
    Destroy,
    SpawnBox,
    SetVariable,
    Self,
    FindEntity,
    GetPosition,
    GetRotation,
    GetVariable,
    Time,
    Number,
    Boolean,
    Text,
    VectorConstant,
    Add,
    Subtract,
    Multiply,
    Divide,
    Modulo,
    Power,
    Abs,
    Min,
    Max,
    Clamp,
    Lerp,
    Sin,
    Cos,
    Floor,
    Round,
    RandomRange,
    MakeVector,
    BreakVector,
    AddVectors,
    SubtractVectors,
    ScaleVector,
    VectorLength,
    Distance,
    Normalize,
    Greater,
    Less,
    Equal,
    And,
    Or,
    Not,
    JoinText,
};
inline constexpr int kVsNodeKindCount = static_cast<int>(VsNodeKind::JoinText) + 1;

[[nodiscard]] const char* vsNodeKindName(VsNodeKind kind);
[[nodiscard]] const char* vsNodeKindKey(VsNodeKind kind);
[[nodiscard]] const char* vsNodeKindCategory(VsNodeKind kind);
[[nodiscard]] const char* vsNodeKindDescription(VsNodeKind kind);
[[nodiscard]] bool vsNodeKindFromKey(const std::string& key, VsNodeKind& out);
[[nodiscard]] bool vsNodeKindIsEvent(VsNodeKind kind);
// Uses a free-text field on the node itself (a variable name).
[[nodiscard]] bool vsNodeKindHasField(VsNodeKind kind);

// Value typed into an unconnected input, or held by a constant node's
// output. Which members matter depends on the pin type.
struct VsLiteral {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    bool flag = false;
    std::string text;
};

struct VsPin {
    int id = 0; // unique across the graph; doubles as the imnodes attribute id
    int nodeId = 0;
    bool isOutput = false;
    VsType type = VsType::Number;
    bool isColor = false;
    std::string label;
};

struct VsNode {
    int id = 0;
    VsNodeKind kind = VsNodeKind::OnStart;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::string field;
    std::vector<int> pinIds;          // inputs first, then outputs
    std::vector<VsLiteral> literals;  // parallel to pinIds
};

struct VsLink {
    int id = 0;
    int outputPinId = 0;
    int inputPinId = 0;
};

class VisualScriptGraph {
public:
    int addNode(VsNodeKind kind, float positionX, float positionY);
    void removeNode(int nodeId);

    // Exec outputs and data inputs take one link, so a new link replaces
    // the old one there.
    [[nodiscard]] bool addLink(int outputPinId, int inputPinId, std::string& outError);
    void removeLink(int linkId);
    void clear();

    [[nodiscard]] const std::vector<VsNode>& nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<VsPin>& pins() const { return pins_; }
    [[nodiscard]] const std::vector<VsLink>& links() const { return links_; }

    [[nodiscard]] VsNode* findNode(int nodeId);
    [[nodiscard]] const VsNode* findNode(int nodeId) const;
    [[nodiscard]] const VsPin* findPin(int pinId) const;
    [[nodiscard]] const VsLink* findLinkInto(int inputPinId) const;
    [[nodiscard]] const VsLink* findLinkFrom(int outputPinId) const;
    [[nodiscard]] int pinIndex(int pinId) const;
    [[nodiscard]] VsLiteral* literal(int pinId);
    [[nodiscard]] const VsLiteral* literal(int pinId) const;

    [[nodiscard]] static bool canConnect(VsType from, VsType to);

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] static bool deserialize(const std::string& text, VisualScriptGraph& out, std::string& outError);

    // On Update -> Rotate By (Self, 0 90*dt 0): spins the object.
    [[nodiscard]] static VisualScriptGraph makeDefault();

private:
    int nextId_ = 1;
    std::vector<VsNode> nodes_;
    std::vector<VsPin> pins_;
    std::vector<VsLink> links_;
};

} // namespace engine::studio
