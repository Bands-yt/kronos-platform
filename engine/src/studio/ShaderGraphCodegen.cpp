#include "studio/ShaderGraphCodegen.hpp"

#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <vector>

#include "studio/ShaderGraph.hpp"

namespace engine::studio {

namespace {

std::string formatFloat(float value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6f", static_cast<double>(value));
    return buffer;
}

std::string formatConstant(ShaderDataType type, const float value[4]) {
    switch (type) {
        case ShaderDataType::Float: return formatFloat(value[0]);
        case ShaderDataType::Vec2: return "vec2(" + formatFloat(value[0]) + ", " + formatFloat(value[1]) + ")";
        case ShaderDataType::Vec3:
            return "vec3(" + formatFloat(value[0]) + ", " + formatFloat(value[1]) + ", " + formatFloat(value[2]) + ")";
        case ShaderDataType::Vec4:
            return "vec4(" + formatFloat(value[0]) + ", " + formatFloat(value[1]) + ", " + formatFloat(value[2]) + ", " +
                   formatFloat(value[3]) + ")";
    }
    return "0.0";
}

// Literal used for an unconnected input pin. Mostly zero, but operands
// where zero would make the node useless default to something neutral.
float defaultInputValue(ShaderNodeKind kind, int pinIndex) {
    switch (kind) {
        case ShaderNodeKind::DivideFloat: return pinIndex == 1 ? 1.0f : 0.0f;
        case ShaderNodeKind::Power: return pinIndex == 1 ? 1.0f : 0.0f;
        case ShaderNodeKind::Smoothstep: return pinIndex == 1 ? 1.0f : 0.0f;
        case ShaderNodeKind::LerpVec4: return pinIndex == 2 ? 0.5f : 0.0f;
        case ShaderNodeKind::ScaleVec4:
        case ShaderNodeKind::ScaleVec3:
        case ShaderNodeKind::Noise:
        case ShaderNodeKind::Checker: return pinIndex == 1 ? 1.0f : 0.0f;
        case ShaderNodeKind::Fresnel: return 5.0f;
        case ShaderNodeKind::CombineVec4: return pinIndex == 3 ? 1.0f : 0.0f;
        default: return 0.0f;
    }
}

enum class Mode { Standalone, Surface };

struct Usage {
    bool worldPosition = false;
    bool worldNormal = false;
    bool uv = false;
    bool texture = false;
    bool noise = false;
    bool checker = false;
};

constexpr const char* kNoiseHelpers = R"(float kgHash(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float kgNoise(vec3 x) {
    vec3 i = floor(x);
    vec3 f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(kgHash(i), kgHash(i + vec3(1, 0, 0)), f.x),
                   mix(kgHash(i + vec3(0, 1, 0)), kgHash(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(kgHash(i + vec3(0, 0, 1)), kgHash(i + vec3(1, 0, 1)), f.x),
                   mix(kgHash(i + vec3(0, 1, 1)), kgHash(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}

)";

constexpr const char* kCheckerHelper = R"(float kgChecker(vec3 p) {
    vec3 cell = floor(p + 1e-4);
    return mod(cell.x + cell.y + cell.z, 2.0);
}

)";

// Depth-first, memoized per node (a node feeding several consumers is
// emitted once), with cycle detection.
class Resolver {
public:
    Resolver(const ShaderGraph& graph, Mode mode, Usage& usage, std::ostringstream& body)
        : graph_(graph), mode_(mode), usage_(usage), body_(body) {}

    std::string resolveInput(const ShaderNode& node, int pinIndex) {
        if (hasError()) return {};
        const ShaderPin* pin = graph_.findPin(node.pinIds[static_cast<size_t>(pinIndex)]);
        const ShaderLink* link = graph_.findLinkInto(pin->id);
        if (link == nullptr) {
            const float value = defaultInputValue(node.kind, pinIndex);
            const float values[4] = {value, value, value, value};
            return formatConstant(pin->type, values);
        }
        return resolveOutputPin(link->outputPinId);
    }

    [[nodiscard]] bool isConnected(const ShaderNode& node, int pinIndex) const {
        return graph_.findLinkInto(node.pinIds[static_cast<size_t>(pinIndex)]) != nullptr;
    }

