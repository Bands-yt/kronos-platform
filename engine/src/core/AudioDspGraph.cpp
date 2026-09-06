#include "core/AudioDspGraph.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace engine::core {

const char* audioNodeKindName(AudioNodeKind kind) {
    switch (kind) {
        case AudioNodeKind::Input: return "Input";
        case AudioNodeKind::Gain: return "Gain";
        case AudioNodeKind::BiquadFilter: return "Filter";
        case AudioNodeKind::PitchShift: return "Pitch Shift";
        case AudioNodeKind::Slice: return "Slice";
        case AudioNodeKind::GraphOutput: return "Output";
    }
    return "Unknown";
}

namespace {

struct PinSpec {
    bool isOutput;
    const char* label;
};

std::vector<PinSpec> pinLayoutFor(AudioNodeKind kind) {
    switch (kind) {
        case AudioNodeKind::Input: return {{true, "Signal"}};
        case AudioNodeKind::Gain: return {{false, "In"}, {true, "Out"}};
        case AudioNodeKind::BiquadFilter: return {{false, "In"}, {true, "Out"}};
        case AudioNodeKind::PitchShift: return {{false, "In"}, {true, "Out"}};
        case AudioNodeKind::Slice: return {{false, "In"}, {true, "Out"}};
        case AudioNodeKind::GraphOutput: return {{false, "In"}};
    }
    return {};
}

// Real RBJ Audio EQ Cookbook biquad -- Direct Form I, zero-initial
// state (a real, honest transient at buffer start, same as any biquad
// processing a buffer with no prior history). See AudioDspGraph.hpp's
// own AudioNodeKind::BiquadFilter comment for why this exact
// derivation, not an approximation.
std::vector<float> applyBiquad(const std::vector<float>& in, bool highPass, float cutoffHz, float q, uint32_t sampleRate) {
    if (sampleRate == 0 || in.empty()) return in;

    float nyquist = static_cast<float>(sampleRate) * 0.5f;
    float clampedCutoff = std::clamp(cutoffHz, 1.0f, nyquist - 1.0f);
    float w0 = 2.0f * 3.14159265358979323846f * clampedCutoff / static_cast<float>(sampleRate);
    float cosW0 = std::cos(w0);
    float sinW0 = std::sin(w0);
    float alpha = sinW0 / (2.0f * std::max(q, 0.01f));

    float b0, b1, b2, a0, a1, a2;
    if (highPass) {
        b0 = (1.0f + cosW0) * 0.5f;
        b1 = -(1.0f + cosW0);
        b2 = (1.0f + cosW0) * 0.5f;
    } else {
        b0 = (1.0f - cosW0) * 0.5f;
        b1 = 1.0f - cosW0;
        b2 = (1.0f - cosW0) * 0.5f;
    }
    a0 = 1.0f + alpha;
    a1 = -2.0f * cosW0;
    a2 = 1.0f - alpha;
    b0 /= a0;
    b1 /= a0;
    b2 /= a0;
    a1 /= a0;
    a2 /= a0;

    std::vector<float> out(in.size());
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    for (size_t i = 0; i < in.size(); ++i) {
        float x0 = in[i];
        float y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        out[i] = y0;
        x2 = x1;
        x1 = x0;
        y2 = y1;
        y1 = y0;
    }
    return out;
}

// Real, honest linear-interpolation resample -- see this file's own
// header comment on why this changes duration along with pitch.
// `pitchRatio` <= 0 is clamped to a small positive epsilon rather than
// producing a divide-by-zero/infinite-length buffer.
std::vector<float> applyPitchShift(const std::vector<float>& in, float pitchRatio) {
    if (in.empty()) return in;
    float ratio = std::max(pitchRatio, 0.01f);
    size_t outLength = static_cast<size_t>(std::max(1.0, std::round(static_cast<double>(in.size()) / ratio)));

    std::vector<float> out(outLength);
    for (size_t i = 0; i < outLength; ++i) {
        double srcPos = static_cast<double>(i) * ratio;
        size_t i0 = static_cast<size_t>(srcPos);
        float frac = static_cast<float>(srcPos - static_cast<double>(i0));
        float s0 = i0 < in.size() ? in[i0] : in.back();
        float s1 = (i0 + 1) < in.size() ? in[i0 + 1] : in.back();
        out[i] = s0 + (s1 - s0) * frac;
    }
    return out;
}

// Real, clamped sub-range extraction in milliseconds. A degenerate
// range (after clamping, start >= end) is a real, honest empty buffer
// -- silence, not an error -- matching this codebase's "an
// out-of-range/degenerate request produces an honest empty/default
// result" convention (e.g. Mesh::faceVertexIndices() on an invalid
// index).
std::vector<float> applySlice(const std::vector<float>& in, float startMs, float endMs, uint32_t sampleRate) {
    if (in.empty() || sampleRate == 0) return {};
    auto msToSample = [&](float ms) -> long long {
        double s = static_cast<double>(ms) / 1000.0 * static_cast<double>(sampleRate);
        return static_cast<long long>(std::llround(s));
    };
    long long startSample = std::clamp<long long>(msToSample(startMs), 0, static_cast<long long>(in.size()));
    long long endSample = std::clamp<long long>(msToSample(endMs), 0, static_cast<long long>(in.size()));
    if (startSample >= endSample) return {};
    return std::vector<float>(in.begin() + startSample, in.begin() + endSample);
}

// Real recursive resolver, memoized by node id with real cycle
// detection -- the exact same shape studio::ShaderGraphCodegen.cpp's
// Resolver already establishes, applied here to real sample buffers
// instead of generated GLSL text: emitNode() runs the actual DSP math
// immediately rather than emitting source.
class Resolver {
public:
    Resolver(const AudioDspGraph& graph, const std::vector<float>& inputSamples, uint32_t sampleRate)
        : graph_(graph), inputSamples_(inputSamples), sampleRate_(sampleRate) {}

