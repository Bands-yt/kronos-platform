#include "core/AudioMixer.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace engine::core {

float mixerDbToGain(float db) { return db <= kMixerSilenceDb ? 0.0f : std::pow(10.0f, db / 20.0f); }

float mixerGainToDb(float gain) {
    return gain <= 0.0001f ? kMixerSilenceDb : std::max(kMixerSilenceDb, 20.0f * std::log10(gain));
}

const char* mixerParamName(MixerParam param) {
    switch (param) {
        case MixerParam::Volume: return "volume";
        case MixerParam::Lowpass: return "lowpass";
        case MixerParam::Highpass: return "highpass";
        case MixerParam::Reverb: return "reverb";
        case MixerParam::Send: return "send";
    }
    return "volume";
}

// --- MixerConfig ---------------------------------------------------------

MixerConfig MixerConfig::defaults() {
    MixerConfig config;
    auto add = [&](const char* name, const char* parent) -> MixerBus& {
        MixerBus bus;
        bus.name = name;
        bus.parent = parent;
        config.buses.push_back(bus);
        return config.buses.back();
    };
    add("Master", "");
    add("Music", "Master").ducks.push_back(MixerDuck{"Voice", -9.0f, -45.0f, 80.0f, 600.0f});
    add("SFX", "Master");
    add("Voice", "Master");
    add("UI", "Master");

    MixerSnapshot paused{"Paused", {}};
    paused.values.push_back({"SFX", MixerParam::Volume, {}, -20.0f});
    paused.values.push_back({"Music", MixerParam::Lowpass, {}, 1500.0f});
    paused.values.push_back({"Music", MixerParam::Volume, {}, -6.0f});
    config.snapshots.push_back(paused);

    MixerSnapshot underwater{"Underwater", {}};
    underwater.values.push_back({"SFX", MixerParam::Lowpass, {}, 700.0f});
    underwater.values.push_back({"Music", MixerParam::Lowpass, {}, 1200.0f});
    underwater.values.push_back({"Voice", MixerParam::Lowpass, {}, 1800.0f});
    underwater.values.push_back({"SFX", MixerParam::Reverb, {}, 0.35f});
    config.snapshots.push_back(underwater);
    return config;
}

