#include "studio/ParticleComputeCodegen.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <sstream>

#include "studio/ParticleComputeGraph.hpp"

namespace engine::studio {

namespace {

std::string formatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(value));
    return buffer;
}

std::string formatConstant(ParticleDataType type, const float value[3]) {
    if (type == ParticleDataType::Float) return formatFloat(value[0]);
    return "vec3(" + formatFloat(value[0]) + ", " + formatFloat(value[1]) + ", " + formatFloat(value[2]) + ")";
}

// Real recursive resolver, memoized by node id with real cycle
// detection -- the exact same shape studio::ShaderGraphCodegen.cpp's
// own Resolver already establishes for its (unrelated) fragment-stage
// graph, applied here to ParticleComputeGraph/ParticleNodeKind instead.
class Resolver {
public:
    Resolver(const ParticleComputeGraph& graph, std::ostringstream& body) : graph_(graph), body_(body) {}

    std::string resolvePin(const ParticlePin& pin) {
        if (hasError()) return {};
        if (pin.isOutput) return resolveNodeOutput(pin.nodeId);

        const ParticleLink* link = graph_.findLinkInto(pin.id);
        if (link == nullptr) {
            // Real, honest fallback for an unconnected ParticleOutput
            // input: the particle's own current, unmodified value --
            // "leave it alone" is the correct no-op for a position/
            // velocity output nobody wired anything into, not zero.
            if (pin.label == "New Position") return "position";
            if (pin.label == "New Velocity") return "velocity";
            float zero[3] = {0.0f, 0.0f, 0.0f};
            return emitTemp(pin.type, formatConstant(pin.type, zero));
        }
        const ParticlePin* sourcePin = graph_.findPin(link->outputPinId);
        if (sourcePin == nullptr) {
            error_ = "internal error: link references a pin that no longer exists";
            return {};
        }
        return resolveNodeOutput(sourcePin->nodeId);
    }

    [[nodiscard]] bool hasError() const { return !error_.empty(); }
    [[nodiscard]] const std::string& error() const { return error_; }

private:
    std::string emitTemp(ParticleDataType type, const std::string& expr) {
        std::string name = "t" + std::to_string(nextTemp_++);
        body_ << "    " << particleDataTypeGlslTypeName(type) << " " << name << " = " << expr << ";\n";
        return name;
    }

    std::string resolveNodeOutput(int nodeId) {
        if (hasError()) return {};
        auto memoIt = memo_.find(nodeId);
        if (memoIt != memo_.end()) return memoIt->second;

        if (visiting_.count(nodeId) != 0) {
            error_ = "graph has a cycle involving node " + std::to_string(nodeId);
            return {};
        }
        const ParticleNode* node = graph_.findNode(nodeId);
        if (node == nullptr) {
            error_ = "internal error: node " + std::to_string(nodeId) + " not found";
            return {};
        }
        visiting_.insert(nodeId);
        std::string varName = emitNode(*node);
        visiting_.erase(nodeId);
        if (!hasError()) memo_[nodeId] = varName;
        return varName;
    }

    const ParticlePin& inputPin(const ParticleNode& node, size_t index) { return *graph_.findPin(node.pinIds[index]); }

    std::string emitNode(const ParticleNode& node) {
        switch (node.kind) {
            case ParticleNodeKind::InputPosition: return "position";
            case ParticleNodeKind::InputVelocity: return "velocity";
            case ParticleNodeKind::InputAge: return "age";
            case ParticleNodeKind::InputDeltaTime: return "deltaTime";
            case ParticleNodeKind::ConstantFloat:
                return emitTemp(ParticleDataType::Float, formatConstant(ParticleDataType::Float, node.constantValue));
            case ParticleNodeKind::ConstantVec3:
                return emitTemp(ParticleDataType::Vec3, formatConstant(ParticleDataType::Vec3, node.constantValue));
            case ParticleNodeKind::AddVec3: {
                std::string a = resolvePin(inputPin(node, 0));
                std::string b = resolvePin(inputPin(node, 1));
                if (hasError()) return {};
                return emitTemp(ParticleDataType::Vec3, a + " + " + b);
            }
            case ParticleNodeKind::ScaleVec3: {
                std::string vec = resolvePin(inputPin(node, 0));
                std::string scale = resolvePin(inputPin(node, 1));
                if (hasError()) return {};
                return emitTemp(ParticleDataType::Vec3, vec + " * " + scale);
            }
            case ParticleNodeKind::ParticleOutput:
                error_ = "internal error: ParticleOutput cannot be a codegen dependency (it must be the graph's one real sink)";
                return {};
        }
        error_ = "internal error: unhandled ParticleNodeKind";
        return {};
    }

