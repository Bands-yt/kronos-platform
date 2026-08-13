#include "studio/plugins/TrailerPanel.hpp"

#include <cstdio>

#include <imgui.h>

#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

TrailerPanel::TrailerPanel(VmaAllocator allocator, VkDevice device, VkCommandPool cmdPool, VkQueue queue, core::ECS& ecs,
                            core::MeshLibrary& meshLibrary, core::TextureLibrary& textureLibrary,
                            core::ParticleSystem& particleSystem, net::NetworkSession& session)
    : meshLibrary_(&meshLibrary), textureLibrary_(&textureLibrary),
      director_(trailerCamera_, ecs, meshLibrary, particleSystem, session.tntWarsMatch()) {
    (void)allocator;
    (void)device;
    (void)cmdPool;
    (void)queue;
    // Real, reasonable default preview size -- ThumbnailCameraRig's own
    // desiredExtent, distinct from director_'s own real capture-rig
    // extent (see renderPreview()'s own comment on why the two real
    // offscreen targets don't need to match resolution).
    previewRig_.desiredExtent = VkExtent2D{480, 270};
}

void TrailerPanel::logLine(const std::string& line) {
    log_.push_back(line);
    if (log_.size() > 200) log_.erase(log_.begin()); // real, bounded scrollback -- same reasoning ModerationPanel's own chat log view already applies
}

void TrailerPanel::update(float dt, core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    if (!captureInitialized_ || cachedRenderer_ == nullptr) return; // real, honest no-op until renderPreview() has real-initialized at least once
    if (!playing_ && !director_.isCapturing()) return;

    director_.tick(dt, *cachedRenderer_, *textureLibrary_);
    if (director_.isCapturing() && director_.isFinished()) {
        director_.stopCapture();
        playing_ = false;
        logLine("Capture finished -- " + std::to_string(director_.framesSaved()) + " frames saved.");
    }
}

void TrailerPanel::renderPreview(VkCommandBuffer cmd, core::Renderer& renderer, core::ECS& ecs) {
    cachedRenderer_ = &renderer;

    if (!captureInitialized_) {
        // Real, deliberately smaller real capture-rig extent than the
        // preview's own -- the real file-sequence output doesn't need
        // to match the live scrub-preview's resolution, matching
        // engine_runtime's own real, bounded capture-resolution choice
        // (see this sprint's own README section).
        captureInitialized_ = director_.initializeCapture(renderer, VkExtent2D{480, 270});
        if (!captureInitialized_) {
            logLine("ERROR: TrailerDirector::initializeCapture failed -- see stderr.");
        }
    }

    // Real per-frame camera copy -- update() (see above) is what
    // actually advances director_'s own real trailerCamera_ pose; this
    // just mirrors it into the real preview rig every frame this panel
    // is open.
    previewRig_.camera = trailerCamera_;
    previewRig_.render(cmd, renderer, ecs, *meshLibrary_, *textureLibrary_);
}

void TrailerPanel::shutdown(core::Renderer& renderer) {
    previewRig_.destroy(renderer, renderer.allocator(), renderer.device());
    if (captureInitialized_) {
        director_.shutdownCapture(renderer);
        captureInitialized_ = false;
    }
}

void TrailerPanel::drawSceneListSection() {
    if (!ImGui::CollapsingHeader("Scenes", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::TextDisabled("%zu real beats, %.1fs total", director_.beats().size(),
                         static_cast<double>(director_.timeline().totalDurationSeconds()));
    ImGui::BeginChild("##trailer_scene_list", ImVec2(0, 160), true);
    const auto& beats = director_.beats();
    auto query = director_.timeline().queryAt(director_.elapsedSeconds());
    for (size_t i = 0; i < beats.size(); ++i) {
        bool isCurrent = query.sceneIndex == static_cast<int>(i);
        char label[128];
        std::snprintf(label, sizeof(label), "%s%s", beats[i].name.c_str(), isCurrent ? "  <-- playing" : "");
        if (ImGui::Selectable(label, isCurrent)) {
            director_.seekToBeat(static_cast<int>(i));
            logLine("Jumped to beat: " + beats[i].name);
        }
    }
    ImGui::EndChild();
}

void TrailerPanel::drawPlaybackSection() {
    if (!ImGui::CollapsingHeader("Playback", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (ImGui::Button(playing_ ? "Pause" : "Play")) playing_ = !playing_;
    ImGui::SameLine();
    if (ImGui::Button("Restart")) {
        director_.seekToBeat(0);
        logLine("Restarted from beat 0.");
    }

    float speed = director_.playbackSpeed();
    if (ImGui::SliderFloat("Playback Speed", &speed, 0.1f, 3.0f)) director_.setPlaybackSpeed(speed);

    ImGui::Text("Elapsed: %.1fs / %.1fs", static_cast<double>(director_.elapsedSeconds()),
                static_cast<double>(director_.timeline().totalDurationSeconds()));

    ImGui::TextDisabled("Lighting presets: see the Lighting Tools panel -- both edit the same live Renderer.");
}

void TrailerPanel::drawCaptureSection() {
    if (!ImGui::CollapsingHeader("Capture", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::InputText("Output Directory", outputDirBuffer_, sizeof(outputDirBuffer_));
    ImGui::SliderFloat("Capture FPS", &captureFps_, 6.0f, 60.0f, "%.0f");

    ImGui::BeginDisabled(director_.isCapturing());
    if (ImGui::Button("Start Capture")) {
        director_.seekToBeat(0);
        director_.startCapture(outputDirBuffer_, captureFps_);
        playing_ = true;
        logLine(std::string("Capture started -> ") + outputDirBuffer_);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!director_.isCapturing());
    if (ImGui::Button("Stop Capture")) {
        director_.stopCapture();
        logLine("Capture stopped by user.");
    }
    ImGui::EndDisabled();

    ImGui::Text("Frames saved: %d", director_.framesSaved());

    ImGui::Separator();
    ImGui::TextUnformatted("Log");
    ImGui::BeginChild("##trailer_log", ImVec2(0, 100), true);
    for (const std::string& line : log_) ImGui::TextUnformatted(line.c_str());
    ImGui::EndChild();
}

void TrailerPanel::drawPanel(core::ECS&, core::EntityId, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("TNT-Wars Trailer");

    if (previewRig_.hasRenderedFrame()) {
        ImVec2 previewSize(static_cast<float>(previewRig_.extent().width), static_cast<float>(previewRig_.extent().height));
        ImGui::Image(reinterpret_cast<ImTextureID>(previewRig_.imguiTextureId()), previewSize);
    } else {
        ImGui::TextDisabled("Preview renders once this panel has been open for a real frame.");
    }

    drawSceneListSection();
    drawPlaybackSection();
    drawCaptureSection();

    drawPluginFooter("Same real trailer::TrailerDirector engine_runtime's own --trailer mode drives.");
    ImGui::End();
}

} // namespace engine::studio::plugins