int MixerConfig::busIndex(const std::string& name) const {
    for (size_t i = 0; i < buses.size(); ++i) {
        if (buses[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

MixerBus* MixerConfig::bus(const std::string& name) {
    const int index = busIndex(name);
    return index < 0 ? nullptr : &buses[index];
}

const MixerBus* MixerConfig::bus(const std::string& name) const {
    const int index = busIndex(name);
    return index < 0 ? nullptr : &buses[index];
}

int MixerConfig::snapshotIndex(const std::string& name) const {
    for (size_t i = 0; i < snapshots.size(); ++i) {
        if (snapshots[i].name == name) return static_cast<int>(i);
    }
    return -1;
}

int MixerConfig::masterIndex() const {
    for (size_t i = 0; i < buses.size(); ++i) {
        if (buses[i].parent.empty()) return static_cast<int>(i);
    }
    return -1;
}

std::string MixerConfig::validate() const {
    if (buses.empty()) return "the mixer has no buses";
    std::set<std::string> names;
    int masters = 0;
    for (const MixerBus& bus : buses) {
        if (bus.name.empty()) return "a bus has no name";
        if (!names.insert(bus.name).second) return "two buses are called \"" + bus.name + "\"";
        if (bus.parent.empty()) ++masters;
    }
    if (masters != 1) return "the mixer needs exactly one bus without a parent (the master)";
    if (buses.size() > 200) return "too many buses";
    for (const MixerBus& bus : buses) {
        if (!bus.parent.empty() && !names.count(bus.parent)) {
            return "\"" + bus.name + "\" goes into \"" + bus.parent + "\", which doesn't exist";
        }
        if (bus.sends.size() > 32) return "\"" + bus.name + "\" has too many sends";
        std::set<std::string> targets;
        for (const MixerSend& send : bus.sends) {
            if (!names.count(send.target)) return "\"" + bus.name + "\" sends to \"" + send.target + "\", which doesn't exist";
            if (send.target == bus.name) return "\"" + bus.name + "\" sends to itself";
            if (!targets.insert(send.target).second) return "\"" + bus.name + "\" sends to \"" + send.target + "\" twice";
        }
        for (const MixerDuck& duck : bus.ducks) {
            if (!names.count(duck.trigger)) return "\"" + bus.name + "\" ducks under \"" + duck.trigger + "\", which doesn't exist";
            if (duck.trigger == bus.name) return "\"" + bus.name + "\" can't duck under itself";
        }
    }

    std::map<std::string, std::vector<std::string>> edges;
    for (const MixerBus& bus : buses) {
        if (!bus.parent.empty()) edges[bus.name].push_back(bus.parent);
        for (const MixerSend& send : bus.sends) edges[bus.name].push_back(send.target);
    }
    std::map<std::string, int> state;
    std::string loopAt;
    std::function<bool(const std::string&)> visit = [&](const std::string& name) {
        int& s = state[name];
        if (s == 1) {
            loopAt = name;
            return false;
        }
        if (s == 2) return true;
        s = 1;
        for (const std::string& next : edges[name]) {
            if (!visit(next)) return false;
        }
        state[name] = 2;
        return true;
    };
    for (const MixerBus& bus : buses) {
        if (!visit(bus.name)) return "sound would loop back into \"" + loopAt + "\"";
    }

    std::set<std::string> snapshotNames;
    for (const MixerSnapshot& snapshot : snapshots) {
        if (snapshot.name.empty()) return "a snapshot has no name";
        if (!snapshotNames.insert(snapshot.name).second) return "two snapshots are called \"" + snapshot.name + "\"";
        for (const MixerSnapshotValue& value : snapshot.values) {
            const MixerBus* target = bus(value.bus);
            if (!target) return "snapshot \"" + snapshot.name + "\" changes \"" + value.bus + "\", which doesn't exist";
            if (value.param == MixerParam::Send &&
                std::none_of(target->sends.begin(), target->sends.end(),
                             [&](const MixerSend& send) { return send.target == value.sendTarget; })) {
                return "snapshot \"" + snapshot.name + "\" changes a send from \"" + value.bus + "\" to \"" +
                       value.sendTarget + "\", which doesn't exist";
            }
        }
    }
    return {};
}

bool MixerConfig::renameBus(const std::string& from, const std::string& to) {
    if (to.empty() || busIndex(from) < 0 || (from != to && busIndex(to) >= 0)) return false;
    for (MixerBus& bus : buses) {
        if (bus.name == from) bus.name = to;
        if (bus.parent == from) bus.parent = to;
        for (MixerSend& send : bus.sends) {
            if (send.target == from) send.target = to;
        }
        for (MixerDuck& duck : bus.ducks) {
            if (duck.trigger == from) duck.trigger = to;
        }
    }
    for (MixerSnapshot& snapshot : snapshots) {
        for (MixerSnapshotValue& value : snapshot.values) {
            if (value.bus == from) value.bus = to;
            if (value.sendTarget == from) value.sendTarget = to;
        }
    }
    return true;
}

bool MixerConfig::removeBus(const std::string& name) {
    const int index = busIndex(name);
    if (index < 0 || buses[index].parent.empty()) return false;
    const std::string parent = buses[index].parent;
    buses.erase(buses.begin() + index);
    for (MixerBus& bus : buses) {
        if (bus.parent == name) bus.parent = parent;
        std::erase_if(bus.sends, [&](const MixerSend& send) { return send.target == name; });
        std::erase_if(bus.ducks, [&](const MixerDuck& duck) { return duck.trigger == name; });
    }
    for (MixerSnapshot& snapshot : snapshots) {
        std::erase_if(snapshot.values, [&](const MixerSnapshotValue& value) {
            return value.bus == name || (value.param == MixerParam::Send && value.sendTarget == name);
        });
    }
    return true;
}

namespace {

std::string quote(const std::string& text) {
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n' || c == '\r') continue;
        out += c;
    }
    return out + "\"";
}

std::string number(float value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << value;
    return out.str();
}

bool tokenize(const std::string& line, std::vector<std::string>& tokens) {
    tokens.clear();
    size_t i = 0;
    while (i < line.size()) {
        if (std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
            continue;
        }
        std::string token;
        if (line[i] == '"') {
            ++i;
            bool closed = false;
            while (i < line.size()) {
                if (line[i] == '\\' && i + 1 < line.size()) {
                    token += line[i + 1];
                    i += 2;
                } else if (line[i] == '"') {
                    ++i;
                    closed = true;
                    break;
                } else {
                    token += line[i++];
                }
            }
            if (!closed) return false;
        } else {
            while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) token += line[i++];
        }
        tokens.push_back(token);
    }
    return true;
}

bool toFloat(const std::string& text, float& out) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    in >> out;
    return !in.fail() && in.eof() && std::isfinite(out);
}

bool paramFromName(const std::string& name, MixerParam& out) {
    for (MixerParam param : {MixerParam::Volume, MixerParam::Lowpass, MixerParam::Highpass, MixerParam::Reverb,
                             MixerParam::Send}) {
        if (name == mixerParamName(param)) {
            out = param;
            return true;
        }
    }
    return false;
}

} // namespace

std::string MixerConfig::serialize() const {
    std::string out = "kronos-mixer 1\n";
    for (const MixerBus& bus : buses) {
        out += "bus " + quote(bus.name) + " parent " + quote(bus.parent) + " volume " + number(bus.volumeDb) +
               " mute " + (bus.muted ? "1" : "0") + " lowpass " + number(bus.lowpassHz) + " highpass " +
               number(bus.highpassHz) + " reverb " + number(bus.reverbMix) + " " + number(bus.reverbRoom) + " " +
               number(bus.reverbDamp) + "\n";
    }
    for (const MixerBus& bus : buses) {
        for (const MixerSend& send : bus.sends) {
            out += "send " + quote(bus.name) + " " + quote(send.target) + " " + number(send.levelDb) +
                   (send.preFader ? " pre\n" : " post\n");
        }
        for (const MixerDuck& duck : bus.ducks) {
            out += "duck " + quote(bus.name) + " " + quote(duck.trigger) + " " + number(duck.amountDb) + " " +
                   number(duck.thresholdDb) + " " + number(duck.attackMs) + " " + number(duck.releaseMs) + "\n";
        }
    }
    for (const MixerSnapshot& snapshot : snapshots) {
        out += "snapshot " + quote(snapshot.name) + "\n";
        for (const MixerSnapshotValue& value : snapshot.values) {
            out += "value " + quote(snapshot.name) + " " + quote(value.bus) + " " + mixerParamName(value.param);
            if (value.param == MixerParam::Send) out += " " + quote(value.sendTarget);
            out += " " + number(value.value) + "\n";
        }
    }
    return out;
}

bool MixerConfig::parse(const std::string& text, std::string* error) {
    MixerConfig parsed;
    std::istringstream in(text);
    std::string line;
    std::vector<std::string> t;
    int lineNumber = 0;
    bool sawHeader = false;
    auto fail = [&](const std::string& why) {
        if (error) *error = "line " + std::to_string(lineNumber) + ": " + why;
        return false;
    };
    while (std::getline(in, line)) {
        ++lineNumber;
        if (!tokenize(line, t)) return fail("unclosed quote");
        if (t.empty() || t[0][0] == '#') continue;
        if (!sawHeader) {
            if (t.size() != 2 || t[0] != "kronos-mixer") return fail("not a Kronos mixer file");
            if (t[1] != "1") return fail("made by a newer Kronos (mixer format " + t[1] + ")");
            sawHeader = true;
            continue;
        }
        if (t[0] == "bus") {
            if (t.size() < 2) return fail("bus without a name");
            MixerBus bus;
            bus.name = t[1];
            for (size_t i = 2; i < t.size();) {
                const std::string& key = t[i];
                const size_t arity = key == "reverb" ? 3 : 1;
                if (i + arity >= t.size()) return fail("\"" + key + "\" is missing its value");
                if (key == "parent") {
                    bus.parent = t[i + 1];
                } else if (key == "mute") {
                    bus.muted = t[i + 1] == "1";
                } else {
                    float values[3] = {};
                    for (size_t v = 0; v < arity; ++v) {
                        if (!toFloat(t[i + 1 + v], values[v])) return fail("\"" + t[i + 1 + v] + "\" isn't a number");
                    }
                    if (key == "volume") {
                        bus.volumeDb = values[0];
                    } else if (key == "lowpass") {
                        bus.lowpassHz = values[0];
                    } else if (key == "highpass") {
                        bus.highpassHz = values[0];
                    } else if (key == "reverb") {
                        bus.reverbMix = values[0];
                        bus.reverbRoom = values[1];
                        bus.reverbDamp = values[2];
                    } else {
                        return fail("unknown bus setting \"" + key + "\"");
                    }
                }
                i += 1 + arity;
            }
            parsed.buses.push_back(bus);
        } else if (t[0] == "send") {
            MixerSend send;
            if (t.size() != 5 || !toFloat(t[3], send.levelDb) || (t[4] != "pre" && t[4] != "post")) {
                return fail("expected: send \"from\" \"to\" <dB> pre|post");
            }
            MixerBus* from = parsed.bus(t[1]);
            if (!from) return fail("send from unknown bus \"" + t[1] + "\"");
            send.target = t[2];
            send.preFader = t[4] == "pre";
            from->sends.push_back(send);
        } else if (t[0] == "duck") {
            MixerDuck duck;
            if (t.size() != 7 || !toFloat(t[3], duck.amountDb) || !toFloat(t[4], duck.thresholdDb) ||
                !toFloat(t[5], duck.attackMs) || !toFloat(t[6], duck.releaseMs)) {
                return fail("expected: duck \"bus\" \"trigger\" <dB> <threshold dB> <attack ms> <release ms>");
            }
            MixerBus* bus = parsed.bus(t[1]);
            if (!bus) return fail("duck on unknown bus \"" + t[1] + "\"");
            duck.trigger = t[2];
            bus->ducks.push_back(duck);
        } else if (t[0] == "snapshot") {
            if (t.size() != 2) return fail("expected: snapshot \"name\"");
            parsed.snapshots.push_back(MixerSnapshot{t[1], {}});
        } else if (t[0] == "value") {
            MixerSnapshotValue value;
            if (t.size() < 5 || !paramFromName(t[3], value.param)) return fail("expected: value \"snapshot\" \"bus\" <setting> <value>");
            const int snapshot = parsed.snapshotIndex(t[1]);
            if (snapshot < 0) return fail("value for unknown snapshot \"" + t[1] + "\"");
            value.bus = t[2];
            size_t at = 4;
            if (value.param == MixerParam::Send) value.sendTarget = t[at++];
            if (t.size() != at + 1 || !toFloat(t[at], value.value)) return fail("bad snapshot value");
            parsed.snapshots[snapshot].values.push_back(value);
        } else {
            return fail("unknown line \"" + t[0] + "\"");
        }
    }
    if (!sawHeader) {
        if (error) *error = "not a Kronos mixer file";
        return false;
    }
    const std::string problem = parsed.validate();
    if (!problem.empty()) {
        if (error) *error = problem;
        return false;
    }
    *this = std::move(parsed);
    return true;
}

bool MixerConfig::saveToFile(const std::string& path, std::string* error) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error) *error = "couldn't write " + path;
        return false;
    }
    out << serialize();
    out.close();
    if (!out) {
        if (error) *error = "couldn't write " + path;
        return false;
    }
    return true;
}

