#include "studio/plugins/AudioMixerPlugin.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include <imgui.h>

#include "studio/FileBrowse.hpp"

namespace engine::studio::plugins {

namespace {

constexpr float kStripWidth = 84.0f;
constexpr float kFaderHeight = 150.0f;
constexpr float kFaderMinDb = -60.0f;
constexpr float kFaderMaxDb = 12.0f;

const char* kParamLabels[] = {"Volume", "Low-pass", "High-pass", "Reverb", "Send"};

float meterFraction(float db) { return std::clamp((db - kFaderMinDb) / (kFaderMaxDb - kFaderMinDb), 0.0f, 1.0f); }

void drawMeter(ImDrawList* draw, ImVec2 min, ImVec2 max, float rmsDb, float peakDb, float duckDb) {
    draw->AddRectFilled(min, max, IM_COL32(20, 22, 26, 255), 2.0f);
    const float height = max.y - min.y;
    const float rmsTop = max.y - height * meterFraction(rmsDb);
    const ImU32 color = rmsDb > -1.0f ? IM_COL32(235, 80, 70, 255) : rmsDb > -12.0f ? IM_COL32(235, 200, 70, 255)
                                                                                 : IM_COL32(80, 200, 120, 255);
    if (rmsTop < max.y - 1.0f) draw->AddRectFilled(ImVec2(min.x + 1, rmsTop), ImVec2(max.x - 1, max.y - 1), color, 1.0f);
    if (peakDb > kFaderMinDb) {
        const float peakY = max.y - height * meterFraction(peakDb);
        draw->AddLine(ImVec2(min.x + 1, peakY), ImVec2(max.x - 1, peakY), IM_COL32(240, 240, 240, 220), 1.5f);
    }
    const float zeroY = max.y - height * meterFraction(0.0f);
    draw->AddLine(ImVec2(min.x - 2, zeroY), ImVec2(max.x + 2, zeroY), IM_COL32(255, 255, 255, 60));
    if (duckDb < -0.5f) {
        const float duckHeight = height * std::min(1.0f, -duckDb / (kFaderMaxDb - kFaderMinDb));
        draw->AddRectFilled(ImVec2(min.x + 1, min.y + 1), ImVec2(max.x - 1, min.y + 1 + duckHeight),
                            IM_COL32(245, 160, 70, 200), 1.0f);
    }
}

bool frequencySlider(const char* label, float& hz, bool lowpass) {
    const bool off = lowpass ? hz >= core::kMixerFilterOpenLowpassHz - 0.5f : hz <= core::kMixerFilterOpenHighpassHz + 0.5f;
    return ImGui::SliderFloat(label, &hz, core::kMixerFilterOpenHighpassHz, core::kMixerFilterOpenLowpassHz,
                              off ? "Off" : "%.0f Hz", ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_AlwaysClamp);
}

} // namespace

AudioMixerPlugin::AudioMixerPlugin(core::Audio& audio, std::function<std::string()> projectDirectory,
                                   std::function<bool()> playing)
    : audio_(audio), projectDirectory_(std::move(projectDirectory)), playing_(std::move(playing)) {
    open_ = false;
}

std::string AudioMixerPlugin::mixerPath() const {
    const std::string directory = projectDirectory_ ? projectDirectory_() : std::string();
    if (directory.empty()) return {};
    return (std::filesystem::path(directory) / "mixer.kmixer").string();
}

void AudioMixerPlugin::reload() {
    config_ = core::MixerConfig::defaults();
    const std::string path = mixerPath();
    std::error_code ec;
    if (!path.empty() && std::filesystem::exists(path, ec)) {
        std::string error;
        if (!config_.loadFromFile(path, &error)) {
            config_ = core::MixerConfig::defaults();
            status_ = "Couldn't read mixer.kmixer (" + error + "), using the default mixer.";
        } else {
            status_ = "Loaded " + path;
        }
    }
    dirty_ = false;
    error_.clear();
    if (config_.busIndex(selectedBus_) < 0) selectedBus_ = config_.buses[config_.masterIndex()].name;
    selectedSnapshot_ = -1;
    apply();
}

bool AudioMixerPlugin::save(std::string* error) {
    const std::string path = mixerPath();
    if (path.empty()) {
        if (error) *error = "Save the project first: the mixer is saved next to it.";
        return false;
    }
    if (!error_.empty()) {
        if (error) *error = "Fix the mixer first: " + error_;
        return false;
    }
    if (!config_.saveToFile(path, error)) return false;
    dirty_ = false;
    return true;
}

void AudioMixerPlugin::apply() {
    std::string error;
    if (audio_.mixer().setConfig(config_, &error)) {
        error_.clear();
    } else {
        error_ = error;
    }
}

void AudioMixerPlugin::update(float dt, core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    const std::string directory = projectDirectory_ ? projectDirectory_() : std::string();
    if (directory != loadedDirectory_) {
        loadedDirectory_ = directory;
        reload();
    }
    const bool playing = playing_ && playing_();
    if (!playing && error_.empty() && audio_.mixer().config().serialize() != config_.serialize()) apply();

    for (const core::MixerBus& bus : config_.buses) {
        const core::MixerBusMeter live = audio_.mixer().meter(bus.name);
        Meter& shown = meters_[bus.name];
        shown.rmsDb = std::max(live.rmsDb, shown.rmsDb - 30.0f * dt);
        shown.peakDb = std::max(live.peakDb, shown.peakDb - 12.0f * dt);
        shown.duckDb = live.duckDb;
    }
}

void AudioMixerPlugin::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::SetNextWindowSize(ImVec2(780.0f, 760.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Mixer", &open_)) {
        ImGui::End();
        return;
    }
    if (!audio_.isInitialized()) ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "No audio device: meters stay empty.");
    drawToolbar();
    drawStrips();
    ImGui::Separator();
    if (ImGui::BeginTabBar("##mixertabs")) {
        if (ImGui::BeginTabItem("Bus")) {
            drawBusDetails();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Snapshots")) {
            drawSnapshots();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Test Sounds")) {
            drawTestSounds();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void AudioMixerPlugin::drawToolbar() {
    const std::string path = mixerPath();
    ImGui::BeginDisabled(path.empty());
    if (ImGui::Button("Save")) {
        std::string error;
        status_ = save(&error) ? "Saved " + path : "Not saved: " + error;
    }
    ImGui::EndDisabled();
    if (path.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Save the project first: the mixer is saved next to it as mixer.kmixer.");
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) reload();
    ImGui::SameLine();
    if (ImGui::Button("Reset to Default")) {
        config_ = core::MixerConfig::defaults();
        selectedBus_ = "Master";
        selectedSnapshot_ = -1;
        dirty_ = true;
        apply();
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Bus")) {
        std::string name = "Bus";
        for (int i = 2; config_.busIndex(name) >= 0; ++i) name = "Bus " + std::to_string(i);
        core::MixerBus bus;
        bus.name = name;
        bus.parent = config_.buses[config_.masterIndex()].name;
        config_.buses.push_back(bus);
        selectedBus_ = name;
        dirty_ = true;
        apply();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s%s", dirty_ ? "Unsaved changes. " : "", path.empty() ? "No project open." : "");
    if (!error_.empty()) ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "Not applied: %s", error_.c_str());
    if (!status_.empty()) ImGui::TextDisabled("%s", status_.c_str());
    if (playing_ && playing_()) {
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f),
                           "Playing: faders show your saved mix. Scripts change the live mix, and Stop resets it.");
    }
}

void AudioMixerPlugin::drawStrips() {
    const float height = kFaderHeight + ImGui::GetFrameHeightWithSpacing() * 3.0f + ImGui::GetStyle().WindowPadding.y * 2.0f + ImGui::GetStyle().ScrollbarSize;
    ImGui::BeginChild("##strips", ImVec2(0.0f, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (size_t i = 0; i < config_.buses.size(); ++i) {
        core::MixerBus& bus = config_.buses[i];
        ImGui::PushID(static_cast<int>(i));
        if (i > 0) ImGui::SameLine();
        ImGui::BeginGroup();
        const bool selected = bus.name == selectedBus_;
        if (ImGui::Selectable(bus.name.c_str(), selected, 0, ImVec2(kStripWidth, 0.0f))) selectedBus_ = bus.name;

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::VSliderFloat("##fader", ImVec2(26.0f, kFaderHeight), &bus.volumeDb, kFaderMinDb, kFaderMaxDb, "",
                            ImGuiSliderFlags_None);
        if (ImGui::IsItemClicked()) selectedBus_ = bus.name;
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) bus.volumeDb = 0.0f;
        if (ImGui::IsItemEdited() || (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))) {
            dirty_ = true;
            apply();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%.1f dB (double-click for 0 dB)", bus.volumeDb);
        const Meter& meter = meters_[bus.name];
        const ImVec2 meterMin(origin.x + 34.0f, origin.y);
        drawMeter(draw, meterMin, ImVec2(meterMin.x + 12.0f, meterMin.y + kFaderHeight), meter.rmsDb, meter.peakDb,
                  meter.duckDb);
        ImGui::Text("%+.1f dB", bus.volumeDb);
        const bool muted = bus.muted;
        if (muted) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.3f, 0.25f, 1.0f));
        if (ImGui::SmallButton("M")) {
            bus.muted = !bus.muted;
            dirty_ = true;
            apply();
        }
        if (muted) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mute (saved with the mixer)");
        ImGui::SameLine();
        const bool solo = audio_.mixer().solo(bus.name);
        if (solo) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.7f, 0.2f, 1.0f));
        if (ImGui::SmallButton("S")) audio_.mixer().setSolo(bus.name, !solo);
        if (solo) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Solo: hear only this bus while editing (not saved)");
        if (meter.duckDb < -0.5f) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.96f, 0.63f, 0.27f, 1.0f), "%.0f", meter.duckDb);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Being ducked by %.1f dB", -meter.duckDb);
        }
        ImGui::TextDisabled("%s", bus.parent.empty() ? "output" : ("> " + bus.parent).c_str());
        ImGui::EndGroup();
        const ImVec2 groupMax = ImGui::GetItemRectMax();
        if (selected) {
            draw->AddRect(ImVec2(ImGui::GetItemRectMin().x - 3.0f, ImGui::GetItemRectMin().y - 3.0f),
                          ImVec2(std::max(groupMax.x, ImGui::GetItemRectMin().x + kStripWidth) + 3.0f, groupMax.y + 3.0f),
                          ImGui::GetColorU32(ImGuiCol_HeaderActive), 4.0f);
        }
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::Dummy(ImVec2(std::max(0.0f, ImGui::GetItemRectMin().x + kStripWidth - ImGui::GetCursorScreenPos().x), 1.0f));
        ImGui::PopID();
    }
    ImGui::EndChild();
}

