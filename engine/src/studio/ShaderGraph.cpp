#include "studio/ShaderGraph.hpp"

#include <algorithm>
#include <map>
#include <locale>
#include <sstream>

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

namespace {

struct PinSpec {
    bool isOutput;
    ShaderDataType type;
    const char* label;
};

struct KindInfo {
    const char* name;
    const char* key;
    const char* category;
    std::vector<PinSpec> pins;
};

constexpr ShaderDataType F = ShaderDataType::Float;
constexpr ShaderDataType V2 = ShaderDataType::Vec2;
constexpr ShaderDataType V3 = ShaderDataType::Vec3;
constexpr ShaderDataType V4 = ShaderDataType::Vec4;

const KindInfo& kindInfo(ShaderNodeKind kind) {
    static const std::vector<KindInfo> kInfos = {
        {"World Position", "world_position", "Inputs", {{true, V3, "World Pos"}}},
        {"World Normal", "world_normal", "Inputs", {{true, V3, "Normal"}}},
        {"UV", "uv", "Inputs", {{true, V2, "UV"}}},
        {"Float", "const_float", "Constants", {{true, F, "Value"}}},
        {"Vector3", "const_vec3", "Constants", {{true, V3, "Value"}}},
        {"Vector4 / Color", "const_vec4", "Constants", {{true, V4, "Value"}}},
        {"Add (Float)", "add_float", "Math", {{false, F, "A"}, {false, F, "B"}, {true, F, "Result"}}},
        {"Add (Vector4)", "add_vec4", "Math", {{false, V4, "A"}, {false, V4, "B"}, {true, V4, "Result"}}},
        {"Multiply (Float)", "mul_float", "Math", {{false, F, "A"}, {false, F, "B"}, {true, F, "Result"}}},
        {"Multiply (Vector4)", "mul_vec4", "Math", {{false, V4, "A"}, {false, V4, "B"}, {true, V4, "Result"}}},
        {"Texture Sample", "texture_sample", "Inputs", {{false, V2, "UV"}, {true, V4, "Color"}}},
        {"PBR Output", "pbr_output", "Output",
         {{false, V4, "Base Color"}, {false, F, "Metallic"}, {false, F, "Roughness"}, {false, V3, "Emissive"}}},
        {"Time", "time", "Inputs", {{true, F, "Seconds"}}},
        {"View Direction", "view_direction", "Inputs", {{true, V3, "View Dir"}}},
        {"Material Color", "material_color", "Inputs", {{true, V4, "Color"}}},
        {"Subtract (Float)", "sub_float", "Math", {{false, F, "A"}, {false, F, "B"}, {true, F, "Result"}}},
        {"Divide (Float)", "div_float", "Math", {{false, F, "A"}, {false, F, "B"}, {true, F, "Result"}}},
        {"Sine", "sine", "Math", {{false, F, "In"}, {true, F, "Result"}}},
        {"Power", "power", "Math", {{false, F, "Base"}, {false, F, "Exponent"}, {true, F, "Result"}}},
        {"One Minus", "one_minus", "Math", {{false, F, "In"}, {true, F, "Result"}}},
        {"Saturate", "saturate", "Math", {{false, F, "In"}, {true, F, "Result"}}},
        {"Smoothstep", "smoothstep", "Math", {{false, F, "Edge 0"}, {false, F, "Edge 1"}, {false, F, "X"}, {true, F, "Result"}}},
        {"Lerp (Vector4)", "lerp_vec4", "Math", {{false, V4, "A"}, {false, V4, "B"}, {false, F, "T"}, {true, V4, "Result"}}},
        {"Scale (Vector4)", "scale_vec4", "Math", {{false, V4, "Vector"}, {false, F, "Scale"}, {true, V4, "Result"}}},
        {"Scale (Vector3)", "scale_vec3", "Math", {{false, V3, "Vector"}, {false, F, "Scale"}, {true, V3, "Result"}}},
        {"Fresnel", "fresnel", "Patterns", {{false, F, "Power"}, {true, F, "Result"}}},
        {"Noise", "noise", "Patterns", {{false, V3, "Position"}, {false, F, "Scale"}, {true, F, "Result"}}},
        {"Checker", "checker", "Patterns", {{false, V3, "Position"}, {false, F, "Scale"}, {true, F, "Result"}}},
        {"Split (Vector2)", "split_vec2", "Vectors", {{false, V2, "In"}, {true, F, "X"}, {true, F, "Y"}}},
        {"Split (Vector3)", "split_vec3", "Vectors", {{false, V3, "In"}, {true, F, "X"}, {true, F, "Y"}, {true, F, "Z"}}},
        {"Split (Vector4)", "split_vec4", "Vectors",
         {{false, V4, "In"}, {true, F, "R"}, {true, F, "G"}, {true, F, "B"}, {true, F, "A"}}},
        {"Combine (Vector4)", "combine_vec4", "Vectors",
         {{false, F, "R"}, {false, F, "G"}, {false, F, "B"}, {false, F, "A"}, {true, V4, "Result"}}},
        {"Vector3 to Vector4", "vec3_to_vec4", "Vectors", {{false, V3, "In"}, {true, V4, "Result"}}},
        {"Vector4 to Vector3", "vec4_to_vec3", "Vectors", {{false, V4, "In"}, {true, V3, "Result"}}},
    };
    static_assert(kShaderNodeKindCount == 34);
    return kInfos[static_cast<size_t>(kind)];
}

} // namespace

