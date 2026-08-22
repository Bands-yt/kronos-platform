#include "studio/plugins/MovieModePlugin.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <imgui.h>

#include "cinematic/CurveInterpolation.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {
namespace {

using namespace engine::cinematic;

constexpr float kHeaderWidth = 168.0f;
constexpr float kRulerHeight = 24.0f;
constexpr float kTrackHeight = 26.0f;
constexpr float kKeySize = 5.0f;

const char* trackKindLabel(TrackKind kind) {
    switch (kind) {
        case TrackKind::Camera: return "Camera";
        case TrackKind::SkeletalAnimation: return "Animation";
        case TrackKind::Transform: return "Transform";
        case TrackKind::LightIntensity: return "Light";
        case TrackKind::Audio: return "Audio";
        case TrackKind::ScriptTrigger: return "Script";
    }
    return "Track";
}

// One hue per track kind, so a 6-track timeline is readable at a glance
// rather than six identical grey rows.
ImU32 trackKindColor(TrackKind kind, float alpha = 1.0f) {
    ImVec4 c;
    switch (kind) {
        case TrackKind::Camera:            c = ImVec4(0.31f, 0.66f, 0.87f, alpha); break;
        case TrackKind::SkeletalAnimation: c = ImVec4(0.55f, 0.75f, 0.35f, alpha); break;
        case TrackKind::Transform:         c = ImVec4(0.85f, 0.62f, 0.28f, alpha); break;
        case TrackKind::LightIntensity:    c = ImVec4(0.92f, 0.82f, 0.35f, alpha); break;
        case TrackKind::Audio:             c = ImVec4(0.72f, 0.45f, 0.85f, alpha); break;
        case TrackKind::ScriptTrigger:     c = ImVec4(0.90f, 0.40f, 0.42f, alpha); break;
    }
    return ImGui::GetColorU32(c);
}

const char* interpolationLabel(InterpolationMode mode) {
    switch (mode) {
        case InterpolationMode::Stepped: return "Stepped";
        case InterpolationMode::Linear:  return "Linear";
        case InterpolationMode::Cubic:   return "Cubic";
        case InterpolationMode::Bezier:  return "Bezier";
    }
    return "?";
}

void drawDiamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border) {
    const ImVec2 pts[4] = {ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y)};
    dl->AddConvexPolyFilled(pts, 4, fill);
    dl->AddPolyline(pts, 4, border, ImDrawFlags_Closed, 1.2f);
}

} // namespace

MovieModePlugin::MovieModePlugin() {
    seedDefaultSequence();

    // A rail with real points, not an empty one: an empty rail draws
    // nothing in the viewport, which reads as a broken gizmo rather than
    // an empty document.
    RailPoint a;
    a.position = {-8.0f, 3.0f, 8.0f};
    RailPoint b;
    b.position = {0.0f, 4.5f, 10.0f};
    RailPoint c;
    c.position = {8.0f, 3.0f, 8.0f};
    rail_.addPoint(a);
    rail_.addPoint(b);
    rail_.addPoint(c);

    CameraRailSettings settings = rail_.settings();
    settings.aimMode = RailAimMode::LookAtPoint;
    settings.lookAtTarget = glm::vec3(0.0f, 1.0f, 0.0f);
    rail_.setSettings(settings);
}

void MovieModePlugin::seedDefaultSequence() {
    // One track per TrackKind -- the six the sequencer models. Seeded with
    // real, sampleable content so the timeline and curve editor open onto
    // something to edit rather than an empty grid.
    auto& camera = sequence_.addTrack("Camera Rail", TrackKind::Camera);
    auto& railParam = camera.channel("railT");
    railParam.keys.push_back(Keyframe{0.0f, 0.0f, InterpolationMode::Bezier, {-0.4f, 0.0f}, {0.8f, 0.0f}});
    railParam.keys.push_back(Keyframe{6.0f, 1.0f, InterpolationMode::Bezier, {-0.8f, 0.0f}, {0.4f, 0.0f}});

    auto& anim = sequence_.addTrack("Actor Animation", TrackKind::SkeletalAnimation);
    anim.channel("weight").keys.push_back(Keyframe{0.0f, 1.0f, InterpolationMode::Linear});

    auto& transform = sequence_.addTrack("Prop Transform", TrackKind::Transform);
    transform.channel("y").keys.push_back(Keyframe{0.0f, 0.0f, InterpolationMode::Cubic});
    transform.channel("y").keys.push_back(Keyframe{3.0f, 2.0f, InterpolationMode::Cubic});
    transform.channel("y").keys.push_back(Keyframe{6.0f, 0.0f, InterpolationMode::Cubic});

    auto& light = sequence_.addTrack("Key Light", TrackKind::LightIntensity);
    light.channel("intensity").keys.push_back(Keyframe{0.0f, 0.2f, InterpolationMode::Linear});
    light.channel("intensity").keys.push_back(Keyframe{2.5f, 3.0f, InterpolationMode::Linear});

    auto& audio = sequence_.addTrack("Audio", TrackKind::Audio);
    audio.events.push_back(TrackEvent{0.5f, "music/opening_stinger"});

    auto& script = sequence_.addTrack("Script Triggers", TrackKind::ScriptTrigger);
    script.events.push_back(TrackEvent{4.0f, "onShotComplete"});

    sequence_.setLoopRegion(0.0f, 6.0f);
}