bool MixerConfig::loadFromFile(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "couldn't open " + path;
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    return parse(buffer.str(), error);
}

// --- Audio-thread DSP ---------------------------------------------------------

namespace {

constexpr uint32_t kMaxChannels = 8;

struct Biquad {
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1[kMaxChannels] = {};
    float z2[kMaxChannels] = {};
    float hz = -1.0f;

    void design(bool highpass, float cutoff, float sampleRate) {
        hz = cutoff;
        const float w = 2.0f * 3.14159265f * std::clamp(cutoff, 10.0f, sampleRate * 0.45f) / sampleRate;
        const float cosw = std::cos(w);
        const float alpha = std::sin(w) / (2.0f * 0.7071f);
        const float a0 = 1.0f + alpha;
        if (highpass) {
            b0 = (1.0f + cosw) * 0.5f / a0;
            b1 = -(1.0f + cosw) / a0;
        } else {
            b0 = (1.0f - cosw) * 0.5f / a0;
            b1 = (1.0f - cosw) / a0;
        }
        b2 = b0;
        a1 = -2.0f * cosw / a0;
        a2 = (1.0f - alpha) / a0;
    }
    void reset() {
        std::fill(std::begin(z1), std::end(z1), 0.0f);
        std::fill(std::begin(z2), std::end(z2), 0.0f);
    }
    void process(float* frames, uint32_t count, uint32_t channels) {
        for (uint32_t i = 0; i < count; ++i) {
            for (uint32_t c = 0; c < channels; ++c) {
                float& x = frames[i * channels + c];
                const float y = b0 * x + z1[c];
                z1[c] = b1 * x - a1 * y + z2[c];
                z2[c] = b2 * x - a2 * y;
                x = y;
            }
        }
    }
};

// A small Freeverb: four damped combs and two allpasses per channel.
struct Reverb {
    struct Comb {
        std::vector<float> buffer;
        size_t at = 0;
        float store = 0.0f;
    };
    struct Allpass {
        std::vector<float> buffer;
        size_t at = 0;
    };
    std::vector<Comb> combs;       // 4 per channel
    std::vector<Allpass> allpasses; // 2 per channel