    std::vector<float> resolvePin(const AudioPin& pin) {
        if (hasError()) return {};
        if (pin.isOutput) return resolveNodeOutput(pin.nodeId);

        const AudioLink* link = graph_.findLinkInto(pin.id);
        if (link == nullptr) {
            // Real, honest failure -- unlike a shader math node (where 0
            // is a sane default for an unassigned input), a DSP chain
            // with a genuinely disconnected input has no sane silent
            // default: passing silence through a Gain/Filter/PitchShift
            // node would be a real, wrong answer, not a helpful no-op.
            error_ = "node " + std::to_string(pin.nodeId) + "'s input '" + pin.label + "' is not connected to anything";
            return {};
        }
        const AudioPin* sourcePin = graph_.findPin(link->outputPinId);
        if (sourcePin == nullptr) {
            error_ = "internal error: link references a pin that no longer exists";
            return {};
        }
        return resolveNodeOutput(sourcePin->nodeId);
    }

    [[nodiscard]] bool hasError() const { return !error_.empty(); }
    [[nodiscard]] const std::string& error() const { return error_; }

private:
    std::vector<float> resolveNodeOutput(int nodeId) {
        if (hasError()) return {};
        auto memoIt = memo_.find(nodeId);
        if (memoIt != memo_.end()) return memoIt->second;

        if (visiting_.count(nodeId) != 0) {
            error_ = "graph has a cycle involving node " + std::to_string(nodeId);
            return {};
        }
        const AudioNode* node = graph_.findNode(nodeId);
        if (node == nullptr) {
            error_ = "internal error: node " + std::to_string(nodeId) + " not found";
            return {};
        }
        visiting_.insert(nodeId);
        std::vector<float> value = emitNode(*node);
        visiting_.erase(nodeId);
        if (!hasError()) memo_[nodeId] = value;
        return value;
    }

    const AudioPin& inputPin(const AudioNode& node, size_t index) { return *graph_.findPin(node.pinIds[index]); }

    std::vector<float> emitNode(const AudioNode& node) {
        switch (node.kind) {
            case AudioNodeKind::Input:
                return inputSamples_;
            case AudioNodeKind::Gain: {
                std::vector<float> in = resolvePin(inputPin(node, 0));
                if (hasError()) return {};
                for (float& s : in) s *= node.gainLinear;
                return in;
            }
            case AudioNodeKind::BiquadFilter: {
                std::vector<float> in = resolvePin(inputPin(node, 0));
                if (hasError()) return {};
                return applyBiquad(in, node.filterIsHighPass, node.cutoffHz, node.q, sampleRate_);
            }
            case AudioNodeKind::PitchShift: {
                std::vector<float> in = resolvePin(inputPin(node, 0));
                if (hasError()) return {};
                return applyPitchShift(in, node.pitchRatio);
            }
            case AudioNodeKind::Slice: {
                std::vector<float> in = resolvePin(inputPin(node, 0));
                if (hasError()) return {};
                return applySlice(in, node.sliceStartMs, node.sliceEndMs, sampleRate_);
            }
            case AudioNodeKind::GraphOutput:
                error_ = "internal error: GraphOutput cannot be a codegen dependency (it must be the graph's one real sink)";
                return {};
        }
        error_ = "internal error: unhandled AudioNodeKind";
        return {};
    }