void MovieModePlugin::update(float dt, core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    if (!sequence_.isPlaying()) return;
    // Scrubbing takes the transport over for as long as the gesture
    // lasts. Without this, update() advances the playhead and the drag
    // then sets it back every frame -- so a held scrub re-fires the same
    // audio cues and script triggers on every single frame, and the
    // playhead visibly fights the cursor.
    if (isScrubbing()) {
        lastFiredEvents_.clear();
        return;
    }
    lastFiredEvents_.clear();
    sequence_.advance(dt, lastFiredEvents_);
}

cinematic::TrackChannel* MovieModePlugin::selectedChannel() {
    auto& tracks = sequence_.mutableTracks();
    if (selectedTrack_ < 0 || selectedTrack_ >= static_cast<int>(tracks.size())) return nullptr;
    auto& channels = tracks[static_cast<size_t>(selectedTrack_)].channels;
    if (selectedChannelIndex_ < 0 || selectedChannelIndex_ >= static_cast<int>(channels.size())) return nullptr;
    return &channels[static_cast<size_t>(selectedChannelIndex_)];
}

void MovieModePlugin::moveRailPoint(int index, const glm::vec3& position) {
    if (index < 0 || index >= static_cast<int>(rail_.pointCount())) return;
    // CameraRail exposes points() as const; rebuilding through a copy
    // keeps the rail's own invariants its own rather than handing out a
    // mutable reference to its internals.
    std::vector<RailPoint> points = rail_.points();
    points[static_cast<size_t>(index)].position = position;
    rail_.clear();
    for (const RailPoint& p : points) rail_.addPoint(p);
}

float MovieModePlugin::railParameterAtPlayhead() const {
    const float duration = std::max(sequence_.durationSeconds(), 1e-4f);
    return std::clamp(sequence_.playheadSeconds() / duration, 0.0f, 1.0f);
}

bool MovieModePlugin::buildExport(std::string& outError) {
    if (!validateExportSettings(exportSettings_, outError)) {
        lastSchedule_.clear();
        return false;
    }
    buildExportSchedule(exportSettings_, sequence_.durationSeconds(), lastSchedule_);
    outError.clear();
    return true;
}