const char* shaderNodeKindName(ShaderNodeKind kind) { return kindInfo(kind).name; }
const char* shaderNodeKindKey(ShaderNodeKind kind) { return kindInfo(kind).key; }
const char* shaderNodeKindCategory(ShaderNodeKind kind) { return kindInfo(kind).category; }

bool shaderNodeKindFromKey(const std::string& key, ShaderNodeKind& out) {
    for (int i = 0; i < kShaderNodeKindCount; ++i) {
        if (key == kindInfo(static_cast<ShaderNodeKind>(i)).key) {
            out = static_cast<ShaderNodeKind>(i);
            return true;
        }
    }
    return false;
}


int ShaderGraph::addNode(ShaderNodeKind kind, float positionX, float positionY) {
    ShaderNode node;
    node.id = nextId_++;
    node.kind = kind;
    node.positionX = positionX;
    node.positionY = positionY;

    for (const PinSpec& spec : kindInfo(kind).pins) {
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

void ShaderGraph::clear() {
    nextId_ = 1;
    nodes_.clear();
    pins_.clear();
    links_.clear();
}

int ShaderGraph::pinIndex(int pinId) const {
    const ShaderPin* pin = findPin(pinId);
    if (pin == nullptr) return -1;
    const ShaderNode* node = findNode(pin->nodeId);
    if (node == nullptr) return -1;
    auto it = std::find(node->pinIds.begin(), node->pinIds.end(), pinId);
    return it == node->pinIds.end() ? -1 : static_cast<int>(it - node->pinIds.begin());
}

namespace {
constexpr const char* kSerializedHeader = "kronos-shader-graph 1";
}

std::string ShaderGraph::serialize() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out.precision(9);
    out << kSerializedHeader << '\n';
    for (const ShaderNode& node : nodes_) {
        out << "node " << node.id << ' ' << shaderNodeKindKey(node.kind) << ' ' << node.positionX << ' '
            << node.positionY;
        for (float value : node.constantValue) out << ' ' << value;
        out << '\n';
    }
    for (const ShaderLink& link : links_) {
        const ShaderPin* from = findPin(link.outputPinId);
        const ShaderPin* to = findPin(link.inputPinId);
        if (from == nullptr || to == nullptr) continue;
        out << "link " << from->nodeId << ' ' << pinIndex(from->id) << ' ' << to->nodeId << ' ' << pinIndex(to->id)
            << '\n';
    }
    return out.str();
}

bool ShaderGraph::deserialize(const std::string& text, ShaderGraph& out, std::string& outError) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    std::string line;
    if (!std::getline(in, line) || line != kSerializedHeader) {
        outError = "not a Kronos shader graph (missing \"kronos-shader-graph 1\" header)";
        return false;
    }
    ShaderGraph graph;
    std::map<int, int> nodeIdMap;
    int lineNumber = 1;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        std::istringstream fields(line);
        fields.imbue(std::locale::classic());
        std::string tag;
        fields >> tag;
        if (tag == "node") {
            int id = 0;
            std::string key;
            float x = 0.0f;
            float y = 0.0f;
            float values[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            fields >> id >> key >> x >> y >> values[0] >> values[1] >> values[2] >> values[3];
            ShaderNodeKind kind{};
            if (fields.fail() || !shaderNodeKindFromKey(key, kind)) {
                outError = "line " + std::to_string(lineNumber) + ": bad node \"" + key + "\"";
                return false;
            }
            int newId = graph.addNode(kind, x, y);
            std::copy(values, values + 4, graph.findNode(newId)->constantValue);
            nodeIdMap[id] = newId;
        } else if (tag == "link") {
            int fromNode = 0, fromPin = 0, toNode = 0, toPin = 0;
            fields >> fromNode >> fromPin >> toNode >> toPin;
            auto pinOf = [&](int oldNode, int index) -> int {
                auto it = nodeIdMap.find(oldNode);
                if (it == nodeIdMap.end()) return 0;
                const ShaderNode* node = graph.findNode(it->second);
                if (node == nullptr || index < 0 || index >= static_cast<int>(node->pinIds.size())) return 0;
                return node->pinIds[static_cast<size_t>(index)];
            };
            const int outputPin = pinOf(fromNode, fromPin);
            const int inputPin = pinOf(toNode, toPin);
            std::string linkError;
            if (fields.fail() || outputPin == 0 || inputPin == 0 || !graph.addLink(outputPin, inputPin, linkError)) {
                outError = "line " + std::to_string(lineNumber) + ": bad link" +
                           (linkError.empty() ? std::string{} : " (" + linkError + ")");
                return false;
            }
        } else {
            outError = "line " + std::to_string(lineNumber) + ": unknown entry \"" + tag + "\"";
            return false;
        }
    }
    out = std::move(graph);
    return true;
}

ShaderGraph ShaderGraph::makeDefault() {
    ShaderGraph graph;
    const int color = graph.addNode(ShaderNodeKind::MaterialColor, 40.0f, 80.0f);
    const int output = graph.addNode(ShaderNodeKind::PbrOutput, 360.0f, 60.0f);
    std::string error;
    (void)graph.addLink(graph.findNode(color)->pinIds[0], graph.findNode(output)->pinIds[0], error);
    return graph;
}

} // namespace engine::studio
