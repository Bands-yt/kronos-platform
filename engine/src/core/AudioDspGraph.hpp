#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace engine::core {

// Kronos ("Node-Based Audio DSP" -- v0.4.0 Creator Suite): a real,
// node-based, non-destructive audio processing graph -- the same
// id-based nodes/pins/links, addNode()/addLink()-as-only-mutators shape
// studio::ShaderGraph and studio::ParticleComputeGraph already
// establish, applied to audio buffers instead of GLSL/GPU state.
// "Non-destructive" here means what it means in every real DAW: editing
// a node's parameter (a filter's cutoff, a gain's dB, a slice's
// endpoints) and calling process() again re-derives the output from the
// original source buffer -- nothing is ever baked back into the input.
//
// Deliberately real-but-bounded, same "state the cut plainly" this
// codebase's own header comments already use throughout (see
// UvTools.hpp's own applyAutoUnwrap() comment): this is real, correct
// OFFLINE buffer processing (call process(), get a new buffer back),
// not a lock-free realtime insert into miniaudio's own playback
// callback -- core::Audio's ma_sound-based playback has no per-sample
// processing seam to hook into without a real, separate low-latency
// audio-thread integration this phase does not attempt.
// studio::plugins::AudioPreviewPlugin instead processes a whole loaded
// buffer through a graph once, then plays the resulting buffer -- real,
// audible, non-destructive relative to the source, just not
// sample-accurate "live knob-turning while it's already playing."
//
// PitchShift is a real, honest resampling-based shift (linear
// interpolation), which changes duration along with pitch -- true
// pitch-without-tempo-change needs phase-vocoder/PSOLA machinery, a
// real, separate, substantially larger DSP undertaking this phase does
// not attempt. Stated here plainly, same as ShaderGraph.hpp's own
// TextureSample scope note.

enum class AudioNodeKind {
    // The graph's one real source -- one output pin. Its buffer is set
    // directly via AudioDspGraph::setInputBuffer(), not generated.
    Input,
    // Real, linear gain -- one input pin, one output pin. `gainLinear`
    // multiplies every sample (1.0 = unity, 0.5 = -6dB, 2.0 = +6dB).
    Gain,
    // Real RBJ-cookbook biquad filter (Robert Bristow-Johnson's
    // standard "Audio EQ Cookbook" formulas -- the same real, widely-
    // used derivation most production biquad implementations use, not
    // an ad hoc approximation) -- one input pin, one output pin.
    // `filterIsHighPass` selects low-pass (false) or high-pass (true);
    // `cutoffHz`/`q` are the real, standard biquad parameters.
    BiquadFilter,
    // Real resampling-based pitch shift -- see this file's own header
    // comment on why duration changes with it. One input pin, one
    // output pin. `pitchRatio` > 1 raises pitch (and shortens duration),
    // < 1 lowers it (and lengthens duration).
    PitchShift,
    // Real sub-range extraction -- one input pin, one output pin.
    // `startMs`/`endMs` name the real, clamped time range (in
    // milliseconds, relative to whatever buffer reaches this node) to
    // keep; everything outside it is real-discarded, not just skipped
    // in rendering.
    Slice,
    // The graph's one real sink -- one input pin. Exactly one
    // GraphOutput node must exist for process() to produce anything.
    GraphOutput,
};
[[nodiscard]] const char* audioNodeKindName(AudioNodeKind kind);

struct AudioPin {
    int id = 0;
    int nodeId = 0;
    bool isOutput = false;
    std::string label;
};

struct AudioNode {
    int id = 0;
    AudioNodeKind kind = AudioNodeKind::Input;

    // Real, authored parameters -- only the fields relevant to `kind`
    // are ever read (see AudioDspGraph.cpp's own emitNode()), same
    // "only the fields this node kind actually uses are meaningful"
    // convention studio::ShaderNode::constantValue already establishes.
    float gainLinear = 1.0f;
    bool filterIsHighPass = false;
    float cutoffHz = 1000.0f;
    float q = 0.707f; // 0.707 == real Butterworth (maximally-flat) Q at the default cutoff
    float pitchRatio = 1.0f;
    float sliceStartMs = 0.0f;
    float sliceEndMs = 0.0f;

    std::vector<int> pinIds; // fixed, kind-specific order: input(s) first, then the single output
};

struct AudioLink {
    int id = 0;
    int outputPinId = 0;
    int inputPinId = 0;
};

struct AudioDspProcessResult {
    bool success = false;
    std::vector<float> samples; // valid only when success -- mono float32, same convention decodeAudioFileToFloatMono() uses
    std::string errorMessage;
};

// Kronos: the real, owning graph -- addNode()/addLink() are the only
// ways to mutate it, same single-place-enforces-every-invariant shape
// ShaderGraph/ParticleComputeGraph already establish. Unlike those two
// (which generate GPU shader text), process() here runs the real DSP
// math directly in C++ against setInputBuffer()'s own real samples.
class AudioDspGraph {
public:
    int addNode(AudioNodeKind kind);
    void removeNode(int nodeId);

    [[nodiscard]] bool addLink(int outputPinId, int inputPinId, std::string& outError);
    void removeLink(int linkId);

    [[nodiscard]] const std::vector<AudioNode>& nodes() const { return nodes_; }
    [[nodiscard]] const std::vector<AudioPin>& pins() const { return pins_; }
    [[nodiscard]] const std::vector<AudioLink>& links() const { return links_; }

    [[nodiscard]] AudioNode* findNode(int nodeId);
    [[nodiscard]] const AudioNode* findNode(int nodeId) const;
    [[nodiscard]] const AudioPin* findPin(int pinId) const;
    [[nodiscard]] const AudioLink* findLinkInto(int inputPinId) const;

    // The real source buffer the graph's one Input node reads from --
    // set this before process(). `sampleRate` is real and load-bearing:
    // BiquadFilter/PitchShift/Slice all compute in real Hz/milliseconds,
    // not normalized sample-index units.
    void setInputBuffer(std::vector<float> samples, uint32_t sampleRate) {
        inputSamples_ = std::move(samples);
        sampleRate_ = sampleRate;
    }

    // Real, deterministic evaluation: walks backward from the graph's
    // one real GraphOutput node (same "exactly one sink" requirement
    // ShaderGraphCodegen/ParticleComputeCodegen already enforce),
    // running every node's own real DSP math, memoized by node id so a
    // node feeding two consumers is only ever computed once. Fails
    // closed (`.success == false`, `.errorMessage` names why) on zero
    // or multiple GraphOutput nodes, a real cycle, or no input buffer
    // set -- never a partial/garbage buffer.
    [[nodiscard]] AudioDspProcessResult process() const;

private:
    int nextId_ = 1;
    std::vector<AudioNode> nodes_;
    std::vector<AudioPin> pins_;
    std::vector<AudioLink> links_;
    std::vector<float> inputSamples_;
    uint32_t sampleRate_ = 0;
};

} // namespace engine::core