void MovieModePlugin::drawTransport() {
    const int fps = framesPerSecond(sequence_.frameRate());

    if (ImGui::Button(sequence_.isPlaying() ? "Pause" : "Play")) {
        sequence_.isPlaying() ? sequence_.pause() : sequence_.play();
    }
    ImGui::SameLine();
    if (ImGui::Button("Stop")) {
        sequence_.pause();
        sequence_.setPlayhead(0.0f);
        rail_.resetDamping();
    }
    ImGui::SameLine();
    if (ImGui::Button("|<")) {
        sequence_.setPlayhead(0.0f);
        rail_.resetDamping();
    }
    ImGui::SameLine();
    if (ImGui::Button("<|")) sequence_.stepFrames(-1);
    ImGui::SameLine();
    if (ImGui::Button("|>")) sequence_.stepFrames(1);
    ImGui::SameLine();
    if (ImGui::Button(">|")) sequence_.setPlayhead(sequence_.durationSeconds());

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    // Monospaced-looking, fixed-width timecode: HH:MM:SS:FF straight from
    // the sequencer rather than reformatted here, so the editor and any
    // exported footage can never disagree about what frame this is.
    ImGui::TextUnformatted(sequence_.timecode().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("@ %d fps", fps);

    ImGui::SameLine();
    ImGui::Dummy(ImVec2(12.0f, 0.0f));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    const char* rateLabels[] = {"24 fps", "30 fps", "60 fps"};
    int rateIndex = static_cast<int>(sequence_.frameRate());
    if (ImGui::Combo("##framerate", &rateIndex, rateLabels, IM_ARRAYSIZE(rateLabels))) {
        sequence_.setFrameRate(static_cast<SequenceFrameRate>(rateIndex));
        // Re-snap: the old playhead was a whole frame at the OLD rate and
        // is almost certainly between frames at the new one.
        sequence_.setPlayhead(sequence_.playheadSeconds());
        exportSettings_.frameRate = sequence_.frameRate();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Snap", &snapToFrames_);
    helpMarker("Snaps dragged keys and the playhead to whole frames. Off lets a key sit between frames, which will not "
               "survive an export at this frame rate.");

    ImGui::SameLine();
    if (ImGui::Button("Render Movie Sequence...")) {
        exportSettings_.frameRate = sequence_.frameRate();
        exporterOpen_ = true;
        ImGui::OpenPopup("Render Movie Sequence");
    }
}

void MovieModePlugin::drawTimeline() {
    auto& tracks = sequence_.mutableTracks();
    const float trackAreaWidth = std::max(120.0f, ImGui::GetContentRegionAvail().x - kHeaderWidth - 8.0f);
    view_.widthPixels = trackAreaWidth;

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 trackOrigin(origin.x + kHeaderWidth, origin.y);
    const float totalHeight = kRulerHeight + kTrackHeight * static_cast<float>(tracks.size());

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(trackOrigin, ImVec2(trackOrigin.x + trackAreaWidth, trackOrigin.y + totalHeight),
                      ImGui::GetColorU32(ImVec4(0.10f, 0.11f, 0.12f, 1.0f)));

    // --- ruler: gridlines at a spacing that stays readable at any zoom ---
    const float gridStep = gridIntervalSeconds(view_);
    const float firstGrid = std::floor(visibleStartSeconds(view_) / gridStep) * gridStep;
    for (float t = firstGrid; t <= visibleEndSeconds(view_); t += gridStep) {
        if (t < 0.0f) continue;
        const float x = trackOrigin.x + timeToPixel(view_, t);
        if (x < trackOrigin.x || x > trackOrigin.x + trackAreaWidth) continue;
        dl->AddLine(ImVec2(x, trackOrigin.y), ImVec2(x, trackOrigin.y + totalHeight),
                    ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.07f)));
        char label[32];
        std::snprintf(label, sizeof(label), "%.2fs", static_cast<double>(t));
        dl->AddText(ImVec2(x + 3.0f, trackOrigin.y + 4.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
    }

    // --- loop region ------------------------------------------------------
    if (sequence_.loopEnabled()) {
        const float lx0 = trackOrigin.x + timeToPixel(view_, sequence_.loopStart());
        const float lx1 = trackOrigin.x + timeToPixel(view_, sequence_.loopEnd());
        dl->AddRectFilled(ImVec2(lx0, trackOrigin.y), ImVec2(lx1, trackOrigin.y + kRulerHeight),
                          ImGui::GetColorU32(ImVec4(0.31f, 0.66f, 0.87f, 0.20f)));
        for (float lx : {lx0, lx1}) {
            dl->AddRectFilled(ImVec2(lx - 3.0f, trackOrigin.y), ImVec2(lx + 3.0f, trackOrigin.y + kRulerHeight),
                              ImGui::GetColorU32(ImVec4(0.31f, 0.66f, 0.87f, 0.85f)));
        }
    }

    // --- an invisible button over the whole strip owns the mouse ---------
    ImGui::InvisibleButton("##timeline", ImVec2(kHeaderWidth + trackAreaWidth, totalHeight));
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float mouseTrackX = mouse.x - trackOrigin.x;
    const bool mouseInTracks = mouse.x >= trackOrigin.x && mouse.x <= trackOrigin.x + trackAreaWidth;
    const bool mouseInRuler = mouseInTracks && mouse.y >= trackOrigin.y && mouse.y <= trackOrigin.y + kRulerHeight;

    if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
        // Zoom about the cursor: the time under the pointer stays put,
        // which is the only zoom that feels anchored while editing.
        const float timeUnderCursor = pixelToTime(view_, mouseTrackX);
        view_.pixelsPerSecond = clampZoom(view_.pixelsPerSecond * (1.0f + ImGui::GetIO().MouseWheel * 0.12f));
        view_.scrollSeconds = timeUnderCursor - mouseTrackX / view_.pixelsPerSecond;
        view_.scrollSeconds = std::max(0.0f, view_.scrollSeconds);
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInRuler) {
        // A loop handle wins over scrubbing when the click is on one --
        // otherwise the handles would be unreachable, since they sit
        // inside the scrub strip.
        const int handle = sequence_.loopEnabled()
                               ? hitTestLoopHandle(view_, sequence_.loopStart(), sequence_.loopEnd(), mouseTrackX)
                               : 0;
        if (handle != 0) {
            draggingLoopHandle_ = handle;
        } else {
            draggingPlayhead_ = true;
            sequence_.setPlayhead(dragTimeForPixel(view_, mouseTrackX, sequence_.frameRate(), snapToFrames_));
            rail_.resetDamping();
        }
    }

    if (draggingPlayhead_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            sequence_.setPlayhead(dragTimeForPixel(view_, mouseTrackX, sequence_.frameRate(), snapToFrames_));
        } else {
            draggingPlayhead_ = false;
            rail_.resetDamping();
        }
    }
    if (draggingLoopHandle_ != 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const float t = dragTimeForPixel(view_, mouseTrackX, sequence_.frameRate(), snapToFrames_);
            if (draggingLoopHandle_ < 0) {
                sequence_.setLoopRegion(std::min(t, sequence_.loopEnd() - 1e-3f), sequence_.loopEnd());
            } else {
                sequence_.setLoopRegion(sequence_.loopStart(), std::max(t, sequence_.loopStart() + 1e-3f));
            }
        } else {
            draggingLoopHandle_ = 0;
        }
    }

    // --- track rows -------------------------------------------------------
    for (size_t i = 0; i < tracks.size(); ++i) {
        SequencerTrack& track = tracks[i];
        const float rowY = trackOrigin.y + kRulerHeight + kTrackHeight * static_cast<float>(i);
        const float rowMid = rowY + kTrackHeight * 0.5f;
        const bool isSelected = static_cast<int>(i) == selectedTrack_;

        if (isSelected) {
            dl->AddRectFilled(ImVec2(origin.x, rowY), ImVec2(trackOrigin.x + trackAreaWidth, rowY + kTrackHeight),
                              ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.05f)));
        }
        dl->AddLine(ImVec2(origin.x, rowY), ImVec2(trackOrigin.x + trackAreaWidth, rowY),
                    ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.06f)));

        // Header: colour chip, name, kind, mute.
        dl->AddRectFilled(ImVec2(origin.x + 2.0f, rowY + 5.0f), ImVec2(origin.x + 6.0f, rowY + kTrackHeight - 5.0f),
                          trackKindColor(track.kind, track.muted ? 0.3f : 1.0f));
        const ImU32 nameColor =
            track.muted ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(ImGuiCol_Text);
        dl->AddText(ImVec2(origin.x + 12.0f, rowY + 5.0f), nameColor, track.name.c_str());
        dl->AddText(ImVec2(origin.x + 12.0f + 96.0f, rowY + 5.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    trackKindLabel(track.kind));

        // Clicking the header selects the track; clicking the chip mutes.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x < trackOrigin.x && mouse.y >= rowY &&
            mouse.y < rowY + kTrackHeight) {
            if (mouse.x < origin.x + 10.0f) {
                track.muted = !track.muted;
            } else {
                selectedTrack_ = static_cast<int>(i);
                selectedChannelIndex_ = 0;
                selectedKey_ = -1;
            }
        }

        if (track.muted) continue;

        // Keys, per channel, all on the one row -- the curve editor is
        // where a single channel gets its own vertical space.
        for (size_t c = 0; c < track.channels.size(); ++c) {
            for (size_t k = 0; k < track.channels[c].keys.size(); ++k) {
                const Keyframe& key = track.channels[c].keys[k];
                if (!isTimeVisible(view_, key.timeSeconds)) continue;
                const float x = trackOrigin.x + timeToPixel(view_, key.timeSeconds);
                const bool keySelected = isSelected && static_cast<int>(c) == selectedChannelIndex_ &&
                                          static_cast<int>(k) == selectedKey_;
                drawDiamond(dl, ImVec2(x, rowMid), keySelected ? kKeySize + 1.5f : kKeySize,
                            trackKindColor(track.kind), ImGui::GetColorU32(keySelected ? ImGuiCol_Text : ImGuiCol_Border));
            }
        }

        // Events are instants, not values -- drawn as ticks, never dragged
        // through the keyframe path.
        for (const TrackEvent& event : track.events) {
            if (!isTimeVisible(view_, event.timeSeconds)) continue;
            const float x = trackOrigin.x + timeToPixel(view_, event.timeSeconds);
            dl->AddLine(ImVec2(x, rowY + 4.0f), ImVec2(x, rowY + kTrackHeight - 4.0f), trackKindColor(track.kind), 2.0f);
            dl->AddText(ImVec2(x + 4.0f, rowY + 5.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), event.payload.c_str());
        }

        // Key picking: nearest within the grab radius, via the tested
        // hit test rather than a hand-rolled distance loop here.
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouseInTracks && mouse.y >= rowY &&
            mouse.y < rowY + kTrackHeight) {
            for (size_t c = 0; c < track.channels.size(); ++c) {
                const size_t hit = hitTestKeyframe(view_, track.channels[c].keys, mouseTrackX);
                if (hit == static_cast<size_t>(-1)) continue;
                selectedTrack_ = static_cast<int>(i);
                selectedChannelIndex_ = static_cast<int>(c);
                selectedKey_ = static_cast<int>(hit);
                dragTrack_ = selectedTrack_;
                dragChannel_ = selectedChannelIndex_;
                dragKey_ = selectedKey_;
                break;
            }
        }
    }

    // --- key drag ---------------------------------------------------------
    if (dragKey_ >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            auto& dragTracks = sequence_.mutableTracks();
            if (dragTrack_ < static_cast<int>(dragTracks.size())) {
                auto& channels = dragTracks[static_cast<size_t>(dragTrack_)].channels;
                if (dragChannel_ < static_cast<int>(channels.size())) {
                    auto& keys = channels[static_cast<size_t>(dragChannel_)].keys;
                    if (dragKey_ < static_cast<int>(keys.size())) {
                        keys[static_cast<size_t>(dragKey_)].timeSeconds =
                            dragTimeForPixel(view_, mouseTrackX, sequence_.frameRate(), snapToFrames_);
                        // Keeps the track sorted while preserving the
                        // dragged key's identity -- see
                        // reorderDraggedKeyframe() for why the obvious
                        // erase-then-reinsert loses a key here.
                        dragKey_ = reorderDraggedKeyframe(keys, dragKey_);
                        selectedKey_ = dragKey_;
                    }
                }
            }
        } else {
            dragKey_ = -1;
            dragTrack_ = -1;
            dragChannel_ = -1;
        }
    }

    // --- playhead, drawn last so it sits above every track ---------------
    const float playX = trackOrigin.x + timeToPixel(view_, sequence_.playheadSeconds());
    if (playX >= trackOrigin.x && playX <= trackOrigin.x + trackAreaWidth) {
        const ImU32 playColor = ImGui::GetColorU32(ImVec4(0.95f, 0.35f, 0.30f, 0.95f));
        dl->AddLine(ImVec2(playX, trackOrigin.y), ImVec2(playX, trackOrigin.y + totalHeight), playColor, 1.6f);
        const ImVec2 head[3] = {ImVec2(playX - 5.0f, trackOrigin.y), ImVec2(playX + 5.0f, trackOrigin.y),
                                 ImVec2(playX, trackOrigin.y + 8.0f)};
        dl->AddConvexPolyFilled(head, 3, playColor);
    }
}