    void init(uint32_t channels, uint32_t sampleRate) {
        static const int kComb[4] = {1116, 1188, 1277, 1356};
        static const int kAllpass[2] = {556, 441};
        const float scale = static_cast<float>(sampleRate) / 44100.0f;
        combs.assign(channels * 4, {});
        allpasses.assign(channels * 2, {});
        for (uint32_t c = 0; c < channels; ++c) {
            const int spread = (c % 2) * 23;
            for (int i = 0; i < 4; ++i) combs[c * 4 + i].buffer.assign(std::max(1, int((kComb[i] + spread) * scale)), 0.0f);
            for (int i = 0; i < 2; ++i) {
                allpasses[c * 2 + i].buffer.assign(std::max(1, int((kAllpass[i] + spread) * scale)), 0.0f);
            }
        }
    }
    float process(uint32_t channel, float input, float feedback, float damp) {
        float out = 0.0f;
        const float in = input * 0.03f;
        for (int i = 0; i < 4; ++i) {
            Comb& comb = combs[channel * 4 + i];
            const float y = comb.buffer[comb.at];
            comb.store = y * (1.0f - damp) + comb.store * damp;
            comb.buffer[comb.at] = in + comb.store * feedback;
            if (++comb.at == comb.buffer.size()) comb.at = 0;
            out += y;
        }
        for (int i = 0; i < 2; ++i) {
            Allpass& pass = allpasses[channel * 2 + i];
            const float buffered = pass.buffer[pass.at];
            const float y = -out + buffered;
            pass.buffer[pass.at] = out + buffered * 0.5f;
            if (++pass.at == pass.buffer.size()) pass.at = 0;
            out = y;
        }
        return out;
    }
};

struct BusNode;

struct DuckState {
    BusNode* trigger = nullptr;
    std::atomic<float> amountDb{0.0f};
    std::atomic<float> thresholdDb{0.0f};
    std::atomic<float> attackMs{50.0f};
    std::atomic<float> releaseMs{400.0f};
    float envelopeDb = 0.0f;
};

struct SendState {
    std::atomic<float> gain{0.0f};
    float current = 0.0f;
    bool preFader = false;
};

struct BusNode {
    ma_node_base base; // must stay first: miniaudio casts the node to this
    uint32_t channels = 2;
    uint32_t sampleRate = 48000;

    std::atomic<float> gain{1.0f};
    std::atomic<float> lowpassHz{kMixerFilterOpenLowpassHz};
    std::atomic<float> highpassHz{kMixerFilterOpenHighpassHz};
    std::atomic<float> reverbMix{0.0f};
    std::atomic<float> reverbRoom{0.5f};
    std::atomic<float> reverbDamp{0.5f};
    std::unique_ptr<SendState[]> sends;
    uint32_t sendCount = 0;
    std::unique_ptr<DuckState[]> ducks;
    uint32_t duckCount = 0;

    float currentGain = 1.0f;
    Biquad lowpass;
    Biquad highpass;
    Reverb reverb;