    [[nodiscard]] bool hasError() const { return !error_.empty(); }
    [[nodiscard]] const std::string& error() const { return error_; }

private:
    std::string resolveOutputPin(int pinId) {
        const ShaderPin* pin = graph_.findPin(pinId);
        if (pin == nullptr) {
            error_ = "internal error: link references a pin that no longer exists";
            return {};
        }
        const ShaderNode* node = graph_.findNode(pin->nodeId);
        if (node == nullptr) {
            error_ = "internal error: node " + std::to_string(pin->nodeId) + " not found";
            return {};
        }
        const std::vector<std::string>& outputs = resolveNode(*node);
        if (hasError()) return {};
        int outputIndex = 0;
        for (int id : node->pinIds) {
            const ShaderPin* candidate = graph_.findPin(id);
            if (candidate == nullptr || !candidate->isOutput) continue;
            if (id == pinId) return outputs[static_cast<size_t>(outputIndex)];
            ++outputIndex;
        }
        error_ = "internal error: pin " + std::to_string(pinId) + " is not an output";
        return {};
    }

    const std::vector<std::string>& resolveNode(const ShaderNode& node) {
        static const std::vector<std::string> kEmpty;
        auto memo = memo_.find(node.id);
        if (memo != memo_.end()) return memo->second;
        if (visiting_.count(node.id) != 0) {
            error_ = "graph has a cycle involving node " + std::to_string(node.id);
            return kEmpty;
        }
        visiting_.insert(node.id);
        std::vector<std::string> outputs = emit(node);
        visiting_.erase(node.id);
        if (hasError()) return kEmpty;
        return memo_[node.id] = std::move(outputs);
    }

    std::string temp(ShaderDataType type, const std::string& expr) {
        std::string name = "t" + std::to_string(nextTemp_++);
        body_ << "    " << shaderDataTypeGlslTypeName(type) << " " << name << " = " << expr << ";\n";
        return name;
    }

    std::string viewDirection() {
        usage_.worldPosition = true;
        return mode_ == Mode::Surface ? "normalize(scene.viewPositionWS.xyz - inWorldPos)"
                                      : "normalize(vec3(0.0, 0.0, 3.0) - inWorldPos)";
    }