void MovieModePlugin::drawCurveEditor() {
    TrackChannel* channel = selectedChannel();
    if (channel == nullptr) {
        ImGui::TextDisabled("Select a track with channels to edit its curve.");
        return;
    }
    auto& keys = channel->keys;

    // Channel picker, so a Transform track's x/y/z are all reachable.
    auto& track = sequence_.mutableTracks()[static_cast<size_t>(selectedTrack_)];
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::BeginCombo("Channel", channel->name.c_str())) {
        for (size_t c = 0; c < track.channels.size(); ++c) {
            const bool sel = static_cast<int>(c) == selectedChannelIndex_;
            if (ImGui::Selectable(track.channels[c].name.c_str(), sel)) {
                selectedChannelIndex_ = static_cast<int>(c);
                selectedKey_ = -1;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-fit", &curveAutoFit_);
    helpMarker("Fits the value axis to the curve. Turn off to keep a fixed range while comparing channels.");

    if (selectedKey_ >= 0 && selectedKey_ < static_cast<int>(keys.size())) {
        Keyframe& key = keys[static_cast<size_t>(selectedKey_)];
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        int mode = static_cast<int>(key.mode);
        const char* modes[] = {"Stepped", "Linear", "Cubic", "Bezier"};
        if (ImGui::Combo("Interp", &mode, modes, IM_ARRAYSIZE(modes))) key.mode = static_cast<InterpolationMode>(mode);
        ImGui::SameLine();
        if (ImGui::Button("Delete Key")) {
            keys.erase(keys.begin() + selectedKey_);
            selectedKey_ = -1;
        }
    }

    // --- value axis -------------------------------------------------------
    if (curveAutoFit_ && !keys.empty()) {
        curveValueMin_ = keys.front().value;
        curveValueMax_ = keys.front().value;
        for (const Keyframe& k : keys) {
            curveValueMin_ = std::min(curveValueMin_, k.value);
            curveValueMax_ = std::max(curveValueMax_, k.value);
        }
        const float pad = std::max(0.5f, (curveValueMax_ - curveValueMin_) * 0.25f);
        curveValueMin_ -= pad;
        curveValueMax_ += pad;
    }
    const float valueSpan = std::max(1e-4f, curveValueMax_ - curveValueMin_);

    const ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    const float canvasW = std::max(160.0f, ImGui::GetContentRegionAvail().x);
    const float canvasH = std::max(120.0f, ImGui::GetContentRegionAvail().y - 8.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasW, canvasPos.y + canvasH),
                      ImGui::GetColorU32(ImVec4(0.09f, 0.10f, 0.11f, 1.0f)));

    // The curve editor shares the timeline's horizontal mapping, so
    // scrubbing or zooming the timeline moves both together.
    TimelineView curveView = view_;
    curveView.widthPixels = canvasW;

    auto valueToY = [&](float value) {
        return canvasPos.y + canvasH - ((value - curveValueMin_) / valueSpan) * canvasH;
    };
    auto yToValue = [&](float y) {
        return curveValueMin_ + ((canvasPos.y + canvasH - y) / canvasH) * valueSpan;
    };
    auto timeToX = [&](float t) { return canvasPos.x + timeToPixel(curveView, t); };

    // Zero line, when it is in range -- the reference an eased curve is
    // read against.
    if (curveValueMin_ < 0.0f && curveValueMax_ > 0.0f) {
        const float zeroY = valueToY(0.0f);
        dl->AddLine(ImVec2(canvasPos.x, zeroY), ImVec2(canvasPos.x + canvasW, zeroY),
                    ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 0.10f)));
    }

    // --- the curve itself, sampled through sampleCurve() ------------------
    // Sampled per pixel rather than drawn as analytic segments: sampleCurve
    // is the same function playback and export evaluate, so what is drawn
    // here is by construction what will render, including the Bezier
    // time-inversion that a naive per-segment draw would get wrong.
    if (!keys.empty()) {
        ImVec2 prev(0.0f, 0.0f);
        bool havePrev = false;
        for (float px = 0.0f; px <= canvasW; px += 1.0f) {
            const float t = pixelToTime(curveView, px);
            const ImVec2 point(canvasPos.x + px, valueToY(sampleCurve(keys, t)));
            if (havePrev) dl->AddLine(prev, point, trackKindColor(track.kind), 1.8f);
            prev = point;
            havePrev = true;
        }
    }

    ImGui::InvisibleButton("##curve", ImVec2(canvasW, canvasH));
    const bool canvasHovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;

    // Double-click on empty canvas inserts a key at that exact time/value.
    if (canvasHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        Keyframe inserted;
        inserted.timeSeconds =
            dragTimeForPixel(curveView, mouse.x - canvasPos.x, sequence_.frameRate(), snapToFrames_);
        inserted.value = yToValue(mouse.y);
        inserted.mode = InterpolationMode::Bezier;
        insertKeyframe(keys, inserted);
    }

    // --- keys and their Bezier handles ------------------------------------
    for (size_t k = 0; k < keys.size(); ++k) {
        Keyframe& key = keys[k];
        const ImVec2 keyPos(timeToX(key.timeSeconds), valueToY(key.value));
        const bool isSelected = static_cast<int>(k) == selectedKey_;

        if (key.mode == InterpolationMode::Bezier) {
            // Handles are offsets in (time, value) space -- projected
            // through the same two mappings as the key itself, so a handle
            // stays visually attached at every zoom.
            const ImVec2 inPos(timeToX(key.timeSeconds + key.inHandle.x), valueToY(key.value + key.inHandle.y));
            const ImVec2 outPos(timeToX(key.timeSeconds + key.outHandle.x), valueToY(key.value + key.outHandle.y));
            const ImU32 handleColor = ImGui::GetColorU32(ImVec4(0.85f, 0.85f, 0.90f, isSelected ? 0.9f : 0.4f));
            dl->AddLine(keyPos, inPos, handleColor, 1.2f);
            dl->AddLine(keyPos, outPos, handleColor, 1.2f);
            dl->AddCircleFilled(inPos, 3.5f, handleColor);
            dl->AddCircleFilled(outPos, 3.5f, handleColor);

            if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                const float grab = view_.grabRadiusPixels;
                if (std::abs(mouse.x - inPos.x) < grab && std::abs(mouse.y - inPos.y) < grab) {
                    curveDragKey_ = static_cast<int>(k);
                    curveDragPart_ = -1;
                    selectedKey_ = static_cast<int>(k);
                } else if (std::abs(mouse.x - outPos.x) < grab && std::abs(mouse.y - outPos.y) < grab) {
                    curveDragKey_ = static_cast<int>(k);
                    curveDragPart_ = 1;
                    selectedKey_ = static_cast<int>(k);
                }
            }
        }

        dl->AddCircleFilled(keyPos, isSelected ? 5.5f : 4.0f, trackKindColor(track.kind));
        dl->AddCircle(keyPos, isSelected ? 5.5f : 4.0f, ImGui::GetColorU32(ImGuiCol_Text), 0, 1.4f);

        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && curveDragKey_ < 0) {
            const float grab = view_.grabRadiusPixels + 2.0f;
            if (std::abs(mouse.x - keyPos.x) < grab && std::abs(mouse.y - keyPos.y) < grab) {
                curveDragKey_ = static_cast<int>(k);
                curveDragPart_ = 0;
                selectedKey_ = static_cast<int>(k);
            }
        }
    }

    // --- handle / key dragging -------------------------------------------
    if (curveDragKey_ >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && curveDragKey_ < static_cast<int>(keys.size())) {
            Keyframe& key = keys[static_cast<size_t>(curveDragKey_)];
            const float mouseTime = pixelToTime(curveView, mouse.x - canvasPos.x);
            const float mouseValue = yToValue(mouse.y);
            if (curveDragPart_ == 0) {
                key.value = mouseValue;
                key.timeSeconds = dragTimeForPixel(curveView, mouse.x - canvasPos.x, sequence_.frameRate(),
                                                    snapToFrames_);
                curveDragKey_ = reorderDraggedKeyframe(keys, curveDragKey_);
                selectedKey_ = curveDragKey_;
            } else if (curveDragPart_ < 0) {
                // An in handle must stay on the incoming side of its key,
                // and an out handle on the outgoing side: letting one cross
                // over folds the segment back on itself and makes
                // bezierValueAtTime's inversion ambiguous.
                key.inHandle = glm::vec2(std::min(mouseTime - key.timeSeconds, -1e-3f), mouseValue - key.value);
            } else {
                key.outHandle = glm::vec2(std::max(mouseTime - key.timeSeconds, 1e-3f), mouseValue - key.value);
            }
        } else {
            curveDragKey_ = -1;
            curveDragPart_ = 0;
        }
    }

    // Playhead, mirrored from the timeline.
    const float playX = timeToX(sequence_.playheadSeconds());
    if (playX >= canvasPos.x && playX <= canvasPos.x + canvasW) {
        dl->AddLine(ImVec2(playX, canvasPos.y), ImVec2(playX, canvasPos.y + canvasH),
                    ImGui::GetColorU32(ImVec4(0.95f, 0.35f, 0.30f, 0.7f)), 1.4f);
    }
}