bool AudioMixerPlugin::busCombo(const char* label, std::string& value, const std::string& exclude) {
    bool changed = false;
    if (ImGui::BeginCombo(label, value.empty() ? "(choose)" : value.c_str())) {
        for (const core::MixerBus& bus : config_.buses) {
            if (bus.name == exclude) continue;
            if (ImGui::Selectable(bus.name.c_str(), bus.name == value)) {
                changed = value != bus.name;
                value = bus.name;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

void AudioMixerPlugin::drawBusDetails() {
    core::MixerBus* bus = config_.bus(selectedBus_);
    if (bus == nullptr) {
        ImGui::TextDisabled("Click a bus above to edit it.");
        return;
    }
    bool changed = false;
    if (renameFor_ != bus->name) {
        std::snprintf(renameBuffer_, sizeof(renameBuffer_), "%s", bus->name.c_str());
        renameFor_ = bus->name;
    }
    ImGui::PushItemWidth(220.0f);
    ImGui::InputText("Name", renameBuffer_, sizeof(renameBuffer_));
    if (ImGui::IsItemDeactivatedAfterEdit() && renameBuffer_[0] != '\0' && bus->name != renameBuffer_) {
        const std::string from = bus->name;
        if (config_.renameBus(from, renameBuffer_)) {
            if (audio_.mixer().solo(from)) {
                audio_.mixer().setSolo(from, false);
                audio_.mixer().setSolo(renameBuffer_, true);
            }
            selectedBus_ = renameBuffer_;
            renameFor_.clear();
            changed = true;
            bus = config_.bus(selectedBus_);
        } else {
            status_ = "There's already a bus called \"" + std::string(renameBuffer_) + "\".";
            renameFor_.clear();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Sounds and scripts refer to buses by name. Default sounds use \"Music\" and \"SFX\".");
    }
    if (!bus->parent.empty()) {
        std::string parent = bus->parent;
        if (busCombo("Goes into", parent, bus->name)) {
            bus->parent = parent;
            changed = true;
        }
    } else {
        ImGui::TextDisabled("This is the master bus: everything ends up here.");
    }
    changed |= ImGui::SliderFloat("Volume", &bus->volumeDb, kFaderMinDb, kFaderMaxDb, "%+.1f dB");
    changed |= frequencySlider("Low-pass", bus->lowpassHz, true);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cuts sounds above this pitch, like hearing through a wall or underwater.");
    changed |= frequencySlider("High-pass", bus->highpassHz, false);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Cuts sounds below this pitch, like a radio or a phone.");
    changed |= ImGui::SliderFloat("Reverb", &bus->reverbMix, 0.0f, 1.0f, "%.2f");
    if (bus->reverbMix > 0.0f) {
        changed |= ImGui::SliderFloat("Room size", &bus->reverbRoom, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Damping", &bus->reverbDamp, 0.0f, 1.0f, "%.2f");
    }

    ImGui::SeparatorText("Sends");
    ImGui::TextDisabled("Also feed a copy of this bus into another one, such as a reverb bus.");
    for (size_t s = 0; s < bus->sends.size(); ++s) {
        core::MixerSend& send = bus->sends[s];
        ImGui::PushID(static_cast<int>(s));
        ImGui::SetNextItemWidth(130.0f);
        changed |= busCombo("##target", send.target, bus->name);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160.0f);
        changed |= ImGui::SliderFloat("##level", &send.levelDb, kFaderMinDb, kFaderMaxDb, "%+.1f dB");
        ImGui::SameLine();
        changed |= ImGui::Checkbox("Before fader", &send.preFader);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep sending at full level even when this bus is turned down.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            const std::string target = send.target;
            bus->sends.erase(bus->sends.begin() + static_cast<long>(s));
            for (core::MixerSnapshot& snapshot : config_.snapshots) {
                std::erase_if(snapshot.values, [&](const core::MixerSnapshotValue& v) {
                    return v.bus == bus->name && v.param == core::MixerParam::Send && v.sendTarget == target;
                });
            }
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button("Add Send")) {
        for (const core::MixerBus& other : config_.buses) {
            if (other.name == bus->name || other.name == bus->parent) continue;
            if (std::any_of(bus->sends.begin(), bus->sends.end(), [&](const auto& s) { return s.target == other.name; })) continue;
            bus->sends.push_back({other.name, -12.0f, false});
            changed = true;
            break;
        }
    }

    ImGui::SeparatorText("Ducking");
    ImGui::TextDisabled("Turn this bus down automatically while another bus is playing (music under dialogue).");
    for (size_t d = 0; d < bus->ducks.size(); ++d) {
        core::MixerDuck& duck = bus->ducks[d];
        ImGui::PushID(1000 + static_cast<int>(d));
        ImGui::SetNextItemWidth(130.0f);
        changed |= busCombo("While playing", duck.trigger, bus->name);
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            bus->ducks.erase(bus->ducks.begin() + static_cast<long>(d));
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::Indent();
        changed |= ImGui::SliderFloat("Turn down by", &duck.amountDb, -40.0f, 0.0f, "%.1f dB");
        changed |= ImGui::SliderFloat("When louder than", &duck.thresholdDb, -70.0f, 0.0f, "%.0f dB");
        changed |= ImGui::SliderFloat("Fade down", &duck.attackMs, 1.0f, 1000.0f, "%.0f ms", ImGuiSliderFlags_Logarithmic);
        changed |= ImGui::SliderFloat("Come back over", &duck.releaseMs, 10.0f, 5000.0f, "%.0f ms", ImGuiSliderFlags_Logarithmic);
        ImGui::Unindent();
        ImGui::PopID();
    }
    if (ImGui::Button("Add Ducking")) {
        for (const core::MixerBus& other : config_.buses) {
            if (other.name != bus->name && !other.parent.empty() &&
                std::none_of(bus->ducks.begin(), bus->ducks.end(), [&](const auto& d) { return d.trigger == other.name; })) {
                bus->ducks.push_back({other.name});
                changed = true;
                break;
            }
        }
    }

    ImGui::Spacing();
    ImGui::BeginDisabled(bus->parent.empty());
    if (ImGui::Button("Remove This Bus")) {
        const std::string name = bus->name;
        audio_.mixer().setSolo(name, false);
        selectedBus_ = bus->parent;
        config_.removeBus(name);
        changed = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Buses inside it move up a level; sounds on it go to the master.");
    ImGui::PopItemWidth();

    if (changed) {
        dirty_ = true;
        apply();
    }
}

void AudioMixerPlugin::drawSnapshots() {
    ImGui::TextDisabled("A snapshot is a set of changes a game can fade in, like \"Paused\" or \"Underwater\".");
    ImGui::TextDisabled("Scripts: audio.snapshot(\"Underwater\", 1, 0.5) fades it in over half a second.");
    bool changed = false;
    ImGui::BeginChild("##snapshotlist", ImVec2(220.0f, 240.0f), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < config_.snapshots.size(); ++i) {
        const core::MixerSnapshot& snapshot = config_.snapshots[i];
        const float intensity = audio_.mixer().snapshotIntensity(snapshot.name);
        char label[96];
        std::snprintf(label, sizeof(label), "%s%s###snap%zu", snapshot.name.c_str(), intensity > 0.0f ? "  (on)" : "", i);
        if (ImGui::Selectable(label, selectedSnapshot_ == static_cast<int>(i))) selectedSnapshot_ = static_cast<int>(i);
    }
    if (ImGui::Button("New Snapshot")) {
        std::string name = "Snapshot";
        for (int i = 2; config_.snapshotIndex(name) >= 0; ++i) name = "Snapshot " + std::to_string(i);
        config_.snapshots.push_back({name, {}});
        selectedSnapshot_ = static_cast<int>(config_.snapshots.size()) - 1;
        changed = true;
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    if (selectedSnapshot_ >= 0 && selectedSnapshot_ < static_cast<int>(config_.snapshots.size())) {
        core::MixerSnapshot& snapshot = config_.snapshots[selectedSnapshot_];
        if (snapshotNameFor_ != selectedSnapshot_) {
            std::snprintf(snapshotName_, sizeof(snapshotName_), "%s", snapshot.name.c_str());
            snapshotNameFor_ = selectedSnapshot_;
        }
        ImGui::SetNextItemWidth(200.0f);
        ImGui::InputText("Name##snapshot", snapshotName_, sizeof(snapshotName_));
        if (ImGui::IsItemDeactivatedAfterEdit() && snapshotName_[0] != '\0' && snapshot.name != snapshotName_) {
            if (config_.snapshotIndex(snapshotName_) < 0) {
                audio_.mixer().setSnapshot(snapshot.name, 0.0f);
                snapshot.name = snapshotName_;
                changed = true;
            } else {
                status_ = "There's already a snapshot called \"" + std::string(snapshotName_) + "\".";
            }
            snapshotNameFor_ = -2;
        }

        const float intensity = audio_.mixer().snapshotIntensity(snapshot.name);
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderFloat("Fade time", &previewFade_, 0.0f, 3.0f, "%.2f s");
        if (ImGui::Button(intensity > 0.0f ? "Turn Off" : "Try It")) {
            if (!error_.empty()) apply();
            audio_.mixer().setSnapshot(snapshot.name, intensity > 0.0f ? 0.0f : 1.0f, previewFade_);
        }
        ImGui::SameLine();
        ImGui::ProgressBar(intensity, ImVec2(140.0f, 0.0f));

        ImGui::SeparatorText("Changes");
        for (size_t v = 0; v < snapshot.values.size(); ++v) {
            core::MixerSnapshotValue& value = snapshot.values[v];
            ImGui::PushID(static_cast<int>(v));
            ImGui::SetNextItemWidth(110.0f);
            changed |= busCombo("##bus", value.bus);
            ImGui::SameLine();
            int param = static_cast<int>(value.param);
            ImGui::SetNextItemWidth(95.0f);
            if (ImGui::Combo("##param", &param, kParamLabels, IM_ARRAYSIZE(kParamLabels))) {
                value.param = static_cast<core::MixerParam>(param);
                value.value = value.param == core::MixerParam::Lowpass    ? 1000.0f
                              : value.param == core::MixerParam::Highpass ? 300.0f
                              : value.param == core::MixerParam::Reverb   ? 0.3f
                                                                          : -12.0f;
                changed = true;
            }
            ImGui::SameLine();
            if (value.param == core::MixerParam::Send) {
                ImGui::SetNextItemWidth(100.0f);
                const core::MixerBus* from = config_.bus(value.bus);
                if (ImGui::BeginCombo("##send", value.sendTarget.empty() ? "(send)" : value.sendTarget.c_str())) {
                    if (from != nullptr) {
                        for (const core::MixerSend& send : from->sends) {
                            if (ImGui::Selectable(send.target.c_str(), send.target == value.sendTarget)) {
                                value.sendTarget = send.target;
                                changed = true;
                            }
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
            }
            ImGui::SetNextItemWidth(150.0f);
            switch (value.param) {
                case core::MixerParam::Volume:
                case core::MixerParam::Send:
                    changed |= ImGui::SliderFloat("##value", &value.value, kFaderMinDb, kFaderMaxDb, "%+.1f dB");
                    break;
                case core::MixerParam::Lowpass: changed |= frequencySlider("##value", value.value, true); break;
                case core::MixerParam::Highpass: changed |= frequencySlider("##value", value.value, false); break;
                case core::MixerParam::Reverb:
                    changed |= ImGui::SliderFloat("##value", &value.value, 0.0f, 1.0f, "%.2f");
                    break;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x")) {
                snapshot.values.erase(snapshot.values.begin() + static_cast<long>(v));
                changed = true;
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Add Change")) {
            snapshot.values.push_back({selectedBus_, core::MixerParam::Volume, {}, -12.0f});
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete Snapshot")) {
            audio_.mixer().setSnapshot(snapshot.name, 0.0f);
            config_.snapshots.erase(config_.snapshots.begin() + selectedSnapshot_);
            selectedSnapshot_ = -1;
            changed = true;
        }
    } else {
        ImGui::TextDisabled("Pick a snapshot on the left.");
    }
    ImGui::EndGroup();
    if (changed) {
        dirty_ = true;
        apply();
    }
}

void AudioMixerPlugin::drawTestSounds() {
    ImGui::TextDisabled("Play sound files through any bus to hear the mix, ducking and snapshots while you edit.");
    for (size_t i = 0; i < tests_.size(); ++i) {
        TestSound& test = tests_[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::SetNextItemWidth(280.0f);
        ImGui::InputText("##file", test.path, sizeof(test.path));
        ImGui::SameLine();
        browseButton("test", test.path, sizeof(test.path), {"Choose a Sound", {"*.wav", "*.mp3", "*.ogg", "*.flac"}, "Sound files"});
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        busCombo("##bus", test.bus);
        ImGui::SameLine();
        ImGui::Checkbox("Loop", &test.loop);
        ImGui::SameLine();
        const bool playing = test.handle != core::kInvalidSoundHandle && audio_.isSoundPlaying(test.handle);
        if (ImGui::Button(playing ? "Stop" : "Play")) {
            if (playing) {
                audio_.stopSound(test.handle);
            } else if (test.path[0] != '\0') {
                if (test.loadedPath != test.path) {
                    if (test.handle != core::kInvalidSoundHandle) audio_.unloadSound(test.handle);
                    test.handle = audio_.loadSound(test.path);
                    test.loadedPath = test.path;
                    if (test.handle == core::kInvalidSoundHandle) status_ = "Couldn't play " + test.loadedPath;
                }
                if (test.handle != core::kInvalidSoundHandle) {
                    audio_.setSoundSpatialized(test.handle, false);
                    audio_.setSoundLooping(test.handle, test.loop);
                    audio_.setSoundBus(test.handle, test.bus);
                    audio_.playFromOffset(test.handle, 0.0);
                }
            }
        }
        if (test.handle != core::kInvalidSoundHandle) audio_.setSoundBus(test.handle, test.bus);
        ImGui::PopID();
    }
}

} // namespace engine::studio::plugins