    std::vector<std::string> emit(const ShaderNode& node) {
        using K = ShaderNodeKind;
        constexpr ShaderDataType F = ShaderDataType::Float;
        constexpr ShaderDataType V3 = ShaderDataType::Vec3;
        constexpr ShaderDataType V4 = ShaderDataType::Vec4;
        auto in = [&](int index) { return resolveInput(node, index); };
        auto binary = [&](ShaderDataType type, const char* op) -> std::vector<std::string> {
            std::string a = in(0);
            std::string b = in(1);
            if (hasError()) return {};
            return {temp(type, a + " " + op + " " + b)};
        };
        auto unary = [&](ShaderDataType type, const std::string& prefix, const std::string& suffix) -> std::vector<std::string> {
            std::string a = in(0);
            if (hasError()) return {};
            return {temp(type, prefix + a + suffix)};
        };

        switch (node.kind) {
            case K::InputWorldPosition: usage_.worldPosition = true; return {temp(V3, "inWorldPos")};
            case K::InputWorldNormal: usage_.worldNormal = true; return {temp(V3, "normalize(inWorldNormal)")};
            case K::InputUV: usage_.uv = true; return {temp(ShaderDataType::Vec2, "inUV")};
            case K::InputTime: return {temp(F, mode_ == Mode::Surface ? "scene.cloudParams.w" : "0.0")};
            case K::InputViewDirection: return {temp(V3, viewDirection())};
            case K::MaterialColor: return {temp(V4, mode_ == Mode::Surface ? "kgMaterial" : "vec4(1.0)")};
            case K::ConstantFloat: return {temp(F, formatConstant(F, node.constantValue))};
            case K::ConstantVec3: return {temp(V3, formatConstant(V3, node.constantValue))};
            case K::ConstantVec4: return {temp(V4, formatConstant(V4, node.constantValue))};
            case K::AddFloat: return binary(F, "+");
            case K::AddVec4: return binary(V4, "+");
            case K::SubtractFloat: return binary(F, "-");
            case K::MultiplyFloat: return binary(F, "*");
            case K::MultiplyVec4: return binary(V4, "*");
            case K::ScaleVec4: return binary(V4, "*");
            case K::ScaleVec3: return binary(V3, "*");
            case K::DivideFloat: {
                std::string a = in(0);
                std::string b = in(1);
                if (hasError()) return {};
                return {temp(F, a + " / (abs(" + b + ") < 1e-6 ? 1e-6 : " + b + ")")};
            }
            case K::Sine: return unary(F, "sin(", ")");
            case K::OneMinus: return unary(F, "1.0 - ", "");
            case K::Saturate: return unary(F, "clamp(", ", 0.0, 1.0)");
            case K::Vec3ToVec4: return unary(V4, "vec4(", ", 1.0)");
            case K::Vec4ToVec3: return unary(V3, "", ".xyz");
            case K::Power: {
                std::string a = in(0);
                std::string b = in(1);
                if (hasError()) return {};
                return {temp(F, "pow(max(" + a + ", 0.0), " + b + ")")};
            }
            case K::Smoothstep: {
                std::string e0 = in(0);
                std::string e1 = in(1);
                std::string x = in(2);
                if (hasError()) return {};
                return {temp(F, "smoothstep(" + e0 + ", " + e1 + ", " + x + ")")};
            }
            case K::LerpVec4: {
                std::string a = in(0);
                std::string b = in(1);
                std::string t = in(2);
                if (hasError()) return {};
                return {temp(V4, "mix(" + a + ", " + b + ", " + t + ")")};
            }
            case K::Fresnel: {
                std::string power = in(0);
                if (hasError()) return {};
                usage_.worldNormal = true;
                return {temp(F, "pow(1.0 - clamp(dot(normalize(inWorldNormal), " + viewDirection() + "), 0.0, 1.0), " +
                                    power + ")")};
            }
            case K::Noise:
            case K::Checker: {
                std::string position = in(0);
                std::string scale = in(1);
                if (hasError()) return {};
                const bool noise = node.kind == K::Noise;
                (noise ? usage_.noise : usage_.checker) = true;
                return {temp(F, std::string(noise ? "kgNoise(" : "kgChecker(") + position + " * " + scale + ")")};
            }
            case K::TextureSample: {
                std::string uv = in(0);
                if (hasError()) return {};
                usage_.texture = true;
                return {temp(V4, std::string(mode_ == Mode::Surface ? "texture(ALBEDO_TEX, " : "texture(materialAlbedo, ") +
                                     uv + ")")};
            }
            case K::SplitVec2:
            case K::SplitVec3:
            case K::SplitVec4: {
                std::string v = in(0);
                if (hasError()) return {};
                const int count = node.kind == K::SplitVec2 ? 2 : node.kind == K::SplitVec3 ? 3 : 4;
                std::vector<std::string> outputs;
                for (int i = 0; i < count; ++i) outputs.push_back(temp(F, v + "." + "xyzw"[i]));
                return outputs;
            }
            case K::CombineVec4: {
                std::string r = in(0);
                std::string g = in(1);
                std::string b = in(2);
                std::string a = in(3);
                if (hasError()) return {};
                return {temp(V4, "vec4(" + r + ", " + g + ", " + b + ", " + a + ")")};
            }
            case K::PbrOutput:
                error_ = "PBR Output cannot feed another node";
                return {};
        }
        error_ = "internal error: unhandled node kind";
        return {};
    }