    std::atomic<float> levelDb{kMixerSilenceDb};
    std::atomic<float> peakHold{0.0f};
    std::atomic<float> duckDb{0.0f};
};

void writeRamped(const float* source, float* target, uint32_t frames, uint32_t channels, float from, float to) {
    const float step = frames > 0 ? (to - from) / static_cast<float>(frames) : 0.0f;
    float g = from;
    for (uint32_t i = 0; i < frames; ++i) {
        g += step;
        for (uint32_t c = 0; c < channels; ++c) target[i * channels + c] = source[i * channels + c] * g;
    }
}

void processBus(ma_node* node, const float** framesIn, ma_uint32*, float** framesOut, ma_uint32* frameCountOut) {
    auto* bus = reinterpret_cast<BusNode*>(node);
    const uint32_t frames = *frameCountOut;
    const uint32_t channels = bus->channels;
    const size_t samples = static_cast<size_t>(frames) * channels;
    float* main = framesOut[0];
    if (framesIn && framesIn[0]) {
        std::memcpy(main, framesIn[0], samples * sizeof(float));
    } else {
        std::memset(main, 0, samples * sizeof(float));
    }
    const auto sr = static_cast<float>(bus->sampleRate);

    const float highpass = bus->highpassHz.load(std::memory_order_relaxed);
    if (highpass > kMixerFilterOpenHighpassHz + 0.5f) {
        if (bus->highpass.hz < 0.0f) bus->highpass.reset();
        if (std::abs(bus->highpass.hz - highpass) > 0.01f) bus->highpass.design(true, highpass, sr);
        bus->highpass.process(main, frames, channels);
    } else {
        bus->highpass.hz = -1.0f;
    }
    const float lowpass = bus->lowpassHz.load(std::memory_order_relaxed);
    if (lowpass < kMixerFilterOpenLowpassHz - 0.5f) {
        if (bus->lowpass.hz < 0.0f) bus->lowpass.reset();
        if (std::abs(bus->lowpass.hz - lowpass) > 0.01f) bus->lowpass.design(false, lowpass, sr);
        bus->lowpass.process(main, frames, channels);
    } else {
        bus->lowpass.hz = -1.0f;
    }

    const float mix = std::clamp(bus->reverbMix.load(std::memory_order_relaxed), 0.0f, 1.0f);
    if (mix > 0.0001f) {
        const float feedback = 0.7f + 0.28f * std::clamp(bus->reverbRoom.load(std::memory_order_relaxed), 0.0f, 1.0f);
        const float damp = 0.4f * std::clamp(bus->reverbDamp.load(std::memory_order_relaxed), 0.0f, 1.0f);
        for (uint32_t i = 0; i < frames; ++i) {
            for (uint32_t c = 0; c < channels; ++c) {
                float& x = main[i * channels + c];
                x = x * (1.0f - mix) + bus->reverb.process(c, x, feedback, damp) * mix;
            }
        }
    }

    for (uint32_t s = 0; s < bus->sendCount; ++s) {
        SendState& send = bus->sends[s];
        if (!send.preFader) continue;
        const float target = send.gain.load(std::memory_order_relaxed);
        writeRamped(main, framesOut[1 + s], frames, channels, send.current, target);
        send.current = target;
    }

    const float blockMs = 1000.0f * static_cast<float>(frames) / sr;
    float duckTotal = 0.0f;
    for (uint32_t d = 0; d < bus->duckCount; ++d) {
        DuckState& duck = bus->ducks[d];
        const float level = duck.trigger->levelDb.load(std::memory_order_relaxed);
        const float target = level > duck.thresholdDb.load(std::memory_order_relaxed)
                                 ? std::min(0.0f, duck.amountDb.load(std::memory_order_relaxed))
                                 : 0.0f;
        const float time = target < duck.envelopeDb ? duck.attackMs.load(std::memory_order_relaxed)
                                                    : duck.releaseMs.load(std::memory_order_relaxed);
        duck.envelopeDb += (target - duck.envelopeDb) * (1.0f - std::exp(-blockMs / std::max(time, 1.0f)));
        duckTotal = std::min(duckTotal, duck.envelopeDb);
    }
    bus->duckDb.store(duckTotal, std::memory_order_relaxed);

    const float gain = bus->gain.load(std::memory_order_relaxed) * mixerDbToGain(duckTotal);
    writeRamped(main, main, frames, channels, bus->currentGain, gain);
    bus->currentGain = gain;

    float peak = 0.0f;
    double sumSquares = 0.0;
    for (size_t i = 0; i < samples; ++i) {
        peak = std::max(peak, std::abs(main[i]));
        sumSquares += static_cast<double>(main[i]) * main[i];
    }
    const float rms = samples > 0 ? static_cast<float>(std::sqrt(sumSquares / static_cast<double>(samples))) : 0.0f;
    bus->levelDb.store(mixerGainToDb(rms), std::memory_order_relaxed);
    if (peak > bus->peakHold.load(std::memory_order_relaxed)) bus->peakHold.store(peak, std::memory_order_relaxed);

    for (uint32_t s = 0; s < bus->sendCount; ++s) {
        SendState& send = bus->sends[s];
        if (send.preFader) continue;
        const float target = send.gain.load(std::memory_order_relaxed);
        writeRamped(main, framesOut[1 + s], frames, channels, send.current, target);
        send.current = target;
    }
}

ma_node_vtable kBusVtable = {processBus, nullptr, 1, MA_NODE_BUS_COUNT_UNKNOWN, MA_NODE_FLAG_CONTINUOUS_PROCESSING};

std::string structureKey(const MixerConfig& config) {
    std::string key;
    for (const MixerBus& bus : config.buses) {
        key += bus.name + '\x1f' + bus.parent + '\x1e';
        for (const MixerSend& send : bus.sends) key += "s" + send.target + (send.preFader ? "p" : "q") + '\x1e';
        for (const MixerDuck& duck : bus.ducks) key += "d" + duck.trigger + '\x1e';
        key += '\x1d';
    }
    return key;
}

struct Effective {
    float volumeDb = 0.0f;
    float lowpassHz = kMixerFilterOpenLowpassHz;
    float highpassHz = kMixerFilterOpenHighpassHz;
    float reverbMix = 0.0f;
    std::vector<float> sendDb;
};

float lerpLogHz(float from, float to, float t) {
    const float a = std::log2(std::max(from, 1.0f));
    const float b = std::log2(std::max(to, 1.0f));
    return std::exp2(a + (b - a) * t);
}

} // namespace