void MovieModePlugin::drawRailEditor() {
    ImGui::Checkbox("Show rail in viewport", &showRailGizmo_);
    ImGui::SameLine();
    ImGui::Checkbox("Look-at lines", &showLookAtLines_);
    helpMarker("Draws a line from each sampled camera position to what it is aiming at, so a rack focus or a "
               "look-at target is visible in the viewport rather than only in numbers.");

    CameraRailSettings settings = rail_.settings();
    bool changed = false;

    ImGui::SetNextItemWidth(140.0f);
    int splineType = static_cast<int>(settings.splineType);
    const char* splines[] = {"Catmull-Rom", "Bezier", "Linear"};
    if (ImGui::Combo("Spline", &splineType, splines, IM_ARRAYSIZE(splines))) {
        settings.splineType = static_cast<RailSplineType>(splineType);
        changed = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    int aimMode = static_cast<int>(settings.aimMode);
    const char* aims[] = {"Follow Path", "Look At Point", "Look At Target"};
    if (ImGui::Combo("Aim", &aimMode, aims, IM_ARRAYSIZE(aims))) {
        settings.aimMode = static_cast<RailAimMode>(aimMode);
        changed = true;
    }

    if (settings.aimMode != RailAimMode::FollowPath) {
        ImGui::SetNextItemWidth(220.0f);
        if (ImGui::DragFloat3("Look-at", &settings.lookAtTarget.x, 0.05f)) changed = true;
    }
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragFloat("Aim damping", &settings.aimDampingSeconds, 0.01f, 0.0f, 2.0f, "%.2f s")) changed = true;
    helpMarker("Seconds for the aim to catch up. 0 snaps; a small value is what makes a move read as operated rather "
               "than robotic.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::DragFloat("Roll", &settings.rollDegrees, 0.25f, -180.0f, 180.0f, "%.1f deg")) changed = true;

    if (changed) rail_.setSettings(settings);

    ImGui::Separator();
    ImGui::Text("Control points (%zu) -- length %.2f m", rail_.pointCount(),
                static_cast<double>(rail_.approximateLength()));

    std::vector<RailPoint> points = rail_.points();
    bool pointsChanged = false;
    int removeIndex = -1;
    for (size_t i = 0; i < points.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const bool sel = static_cast<int>(i) == selectedRailPoint_;
        if (ImGui::RadioButton("##sel", sel)) selectedRailPoint_ = static_cast<int>(i);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::DragFloat3("pos", &points[i].position.x, 0.05f)) pointsChanged = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::DragFloat("mm", &points[i].focalLengthMm, 0.5f, 8.0f, 300.0f, "%.0fmm")) pointsChanged = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(80.0f);
        if (ImGui::DragFloat("f/", &points[i].aperture, 0.05f, 0.95f, 22.0f, "f/%.1f")) pointsChanged = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) removeIndex = static_cast<int>(i);
        ImGui::PopID();
    }

    if (ImGui::Button("Add Point")) {
        RailPoint added;
        // Extends past the current end rather than landing on top of it,
        // which would create a zero-length segment.
        added.position = points.empty() ? glm::vec3(0.0f) : points.back().position + glm::vec3(2.0f, 0.0f, 0.0f);
        points.push_back(added);
        pointsChanged = true;
    }

    if (removeIndex >= 0) {
        points.erase(points.begin() + removeIndex);
        if (selectedRailPoint_ >= static_cast<int>(points.size())) selectedRailPoint_ = -1;
        pointsChanged = true;
    }
    if (pointsChanged) {
        rail_.clear();
        for (const RailPoint& p : points) rail_.addPoint(p);
    }
}