    const ShaderGraph& graph_;
    Mode mode_;
    Usage& usage_;
    std::ostringstream& body_;
    std::map<int, std::vector<std::string>> memo_;
    std::set<int> visiting_;
    int nextTemp_ = 0;
    std::string error_;
};

const ShaderNode* findSingleOutput(const ShaderGraph& graph, std::string& error) {
    const ShaderNode* output = nullptr;
    int count = 0;
    for (const ShaderNode& node : graph.nodes()) {
        if (node.kind != ShaderNodeKind::PbrOutput) continue;
        output = &node;
        ++count;
    }
    if (count == 0) error = "graph has no PBR Output node -- nothing to generate a shader from";
    if (count > 1) error = "graph has " + std::to_string(count) + " PBR Output nodes -- exactly one is required";
    return count == 1 ? output : nullptr;
}

struct ResolvedOutputs {
    std::string baseColor, metallic, roughness, emissive;
};

bool resolveOutputs(Resolver& resolver, const ShaderNode& output, bool onlyConnected, ResolvedOutputs& out) {
    std::string* targets[] = {&out.baseColor, &out.metallic, &out.roughness, &out.emissive};
    for (int i = 0; i < 4; ++i) {
        if (onlyConnected && !resolver.isConnected(output, i)) continue;
        *targets[i] = resolver.resolveInput(output, i);
        if (resolver.hasError()) return false;
    }
    return true;
}

void writeHelpers(std::ostringstream& out, const Usage& usage) {
    if (usage.noise) out << kNoiseHelpers;
    if (usage.checker) out << kCheckerHelper;
}

} // namespace

ShaderGraphCodegenResult generateFragmentShaderGlsl(const ShaderGraph& graph) {
    ShaderGraphCodegenResult result;
    const ShaderNode* output = findSingleOutput(graph, result.errorMessage);
    if (output == nullptr) return result;

    Usage usage;
    std::ostringstream body;
    Resolver resolver(graph, Mode::Standalone, usage, body);
    ResolvedOutputs resolved;
    if (!resolveOutputs(resolver, *output, false, resolved)) {
        result.errorMessage = resolver.error();
        return result;
    }

    std::ostringstream out;
    out << "#version 450\n";
    if (usage.worldPosition) out << "layout(location = 0) in vec3 inWorldPos;\n";
    if (usage.worldNormal) out << "layout(location = 1) in vec3 inWorldNormal;\n";
    if (usage.uv) out << "layout(location = 2) in vec2 inUV;\n";
    if (usage.texture) out << "layout(set = 0, binding = 0) uniform sampler2D materialAlbedo;\n";
    out << "layout(location = 0) out vec4 outColor;\n\n";
    writeHelpers(out, usage);
    out << "void main() {\n" << body.str();
    out << "    outColor = " << resolved.baseColor << ";\n";
    out << "}\n";

    result.success = true;
    result.glsl = out.str();
    return result;
}

ShaderGraphCodegenResult generateSurfaceShaderGlsl(const ShaderGraph& graph, const SurfaceShaderTarget& target) {
    ShaderGraphCodegenResult result;
    const ShaderNode* output = findSingleOutput(graph, result.errorMessage);
    if (output == nullptr) return result;

    Usage usage;
    std::ostringstream body;
    Resolver resolver(graph, Mode::Surface, usage, body);
    ResolvedOutputs resolved;
    if (!resolveOutputs(resolver, *output, true, resolved)) {
        result.errorMessage = resolver.error();
        return result;
    }

    std::ostringstream out;
    if (target.rayTracing) {
        out << "#version 460\n"
               "#extension GL_EXT_ray_query : require\n"
               "#extension GL_EXT_buffer_reference : require\n"
               "#extension GL_EXT_buffer_reference_uvec2 : require\n";
    } else {
        out << "#version 450\n";
    }
    if (target.bindless) out << "#extension GL_EXT_nonuniform_qualifier : require\n#define KRONOS_BINDLESS\n";
    if (target.rayTracing) out << "#define KRONOS_RAY_TRACING\n";
    out << "#define KRONOS_SURFACE_GRAPH\n";
    out << "#include \"kronos/forward_main.glsl\"\n\n";
    writeHelpers(out, usage);
    out << "void kronosSurfaceGraph(inout vec3 albedo, inout float metallic, inout float roughness, inout vec3 emissive) {\n";
    out << "    vec4 kgMaterial = vec4(albedo, inBaseColor.a);\n";
    out << body.str();
    if (!resolved.baseColor.empty()) out << "    albedo = max(" << resolved.baseColor << ".rgb, vec3(0.0));\n";
    if (!resolved.metallic.empty()) out << "    metallic = " << resolved.metallic << ";\n";
    if (!resolved.roughness.empty()) out << "    roughness = " << resolved.roughness << ";\n";
    if (!resolved.emissive.empty()) out << "    emissive = max(" << resolved.emissive << ", vec3(0.0));\n";
    out << "}\n";

    result.success = true;
    result.glsl = out.str();
    return result;
}

} // namespace engine::studio