// --- AudioMixer -------------------------------------------------------------

struct AudioMixer::Impl {
    ma_engine* engine = nullptr;
    MixerConfig config = MixerConfig::defaults();
    std::string structure;
    std::vector<std::unique_ptr<BusNode>> nodes;
    std::unordered_map<ma_sound*, std::string> routes;

    struct Active {
        int snapshot = -1;
        std::string name;
        float intensity = 0.0f;
        float target = 0.0f;
        float rate = 0.0f;
    };
    std::vector<Active> active;
    std::map<std::string, float> userVolume;
    std::set<std::string> soloed;
    std::vector<Effective> effective;

    ma_node* nodeFor(const std::string& bus) {
        int index = config.busIndex(bus);
        if (index < 0) index = config.masterIndex();
        if (index < 0 || static_cast<size_t>(index) >= nodes.size()) return nullptr;
        return reinterpret_cast<ma_node*>(nodes[index].get());
    }

    void destroyNodes(std::vector<std::unique_ptr<BusNode>>& old) {
        for (auto& node : old) ma_node_detach_all_output_buses(reinterpret_cast<ma_node*>(node.get()));
        for (auto& node : old) ma_node_uninit(reinterpret_cast<ma_node*>(node.get()), nullptr);
        old.clear();
    }

    bool build(std::string* error) {
        std::vector<std::unique_ptr<BusNode>> fresh;
        const uint32_t channels = std::min(ma_engine_get_channels(engine), kMaxChannels);
        const uint32_t sampleRate = ma_engine_get_sample_rate(engine);
        if (ma_engine_get_channels(engine) > kMaxChannels) {
            if (error) *error = "the mixer supports up to 8 output channels";
            return false;
        }
        ma_node_graph* graph = ma_engine_get_node_graph(engine);
        for (const MixerBus& bus : config.buses) {
            auto node = std::make_unique<BusNode>();
            node->channels = channels;
            node->sampleRate = sampleRate;
            node->reverb.init(channels, sampleRate);
            node->sendCount = static_cast<uint32_t>(bus.sends.size());
            node->sends = std::make_unique<SendState[]>(std::max<size_t>(1, bus.sends.size()));
            for (size_t s = 0; s < bus.sends.size(); ++s) node->sends[s].preFader = bus.sends[s].preFader;
            node->duckCount = static_cast<uint32_t>(bus.ducks.size());
            node->ducks = std::make_unique<DuckState[]>(std::max<size_t>(1, bus.ducks.size()));

            std::vector<ma_uint32> outChannels(1 + bus.sends.size(), channels);
            ma_uint32 inChannels = channels;
            ma_node_config nodeConfig = ma_node_config_init();
            nodeConfig.vtable = &kBusVtable;
            nodeConfig.outputBusCount = static_cast<ma_uint32>(outChannels.size());
            nodeConfig.pInputChannels = &inChannels;
            nodeConfig.pOutputChannels = outChannels.data();
            if (ma_node_init(graph, &nodeConfig, nullptr, reinterpret_cast<ma_node*>(node.get())) != MA_SUCCESS) {
                for (auto& made : fresh) ma_node_uninit(reinterpret_cast<ma_node*>(made.get()), nullptr);
                if (error) *error = "couldn't create the mixer bus \"" + bus.name + "\"";
                return false;
            }
            fresh.push_back(std::move(node));
        }
        for (size_t i = 0; i < config.buses.size(); ++i) {
            for (size_t d = 0; d < config.buses[i].ducks.size(); ++d) {
                fresh[i]->ducks[d].trigger = fresh[config.busIndex(config.buses[i].ducks[d].trigger)].get();
            }
        }
        std::vector<std::unique_ptr<BusNode>> old = std::move(nodes);
        nodes = std::move(fresh);
        pushParams(true);

        for (size_t i = 0; i < config.buses.size(); ++i) {
            auto* node = reinterpret_cast<ma_node*>(nodes[i].get());
            const MixerBus& bus = config.buses[i];
            ma_node* parent = bus.parent.empty() ? ma_engine_get_endpoint(engine)
                                                 : reinterpret_cast<ma_node*>(nodes[config.busIndex(bus.parent)].get());
            ma_node_attach_output_bus(node, 0, parent, 0);
            for (size_t s = 0; s < bus.sends.size(); ++s) {
                ma_node_attach_output_bus(node, static_cast<ma_uint32>(1 + s),
                                          reinterpret_cast<ma_node*>(nodes[config.busIndex(bus.sends[s].target)].get()), 0);
            }
        }
        for (auto& [sound, bus] : routes) ma_node_attach_output_bus(sound, 0, nodeFor(bus), 0);
        destroyNodes(old);
        structure = structureKey(config);
        return true;
    }

