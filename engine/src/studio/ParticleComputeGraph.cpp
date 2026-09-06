#include "studio/ParticleComputeGraph.hpp"

#include <algorithm>

namespace engine::studio {

const char* particleDataTypeName(ParticleDataType type) {
    switch (type) {
        case ParticleDataType::Float: return "Float";
        case ParticleDataType::Vec3: return "Vector3";
    }
    return "Float";
}

const char* particleDataTypeGlslTypeName(ParticleDataType type) {
    switch (type) {
        case ParticleDataType::Float: return "float";
        case ParticleDataType::Vec3: return "vec3";
    }
    return "float";
}

const char* particleNodeKindName(ParticleNodeKind kind) {
    switch (kind) {
        case ParticleNodeKind::InputPosition: return "Position";
        case ParticleNodeKind::InputVelocity: return "Velocity";
        case ParticleNodeKind::InputAge: return "Age";
        case ParticleNodeKind::InputDeltaTime: return "Delta Time";
        case ParticleNodeKind::ConstantFloat: return "Float";
        case ParticleNodeKind::ConstantVec3: return "Vector3";
        case ParticleNodeKind::AddVec3: return "Add (Vector3)";
        case ParticleNodeKind::ScaleVec3: return "Scale (Vector3 x Float)";
        case ParticleNodeKind::ParticleOutput: return "Particle Output";
    }
    return "Unknown";
}

namespace {

struct PinSpec {
    bool isOutput;
    ParticleDataType type;
    const char* label;
};

std::vector<PinSpec> pinLayoutFor(ParticleNodeKind kind) {
    switch (kind) {
        case ParticleNodeKind::InputPosition: return {{true, ParticleDataType::Vec3, "Position"}};
        case ParticleNodeKind::InputVelocity: return {{true, ParticleDataType::Vec3, "Velocity"}};
        case ParticleNodeKind::InputAge: return {{true, ParticleDataType::Float, "Age"}};
        case ParticleNodeKind::InputDeltaTime: return {{true, ParticleDataType::Float, "Delta Time"}};
        case ParticleNodeKind::ConstantFloat: return {{true, ParticleDataType::Float, "Value"}};
        case ParticleNodeKind::ConstantVec3: return {{true, ParticleDataType::Vec3, "Value"}};
        case ParticleNodeKind::AddVec3:
            return {{false, ParticleDataType::Vec3, "A"}, {false, ParticleDataType::Vec3, "B"}, {true, ParticleDataType::Vec3, "Result"}};
        case ParticleNodeKind::ScaleVec3:
            return {{false, ParticleDataType::Vec3, "Vector"}, {false, ParticleDataType::Float, "Scale"}, {true, ParticleDataType::Vec3, "Result"}};
        case ParticleNodeKind::ParticleOutput:
            return {{false, ParticleDataType::Vec3, "New Position"}, {false, ParticleDataType::Vec3, "New Velocity"}};
    }
    return {};
}

} // namespace

int ParticleComputeGraph::addNode(ParticleNodeKind kind, float positionX, float positionY) {
    ParticleNode node;
    node.id = nextId_++;
    node.kind = kind;
    node.positionX = positionX;
    node.positionY = positionY;

    for (const PinSpec& spec : pinLayoutFor(kind)) {
        ParticlePin pin;
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

void ParticleComputeGraph::removeNode(int nodeId) {
    const ParticleNode* node = findNode(nodeId);
    if (node == nullptr) return;
    std::vector<int> pinIds = node->pinIds;

    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                 [&](const ParticleLink& link) {
                                     return std::find(pinIds.begin(), pinIds.end(), link.outputPinId) != pinIds.end() ||
                                            std::find(pinIds.begin(), pinIds.end(), link.inputPinId) != pinIds.end();
                                 }),
                 links_.end());
    pins_.erase(std::remove_if(pins_.begin(), pins_.end(), [&](const ParticlePin& pin) { return pin.nodeId == nodeId; }),
                pins_.end());
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const ParticleNode& n) { return n.id == nodeId; }),
                 nodes_.end());
}

bool ParticleComputeGraph::addLink(int outputPinId, int inputPinId, std::string& outError) {
    const ParticlePin* outputPin = findPin(outputPinId);
    const ParticlePin* inputPin = findPin(inputPinId);
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
        outError = std::string("type mismatch: cannot connect ") + particleDataTypeName(outputPin->type) + " to " +
                    particleDataTypeName(inputPin->type);
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

    ParticleLink link;
    link.id = nextId_++;
    link.outputPinId = outputPinId;
    link.inputPinId = inputPinId;
    links_.push_back(link);
    return true;
}

void ParticleComputeGraph::removeLink(int linkId) {
    links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const ParticleLink& link) { return link.id == linkId; }),
                 links_.end());
}

ParticleNode* ParticleComputeGraph::findNode(int nodeId) {
    for (auto& node : nodes_) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const ParticleNode* ParticleComputeGraph::findNode(int nodeId) const {
    for (const auto& node : nodes_) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const ParticlePin* ParticleComputeGraph::findPin(int pinId) const {
    for (const auto& pin : pins_) {
        if (pin.id == pinId) return &pin;
    }
    return nullptr;
}

const ParticleLink* ParticleComputeGraph::findLinkInto(int inputPinId) const {
    for (const auto& link : links_) {
        if (link.inputPinId == inputPinId) return &link;
    }
    return nullptr;
}

} // namespace engine::studio
