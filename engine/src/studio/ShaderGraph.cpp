#include "studio/ShaderGraph.hpp"

#include <algorithm>

namespace engine::studio {

const char* shaderDataTypeName(ShaderDataType type) {
    switch (type) {
        case ShaderDataType::Float: return "Float";
        case ShaderDataType::Vec2: return "Vec2";
        case ShaderDataType::Vec3: return "Vec3";
        case ShaderDataType::Vec4: return "Vec4";
    }
    return "Float";
}

const char* shaderDataTypeGlslTypeName(ShaderDataType type) {
    switch (type) {
        case ShaderDataType::Float: return "float";
        case ShaderDataType::Vec2: return "vec2";
        case ShaderDataType::Vec3: return "vec3";
        case ShaderDataType::Vec4: return "vec4";
    }
    return "float";
}

const char* shaderNodeKindName(ShaderNodeKind kind) {
    switch (kind) {
        case ShaderNodeKind::InputWorldPosition: return "World Position";
        case ShaderNodeKind::InputWorldNormal: return "World Normal";
        case ShaderNodeKind::InputUV: return "UV";
        case ShaderNodeKind::ConstantFloat: return "Float";
        case ShaderNodeKind::ConstantVec3: return "Vector3";
        case ShaderNodeKind::ConstantVec4: return "Vector4 / Color";
        case ShaderNodeKind::AddFloat: return "Add (Float)";
        case ShaderNodeKind::AddVec4: return "Add (Vector4)";
        case ShaderNodeKind::MultiplyFloat: return "Multiply (Float)";
        case ShaderNodeKind::MultiplyVec4: return "Multiply (Vector4)";
        case ShaderNodeKind::TextureSample: return "Texture Sample";
        case ShaderNodeKind::PbrOutput: return "PBR Output";
    }
    return "Unknown";
}

namespace {
// Real, fixed per-kind pin layout -- see ShaderGraph::addNode()'s own
// call site below. Each entry: (isOutput, type, label). Order matters:
// ShaderNode::pinIds preserves this exact order, and codegen (a
// separate file) depends on that fixed indexing (e.g. PbrOutput's
// pinIds[0] is always its baseColor input) rather than searching by
// label at codegen time.
struct PinSpec {
    bool isOutput;
    ShaderDataType type;
    const char* label;
};

std::vector<PinSpec> pinLayoutFor(ShaderNodeKind kind) {
    switch (kind) {
        case ShaderNodeKind::InputWorldPosition: return {{true, ShaderDataType::Vec3, "World Pos"}};
        case ShaderNodeKind::InputWorldNormal: return {{true, ShaderDataType::Vec3, "Normal"}};
        case ShaderNodeKind::InputUV: return {{true, ShaderDataType::Vec2, "UV"}};
        case ShaderNodeKind::ConstantFloat: return {{true, ShaderDataType::Float, "Value"}};
        case ShaderNodeKind::ConstantVec3: return {{true, ShaderDataType::Vec3, "Value"}};
        case ShaderNodeKind::ConstantVec4: return {{true, ShaderDataType::Vec4, "Value"}};
        case ShaderNodeKind::AddFloat:
            return {{false, ShaderDataType::Float, "A"}, {false, ShaderDataType::Float, "B"}, {true, ShaderDataType::Float, "Result"}};
        case ShaderNodeKind::AddVec4:
            return {{false, ShaderDataType::Vec4, "A"}, {false, ShaderDataType::Vec4, "B"}, {true, ShaderDataType::Vec4, "Result"}};
        case ShaderNodeKind::MultiplyFloat:
            return {{false, ShaderDataType::Float, "A"}, {false, ShaderDataType::Float, "B"}, {true, ShaderDataType::Float, "Result"}};
        case ShaderNodeKind::MultiplyVec4:
            return {{false, ShaderDataType::Vec4, "A"}, {false, ShaderDataType::Vec4, "B"}, {true, ShaderDataType::Vec4, "Result"}};
        case ShaderNodeKind::TextureSample:
            return {{false, ShaderDataType::Vec2, "UV"}, {true, ShaderDataType::Vec4, "Color"}};
        case ShaderNodeKind::PbrOutput:
            return {{false, ShaderDataType::Vec4, "Base Color"},
                    {false, ShaderDataType::Float, "Metallic"},
                    {false, ShaderDataType::Float, "Roughness"},
                    {false, ShaderDataType::Vec3, "Emissive"}};
    }
    return {};
}
} // namespace

int ShaderGraph::addNode(ShaderNodeKind kind, float positionX, float positionY) {
    ShaderNode node;
    node.id = nextId_++;
    node.kind = kind;
    node.positionX = positionX;
    node.positionY = positionY;

    for (const PinSpec& spec : pinLayoutFor(kind)) {
        ShaderPin pin;
        pin.id = nextId_++;
        pin.nodeId = node.id;
        pin.isOutput = spec.isOutput;
        pin.type = spec.type;
        pin.label = spec.label;
        node.pinIds.push_back(pin.id);
        pins_.push_back(pin);
    }

    nodes_.push_back(node);
    return node.id;
}

void ShaderGraph::removeNode(int nodeId) {
    const ShaderNode* node = findNode(nodeId);
    if (node == nullptr) return;
    std::vector<int> pinIds = node->pinIds; // copy -- node itself is erased below, before the loop that uses this

    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                 [&](const ShaderLink& link) {
                                     return std::find(pinIds.begin(), pinIds.end(), link.outputPinId) != pinIds.end() ||
                                            std::find(pinIds.begin(), pinIds.end(), link.inputPinId) != pinIds.end();
                                 }),
                 links_.end());
    pins_.erase(std::remove_if(pins_.begin(), pins_.end(), [&](const ShaderPin& pin) { return pin.nodeId == nodeId; }),
                pins_.end());
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const ShaderNode& n) { return n.id == nodeId; }),
                 nodes_.end());
}