    const ParticleComputeGraph& graph_;
    std::ostringstream& body_;
    std::map<int, std::string> memo_;
    std::set<int> visiting_;
    int nextTemp_ = 0;
    std::string error_;
};

} // namespace

ParticleComputeCodegenResult generateComputeParticleShaderGlsl(const ParticleComputeGraph& graph, uint32_t localSizeX) {
    ParticleComputeCodegenResult result;

    const ParticleNode* outputNode = nullptr;
    int outputNodeCount = 0;
    for (const ParticleNode& node : graph.nodes()) {
        if (node.kind == ParticleNodeKind::ParticleOutput) {
            outputNode = &node;
            ++outputNodeCount;
        }
    }
    if (outputNodeCount == 0) {
        result.errorMessage = "graph has no Particle Output node -- nothing to generate a compute shader from";
        return result;
    }
    if (outputNodeCount > 1) {
        result.errorMessage = "graph has " + std::to_string(outputNodeCount) +
                               " Particle Output nodes -- exactly one is required (which one would win?)";
        return result;
    }

    std::ostringstream body;
    Resolver resolver(graph, body);
    std::string newPositionVar = resolver.resolvePin(*graph.findPin(outputNode->pinIds[0]));
    std::string newVelocityVar = resolver.hasError() ? std::string{} : resolver.resolvePin(*graph.findPin(outputNode->pinIds[1]));
    if (resolver.hasError()) {
        result.errorMessage = resolver.error();
        return result;
    }

    std::ostringstream out;
    out << "#version 450\n\n";
    // Kronos ("Real-Time GPU Particle Compute" Phase 1): a real,
    // deliberately bounded per-particle state -- position/velocity/age/
    // lifetime only (matches core::Particle's own position/velocity/
    // age/lifetime CPU fields, see core/ParticleSystem.hpp). Size/color
    // interpolation stays CPU-side (Particle::currentSize()/
    // currentColor()) for this phase -- a real, stated scope cut, not
    // an oversight, same "bounded node set, not the full future
    // catalogue" convention this file's own header comment states.
    out << "struct Particle {\n";
    out << "    vec3 position;\n";
    out << "    float age;\n";
    out << "    vec3 velocity;\n";
    out << "    float lifetime;\n";
    out << "};\n\n";
    out << "layout(local_size_x = " << localSizeX << ") in;\n\n";
    out << "layout(std430, binding = 0) buffer ParticleBuffer {\n";
    out << "    Particle particles[];\n";
    out << "};\n\n";
    out << "layout(push_constant) uniform PushConstants {\n";
    out << "    float deltaTime;\n";
    out << "    uint particleCount;\n";
    out << "} pc;\n\n";
    out << "// Generated by Kronos Studio's Particle Compute Graph -- see studio/ParticleComputeCodegen.cpp.\n";
    out << "void main() {\n";
    out << "    uint idx = gl_GlobalInvocationID.x;\n";
    out << "    if (idx >= pc.particleCount) return;\n\n";
    out << "    vec3 position = particles[idx].position;\n";
    out << "    vec3 velocity = particles[idx].velocity;\n";
    out << "    float age = particles[idx].age;\n";
    out << "    float deltaTime = pc.deltaTime;\n\n";
    out << body.str();
    out << "\n    particles[idx].position = " << newPositionVar << ";\n";
    out << "    particles[idx].velocity = " << newVelocityVar << ";\n";
    out << "}\n";

    result.success = true;
    result.glsl = out.str();
    return result;
}

} // namespace engine::studio