    void computeEffective() {
        effective.assign(config.buses.size(), {});
        for (size_t i = 0; i < config.buses.size(); ++i) {
            const MixerBus& bus = config.buses[i];
            Effective& e = effective[i];
            e.volumeDb = bus.volumeDb;
            e.lowpassHz = bus.lowpassHz;
            e.highpassHz = bus.highpassHz;
            e.reverbMix = bus.reverbMix;
            for (const MixerSend& send : bus.sends) e.sendDb.push_back(send.levelDb);
        }
        for (const Active& a : active) {
            if (a.snapshot < 0 || a.intensity <= 0.0f) continue;
            const float t = std::min(a.intensity, 1.0f);
            for (const MixerSnapshotValue& value : config.snapshots[a.snapshot].values) {
                const int index = config.busIndex(value.bus);
                if (index < 0) continue;
                Effective& e = effective[index];
                switch (value.param) {
                    case MixerParam::Volume: e.volumeDb += (value.value - e.volumeDb) * t; break;
                    case MixerParam::Lowpass: e.lowpassHz = lerpLogHz(e.lowpassHz, value.value, t); break;
                    case MixerParam::Highpass: e.highpassHz = lerpLogHz(e.highpassHz, value.value, t); break;
                    case MixerParam::Reverb: e.reverbMix += (value.value - e.reverbMix) * t; break;
                    case MixerParam::Send: {
                        const auto& sends = config.buses[index].sends;
                        for (size_t s = 0; s < sends.size(); ++s) {
                            if (sends[s].target == value.sendTarget) e.sendDb[s] += (value.value - e.sendDb[s]) * t;
                        }
                        break;
                    }
                }
            }
        }
    }

    bool audibleUnderSolo(int index) const {
        if (soloed.empty()) return true;
        for (int at = index; at >= 0; at = config.busIndex(config.buses[at].parent)) {
            if (soloed.count(config.buses[at].name)) return true;
        }
        std::function<bool(const std::string&)> hasSoloedChild = [&](const std::string& name) {
            for (const MixerBus& bus : config.buses) {
                if (bus.parent != name) continue;
                if (soloed.count(bus.name) || hasSoloedChild(bus.name)) return true;
            }
            return false;
        };
        return hasSoloedChild(config.buses[index].name);
    }

    void pushParams(bool immediate) {
        computeEffective();
        if (nodes.size() != config.buses.size()) return;
        for (size_t i = 0; i < config.buses.size(); ++i) {
            const MixerBus& bus = config.buses[i];
            const Effective& e = effective[i];
            BusNode& node = *nodes[i];
            auto user = userVolume.find(bus.name);
            float gain = mixerDbToGain(e.volumeDb) * (user == userVolume.end() ? 1.0f : user->second);
            if (bus.muted || !audibleUnderSolo(static_cast<int>(i))) gain = 0.0f;
            node.gain.store(gain, std::memory_order_relaxed);
            node.lowpassHz.store(e.lowpassHz, std::memory_order_relaxed);
            node.highpassHz.store(e.highpassHz, std::memory_order_relaxed);
            node.reverbMix.store(e.reverbMix, std::memory_order_relaxed);
            node.reverbRoom.store(bus.reverbRoom, std::memory_order_relaxed);
            node.reverbDamp.store(bus.reverbDamp, std::memory_order_relaxed);
            for (size_t s = 0; s < bus.sends.size() && s < node.sendCount; ++s) {
                node.sends[s].gain.store(mixerDbToGain(e.sendDb[s]), std::memory_order_relaxed);
                if (immediate) node.sends[s].current = mixerDbToGain(e.sendDb[s]);
            }
            for (size_t d = 0; d < bus.ducks.size() && d < node.duckCount; ++d) {
                node.ducks[d].amountDb.store(bus.ducks[d].amountDb, std::memory_order_relaxed);
                node.ducks[d].thresholdDb.store(bus.ducks[d].thresholdDb, std::memory_order_relaxed);
                node.ducks[d].attackMs.store(bus.ducks[d].attackMs, std::memory_order_relaxed);
                node.ducks[d].releaseMs.store(bus.ducks[d].releaseMs, std::memory_order_relaxed);
            }
            if (immediate) node.currentGain = gain;
        }
    }
};

AudioMixer::AudioMixer() : impl_(std::make_unique<Impl>()) { impl_->computeEffective(); }

AudioMixer::~AudioMixer() { detach(); }

bool AudioMixer::attach(ma_engine* engine, std::string* error) {
    detach();
    if (!engine) {
        if (error) *error = "no audio engine";
        return false;
    }
    impl_->engine = engine;
    if (!impl_->build(error)) {
        impl_->engine = nullptr;
        return false;
    }
    return true;
}

void AudioMixer::detach() {
    if (!impl_->engine) return;
    for (auto& [sound, bus] : impl_->routes) {
        ma_node_attach_output_bus(sound, 0, ma_engine_get_endpoint(impl_->engine), 0);
    }
    impl_->destroyNodes(impl_->nodes);
    impl_->engine = nullptr;
    impl_->structure.clear();
}

bool AudioMixer::attached() const { return impl_->engine != nullptr; }

