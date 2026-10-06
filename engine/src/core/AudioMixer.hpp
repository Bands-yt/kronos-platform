#pragma once

#include <memory>
#include <string>
#include <vector>

struct ma_engine;
struct ma_sound;

namespace engine::core {

inline constexpr float kMixerFilterOpenLowpassHz = 20000.0f;
inline constexpr float kMixerFilterOpenHighpassHz = 20.0f;
inline constexpr float kMixerSilenceDb = -80.0f;

struct MixerSend {
    std::string target;
    float levelDb = 0.0f;
    bool preFader = false;
};

// Turns this bus down by amountDb while `trigger` is louder than thresholdDb.
struct MixerDuck {
    std::string trigger;
    float amountDb = -12.0f;
    float thresholdDb = -40.0f;
    float attackMs = 50.0f;
    float releaseMs = 400.0f;
};

struct MixerBus {
    std::string name;
    std::string parent; // empty only for the master bus
    float volumeDb = 0.0f;
    bool muted = false;
    float lowpassHz = kMixerFilterOpenLowpassHz;
    float highpassHz = kMixerFilterOpenHighpassHz;
    float reverbMix = 0.0f;
    float reverbRoom = 0.5f;
    float reverbDamp = 0.5f;
    std::vector<MixerSend> sends;
    std::vector<MixerDuck> ducks;
};

enum class MixerParam { Volume, Lowpass, Highpass, Reverb, Send };

struct MixerSnapshotValue {
    std::string bus;
    MixerParam param = MixerParam::Volume;
    std::string sendTarget; // for MixerParam::Send
    float value = 0.0f;
};

struct MixerSnapshot {
    std::string name;
    std::vector<MixerSnapshotValue> values;
};

struct MixerConfig {
    std::vector<MixerBus> buses;
    std::vector<MixerSnapshot> snapshots;

    // Master, with Music, Sounds, Voice and UI under it, and Music ducking under Voice.
    [[nodiscard]] static MixerConfig defaults();

    [[nodiscard]] int busIndex(const std::string& name) const;
    [[nodiscard]] MixerBus* bus(const std::string& name);
    [[nodiscard]] const MixerBus* bus(const std::string& name) const;
    [[nodiscard]] int snapshotIndex(const std::string& name) const;
    [[nodiscard]] int masterIndex() const;

    // Empty when the config can be built: one master, unique names, known
    // parents/targets/triggers, and no loops through parents and sends.
    [[nodiscard]] std::string validate() const;

    // Renames a bus and every reference to it.
    bool renameBus(const std::string& from, const std::string& to);
    // Removes a bus; its children move to its parent. The master can't be removed.
    bool removeBus(const std::string& name);

    [[nodiscard]] std::string serialize() const;
    [[nodiscard]] bool parse(const std::string& text, std::string* error = nullptr);
    [[nodiscard]] bool saveToFile(const std::string& path, std::string* error = nullptr) const;
    [[nodiscard]] bool loadFromFile(const std::string& path, std::string* error = nullptr);
};

[[nodiscard]] const char* mixerParamName(MixerParam param);
[[nodiscard]] float mixerDbToGain(float db);
[[nodiscard]] float mixerGainToDb(float gain);

struct MixerBusMeter {
    float peakDb = kMixerSilenceDb;
    float rmsDb = kMixerSilenceDb;
    float duckDb = 0.0f;
};

// Runs a MixerConfig as a graph of miniaudio nodes: every bus filters,
// reverbs, ducks and fades what is routed into it and feeds its parent and
// its sends. Parameters reach the audio thread without locks, and every
// gain change is ramped to avoid clicks.
class AudioMixer {
public:
    AudioMixer();
    ~AudioMixer();
    AudioMixer(const AudioMixer&) = delete;
    AudioMixer& operator=(const AudioMixer&) = delete;

    bool attach(ma_engine* engine, std::string* error = nullptr);
    void detach();
    [[nodiscard]] bool attached() const;

    // Rebuilds the graph when buses, sends or ducks changed; otherwise only
    // updates parameters. Sounds keep their bus (or fall back to the master
    // when it is gone). A config that fails validate() is refused.
    bool setConfig(const MixerConfig& config, std::string* error = nullptr);
    [[nodiscard]] const MixerConfig& config() const;

    // Routes a sound into a bus; an unknown bus means the master.
    void route(ma_sound* sound, const std::string& bus);
    void forget(ma_sound* sound);
    [[nodiscard]] std::string busOf(ma_sound* sound) const;

    // Fades a snapshot's intensity (0 = off, 1 = fully applied) over
    // fadeSeconds. Active snapshots apply in the order they were started.
    bool setSnapshot(const std::string& name, float intensity, float fadeSeconds = 0.0f);
    [[nodiscard]] float snapshotIntensity(const std::string& name) const;
    void clearSnapshots();

    // Player-side volume (settings menu) on top of the designed mix.
    void setUserVolume(const std::string& bus, float gain);
    [[nodiscard]] float userVolume(const std::string& bus) const;
    // Solo for auditioning in the editor: only soloed buses, the buses
    // under them, and their parents stay audible.
    void setSolo(const std::string& bus, bool solo);
    [[nodiscard]] bool solo(const std::string& bus) const;

    // Advances snapshot fades and pushes parameters to the audio thread.
    void update(float dt);

    // The value a parameter has right now, after snapshots.
    [[nodiscard]] float effectiveValue(const std::string& bus, MixerParam param, const std::string& sendTarget = {}) const;
    [[nodiscard]] MixerBusMeter meter(const std::string& bus) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace engine::core