    const AudioDspGraph& graph_;
    const std::vector<float>& inputSamples_;
    uint32_t sampleRate_;
    std::map<int, std::vector<float>> memo_;
    std::set<int> visiting_;
    std::string error_;
};

} // namespace

int AudioDspGraph::addNode(AudioNodeKind kind) {
    AudioNode node;
    node.id = nextId_++;
    node.kind = kind;

    for (const PinSpec& spec : pinLayoutFor(kind)) {
        AudioPin pin;
        pin.id = nextId_++;
        pin.nodeId = node.id;
        pin.isOutput = spec.isOutput;
        pin.label = spec.label;
        node.pinIds.push_back(pin.id);
        pins_.push_back(pin);
    }

    nodes_.push_back(node);
    return node.id;
}

void AudioDspGraph::removeNode(int nodeId) {
    const AudioNode* node = findNode(nodeId);
    if (node == nullptr) return;
    std::vector<int> pinIds = node->pinIds;

    links_.erase(std::remove_if(links_.begin(), links_.end(),
                                 [&](const AudioLink& link) {
                                     return std::find(pinIds.begin(), pinIds.end(), link.outputPinId) != pinIds.end() ||
                                            std::find(pinIds.begin(), pinIds.end(), link.inputPinId) != pinIds.end();
                                 }),
                 links_.end());
    pins_.erase(std::remove_if(pins_.begin(), pins_.end(), [&](const AudioPin& pin) { return pin.nodeId == nodeId; }),
                pins_.end());
    nodes_.erase(std::remove_if(nodes_.begin(), nodes_.end(), [&](const AudioNode& n) { return n.id == nodeId; }),
                 nodes_.end());
}

bool AudioDspGraph::addLink(int outputPinId, int inputPinId, std::string& outError) {
    const AudioPin* outputPin = findPin(outputPinId);
    const AudioPin* inputPin = findPin(inputPinId);
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
    if (findLinkInto(inputPinId) != nullptr) {
        outError = "this input already has an incoming connection -- remove it first";
        return false;
    }
    if (outputPin->nodeId == inputPin->nodeId) {
        outError = "cannot connect a node to itself";
        return false;
    }

    AudioLink link;
    link.id = nextId_++;
    link.outputPinId = outputPinId;
    link.inputPinId = inputPinId;
    links_.push_back(link);
    return true;
}

void AudioDspGraph::removeLink(int linkId) {
    links_.erase(std::remove_if(links_.begin(), links_.end(), [&](const AudioLink& link) { return link.id == linkId; }),
                 links_.end());
}

AudioNode* AudioDspGraph::findNode(int nodeId) {
    for (auto& node : nodes_) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const AudioNode* AudioDspGraph::findNode(int nodeId) const {
    for (const auto& node : nodes_) {
        if (node.id == nodeId) return &node;
    }
    return nullptr;
}

const AudioPin* AudioDspGraph::findPin(int pinId) const {
    for (const auto& pin : pins_) {
        if (pin.id == pinId) return &pin;
    }
    return nullptr;
}

const AudioLink* AudioDspGraph::findLinkInto(int inputPinId) const {
    for (const auto& link : links_) {
        if (link.inputPinId == inputPinId) return &link;
    }
    return nullptr;
}

AudioDspProcessResult AudioDspGraph::process() const {
    AudioDspProcessResult result;
    if (inputSamples_.empty()) {
        result.errorMessage = "no input buffer set -- call setInputBuffer() first";
        return result;
    }

    const AudioNode* outputNode = nullptr;
    int outputCount = 0;
    for (const AudioNode& node : nodes_) {
        if (node.kind == AudioNodeKind::GraphOutput) {
            outputNode = &node;
            ++outputCount;
        }
    }
    if (outputCount == 0) {
        result.errorMessage = "graph has no Output node -- nothing to generate a processed buffer from";
        return result;
    }
    if (outputCount > 1) {
        result.errorMessage =
            "graph has " + std::to_string(outputCount) + " Output nodes -- exactly one is required (which one would win?)";
        return result;
    }

    Resolver resolver(*this, inputSamples_, sampleRate_);
    std::vector<float> processed = resolver.resolvePin(*findPin(outputNode->pinIds[0]));
    if (resolver.hasError()) {
        result.errorMessage = resolver.error();
        return result;
    }

    result.success = true;
    result.samples = std::move(processed);
    return result;
}

} // namespace engine::core