void MovieModePlugin::drawExporterModal() {
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Render Movie Sequence", &exporterOpen_, ImGuiWindowFlags_AlwaysAutoResize)) return;

    ImGui::TextDisabled("Offline render -- runs unthrottled, decoupled from real time.");
    ImGui::Separator();

    // --- resolution -------------------------------------------------------
    struct Preset {
        const char* label;
        ExportResolution resolution;
    };
    static const Preset kPresets[] = {
        {"1080p (1920x1080)", resolution_presets::k1080p},
        {"1440p (2560x1440)", resolution_presets::k1440p},
        {"4K (3840x2160)", resolution_presets::k4K},
        {"8K (7680x4320)", resolution_presets::k8K},
    };
    int presetIndex = -1;
    for (int i = 0; i < IM_ARRAYSIZE(kPresets); ++i) {
        if (kPresets[i].resolution.width == exportSettings_.resolution.width &&
            kPresets[i].resolution.height == exportSettings_.resolution.height) {
            presetIndex = i;
            break;
        }
    }
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("Resolution", presetIndex >= 0 ? kPresets[presetIndex].label : "Custom")) {
        for (int i = 0; i < IM_ARRAYSIZE(kPresets); ++i) {
            if (ImGui::Selectable(kPresets[i].label, i == presetIndex)) exportSettings_.resolution = kPresets[i].resolution;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    int dims[2] = {static_cast<int>(exportSettings_.resolution.width),
                   static_cast<int>(exportSettings_.resolution.height)};
    if (ImGui::DragInt2("##dims", dims, 1.0f, 1, 16384)) {
        exportSettings_.resolution.width = static_cast<uint32_t>(std::max(1, dims[0]));
        exportSettings_.resolution.height = static_cast<uint32_t>(std::max(1, dims[1]));
    }

    // --- frame rate & range -----------------------------------------------
    ImGui::SetNextItemWidth(200.0f);
    int rateIndex = static_cast<int>(exportSettings_.frameRate);
    const char* rateLabels[] = {"24 fps", "30 fps", "60 fps"};
    if (ImGui::Combo("Frame rate", &rateIndex, rateLabels, IM_ARRAYSIZE(rateLabels))) {
        exportSettings_.frameRate = static_cast<SequenceFrameRate>(rateIndex);
    }
    ImGui::SetNextItemWidth(200.0f);
    ImGui::DragFloat("Start (s)", &exportSettings_.startSeconds, 0.05f, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200.0f);
    ImGui::DragFloat("End (s)", &exportSettings_.endSeconds, 0.05f, 0.0f, 0.0f, "%.2f");
    ImGui::SameLine();
    helpMarker("End of 0 means \"to the end of the sequence\".");

    // --- channels ----------------------------------------------------------
    ImGui::Separator();
    ImGui::TextUnformatted("Channels");
    auto channelToggle = [&](ExportChannel channel, const char* label, const char* help) {
        bool enabled = std::find(exportSettings_.channels.begin(), exportSettings_.channels.end(), channel) !=
                        exportSettings_.channels.end();
        if (ImGui::Checkbox(label, &enabled)) {
            if (enabled) {
                exportSettings_.channels.push_back(channel);
            } else {
                exportSettings_.channels.erase(
                    std::remove(exportSettings_.channels.begin(), exportSettings_.channels.end(), channel),
                    exportSettings_.channels.end());
            }
        }
        if (help != nullptr) {
            ImGui::SameLine();
            helpMarker(help);
        }
    };
    channelToggle(ExportChannel::Color, "Color", nullptr);
    ImGui::SameLine();
    channelToggle(ExportChannel::Depth, "Depth", "Always written as EXR -- quantising depth to 8 bits destroys the "
                                                  "precision a compositor came for.");
    ImGui::SameLine();
    channelToggle(ExportChannel::MotionVectors, "Motion Vectors", "Always EXR, same reason as Depth.");

    ImGui::SetNextItemWidth(200.0f);
    int colorFormat = static_cast<int>(exportSettings_.colorFormat);
    const char* formats[] = {"PNG", "EXR"};
    if (ImGui::Combo("Color format", &colorFormat, formats, IM_ARRAYSIZE(formats))) {
        exportSettings_.colorFormat = static_cast<ExportImageFormat>(colorFormat);
    }

    // --- motion blur -------------------------------------------------------
    ImGui::Separator();
    ImGui::Checkbox("Motion blur", &exportSettings_.motionBlur.enabled);
    if (exportSettings_.motionBlur.enabled) {
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderInt("Sub-frame samples", &exportSettings_.motionBlur.subFrameSamples, 1, 64);
        ImGui::SameLine();
        helpMarker("Renders each output frame this many times across the open shutter and averages them. Linearly "
                   "more expensive.");
        ImGui::SetNextItemWidth(200.0f);
        ImGui::SliderFloat("Shutter", &exportSettings_.motionBlur.shutterAngleFraction, 0.0f, 1.0f, "%.2f");
        ImGui::SameLine();
        ImGui::TextDisabled("(%.0f deg)", static_cast<double>(exportSettings_.motionBlur.shutterAngleFraction * 360.0f));
    }

    ImGui::SetNextItemWidth(320.0f);
    char dirBuffer[512];
    std::snprintf(dirBuffer, sizeof(dirBuffer), "%s", exportSettings_.outputDirectory.c_str());
    if (ImGui::InputText("Output directory", dirBuffer, sizeof(dirBuffer))) exportSettings_.outputDirectory = dirBuffer;
    ImGui::SetNextItemWidth(320.0f);
    char prefixBuffer[256];
    std::snprintf(prefixBuffer, sizeof(prefixBuffer), "%s", exportSettings_.filePrefix.c_str());
    if (ImGui::InputText("File prefix", prefixBuffer, sizeof(prefixBuffer))) exportSettings_.filePrefix = prefixBuffer;

    // --- the cost, before committing to it ---------------------------------
    ImGui::Separator();
    std::string validationError;
    const bool valid = validateExportSettings(exportSettings_, validationError);
    if (valid) {
        const int frames = exportFrameCount(exportSettings_, sequence_.durationSeconds());
        const uint64_t perFrame = estimatedBytesPerFrame(exportSettings_);
        const double totalGb = static_cast<double>(perFrame) * static_cast<double>(frames) / (1024.0 * 1024.0 * 1024.0);
        ImGui::Text("%d frames, ~%.2f GB total", frames, totalGb);
        if (frames > 0) {
            ImGui::TextDisabled("First file: %s",
                                exportFrameFilename(exportSettings_, 0, exportSettings_.channels.empty()
                                                                            ? ExportChannel::Color
                                                                            : exportSettings_.channels.front())
                                    .c_str());
        }
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", validationError.c_str());
    }

    ImGui::Separator();
    ImGui::BeginDisabled(!valid);
    if (ImGui::Button("Render", ImVec2(120.0f, 0.0f))) {
        std::string error;
        if (buildExport(error)) {
            char status[256];
            std::snprintf(status, sizeof(status), "Scheduled %zu frames into \"%s\".", lastSchedule_.size(),
                          exportSettings_.outputDirectory.c_str());
            exportStatus_ = status;
        } else {
            exportStatus_ = "Export refused: " + error;
        }
        exporterOpen_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        exporterOpen_ = false;
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void MovieModePlugin::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::SetNextWindowSize(ImVec2(1020.0f, 620.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin(name());
    drawPluginHeader("Movie Mode");

    drawTransport();
    ImGui::Separator();

    if (ImGui::BeginTabBar("##moviemode")) {
        if (ImGui::BeginTabItem("Timeline")) {
            drawTimeline();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Curve Editor")) {
            drawCurveEditor();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Camera Rail")) {
            drawRailEditor();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // The modal is opened from drawTransport()'s button but must be begun
    // at this level: BeginPopupModal has to run every frame from the same
    // window, not from inside a tab that may not be selected.
    drawExporterModal();

    char footer[320];
    if (!exportStatus_.empty()) {
        std::snprintf(footer, sizeof(footer), "%s", exportStatus_.c_str());
    } else if (!lastFiredEvents_.empty()) {
        std::snprintf(footer, sizeof(footer), "Fired: %s", lastFiredEvents_.front().payload.c_str());
    } else {
        std::snprintf(footer, sizeof(footer), "%s | %zu tracks | %.2fs | zoom %.0f px/s",
                      sequence_.timecode().c_str(), sequence_.tracks().size(),
                      static_cast<double>(sequence_.durationSeconds()), static_cast<double>(view_.pixelsPerSecond));
    }
    drawPluginFooter(footer);
    ImGui::End();
}

} // namespace engine::studio::plugins