bool AudioMixer::setConfig(const MixerConfig& config, std::string* error) {
    const std::string problem = config.validate();
    if (!problem.empty()) {
        if (error) *error = problem;
        return false;
    }
    MixerConfig previous = std::move(impl_->config);
    impl_->config = config;
    for (auto& a : impl_->active) a.snapshot = impl_->config.snapshotIndex(a.name);
    std::erase_if(impl_->active, [](const Impl::Active& a) { return a.snapshot < 0; });
    if (impl_->engine && structureKey(impl_->config) != impl_->structure) {
        if (!impl_->build(error)) {
            impl_->config = std::move(previous);
            impl_->build(nullptr);
            return false;
        }
        return true;
    }
    impl_->pushParams(false);
    return true;
}

const MixerConfig& AudioMixer::config() const { return impl_->config; }

void AudioMixer::route(ma_sound* sound, const std::string& bus) {
    if (!sound) return;
    auto [it, inserted] = impl_->routes.try_emplace(sound, bus);
    if (!inserted) {
        if (it->second == bus) return;
        it->second = bus;
    }
    if (impl_->engine) {
        if (ma_node* node = impl_->nodeFor(bus)) ma_node_attach_output_bus(sound, 0, node, 0);
    }
}

void AudioMixer::forget(ma_sound* sound) {
    impl_->routes.erase(sound);
}

std::string AudioMixer::busOf(ma_sound* sound) const {
    auto it = impl_->routes.find(sound);
    if (it == impl_->routes.end()) return {};
    return impl_->config.busIndex(it->second) >= 0 ? it->second : impl_->config.buses[impl_->config.masterIndex()].name;
}

bool AudioMixer::setSnapshot(const std::string& name, float intensity, float fadeSeconds) {
    const int index = impl_->config.snapshotIndex(name);
    if (index < 0) return false;
    intensity = std::clamp(intensity, 0.0f, 1.0f);
    auto it = std::find_if(impl_->active.begin(), impl_->active.end(), [&](const Impl::Active& a) { return a.name == name; });
    if (it == impl_->active.end()) {
        if (intensity <= 0.0f) return true;
        impl_->active.push_back(Impl::Active{index, name, 0.0f, 0.0f, 0.0f});
        it = impl_->active.end() - 1;
    }
    it->target = intensity;
    if (fadeSeconds <= 0.0f) {
        it->intensity = intensity;
        it->rate = 0.0f;
    } else {
        it->rate = std::abs(intensity - it->intensity) / fadeSeconds;
    }
    if (it->intensity <= 0.0f && it->target <= 0.0f) impl_->active.erase(it);
    impl_->pushParams(false);
    return true;
}

float AudioMixer::snapshotIntensity(const std::string& name) const {
    for (const auto& a : impl_->active) {
        if (a.name == name) return a.intensity;
    }
    return 0.0f;
}

void AudioMixer::clearSnapshots() {
    impl_->active.clear();
    impl_->pushParams(false);
}

void AudioMixer::setUserVolume(const std::string& bus, float gain) {
    impl_->userVolume[bus] = std::max(0.0f, gain);
    impl_->pushParams(false);
}

float AudioMixer::userVolume(const std::string& bus) const {
    auto it = impl_->userVolume.find(bus);
    return it == impl_->userVolume.end() ? 1.0f : it->second;
}

void AudioMixer::setSolo(const std::string& bus, bool solo) {
    if (solo) {
        impl_->soloed.insert(bus);
    } else {
        impl_->soloed.erase(bus);
    }
    impl_->pushParams(false);
}

bool AudioMixer::solo(const std::string& bus) const { return impl_->soloed.count(bus) > 0; }

void AudioMixer::update(float dt) {
    for (auto& a : impl_->active) {
        if (a.intensity == a.target) continue;
        if (a.rate <= 0.0f) {
            a.intensity = a.target;
            continue;
        }
        const float step = a.rate * std::max(dt, 0.0f);
        a.intensity = a.intensity < a.target ? std::min(a.target, a.intensity + step) : std::max(a.target, a.intensity - step);
    }
    std::erase_if(impl_->active, [](const Impl::Active& a) { return a.intensity <= 0.0f && a.target <= 0.0f; });
    impl_->pushParams(false);
}

float AudioMixer::effectiveValue(const std::string& bus, MixerParam param, const std::string& sendTarget) const {
    const int index = impl_->config.busIndex(bus);
    if (index < 0 || static_cast<size_t>(index) >= impl_->effective.size()) return 0.0f;
    const Effective& e = impl_->effective[index];
    switch (param) {
        case MixerParam::Volume: return e.volumeDb;
        case MixerParam::Lowpass: return e.lowpassHz;
        case MixerParam::Highpass: return e.highpassHz;
        case MixerParam::Reverb: return e.reverbMix;
        case MixerParam::Send: {
            const auto& sends = impl_->config.buses[index].sends;
            for (size_t s = 0; s < sends.size(); ++s) {
                if (sends[s].target == sendTarget) return e.sendDb[s];
            }
            return kMixerSilenceDb;
        }
    }
    return 0.0f;
}

MixerBusMeter AudioMixer::meter(const std::string& bus) const {
    MixerBusMeter meter;
    const int index = impl_->config.busIndex(bus);
    if (index < 0 || static_cast<size_t>(index) >= impl_->nodes.size()) return meter;
    BusNode& node = *impl_->nodes[index];
    meter.rmsDb = node.levelDb.load(std::memory_order_relaxed);
    meter.peakDb = mixerGainToDb(node.peakHold.exchange(0.0f, std::memory_order_relaxed));
    meter.duckDb = node.duckDb.load(std::memory_order_relaxed);
    return meter;
}

} // namespace engine::core