bool ShaderGraph::addLink(int outputPinId, int inputPinId, std::string& outError) {
    const ShaderPin* outputPin = findPin(outputPinId);
    const ShaderPin* inputPin = findPin(inputPinId);
    if (outputPin == nullptr || inputPin == nullptr) {
        outError = "one or both pins do not exist";
        return false;
    }
    if (!outputPin->isOutput) {
        outError = "the first pin must be an output pin";
        return false;
    }
    if (inputPin->isOutput) {
        outError = "the second pin must be an input pin";
        return false;
    }
    if (outputPin->type != inputPin->type) {
        outError = std::string("type mismatch: cannot connect ") + shaderDataTypeName(outputPin->type) + " to " +
                    shaderDataTypeName(inputPin->type);
        return false;
    }
    if (findLinkInto(inputPinId) != nullptr) {
        outError = "this input already has an incoming connection -- remove it first";
        return false;
    }
    if (outputPin->nodeId == inputPin->nodeId) {
        outError = "cannot connect a node to itself";
        return false;
    }

    ShaderLink link;
    link.id = nextId_++;
    link.outputPinId = outputPinId;
    link.inputPinId = inputPinId;
    links_.push_back(link);
    return true;
}

void ShaderGraph::removeLink(int linkId) {
    links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const ShaderLink& link) { return link.id == linkId; }),
                 links_.end());
}

ShaderNode* ShaderGraph::findNode(int nodeId) {
    for (auto& node : nodes_) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const ShaderNode* ShaderGraph::findNode(int nodeId) const {
    for (const auto& node : nodes_) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const ShaderPin* ShaderGraph::findPin(int pinId) const {
    for (const auto& pin : pins_) {
        if (pin.id == pinId) return &pin;
    }
    return nullptr;
}

const ShaderLink* ShaderGraph::findLinkInto(int inputPinId) const {
    for (const auto& link : links_) {
        if (link.inputPinId == inputPinId) return &link;
    }
    return nullptr;
}

} // namespace engine::studio
