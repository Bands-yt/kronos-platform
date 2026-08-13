#include "studio/plugins/DiagnosticsPlugin.hpp"

#include <cstdio>

#include <imgui.h>

#include "core/Components.hpp"
#include "core/Interactable.hpp"
#include "core/Navigation.hpp"
#include "core/OreNode.hpp"
#include "core/Shop.hpp"
#include "core/Terrain.hpp"
#include "core/WorldProp.hpp"
#include "studio/EntityClassification.hpp"
#include "studio/PluginChrome.hpp"

namespace engine::studio::plugins {

DiagnosticsPlugin::DiagnosticsPlugin(core::Profiler& profiler, const core::PerformanceMetrics& metrics,
                                      const NotificationCenter& notifications)
    : profiler_(&profiler), metrics_(&metrics), notifications_(&notifications) {}

void DiagnosticsPlugin::drawMetricsSection() const {
    if (!ImGui::CollapsingHeader("Renderer / Physics / Terrain / Process", ImGuiTreeNodeFlags_DefaultOpen)) return;
    ImGui::Text("%.1f fps (%.2f ms)", metrics_->fps, metrics_->frameTimeMs);
    ImGui::Text("Draw calls: %u | Triangles: %llu", metrics_->drawCalls,
                static_cast<unsigned long long>(metrics_->triangleCount));
    ImGui::Text("GPU memory: %.0f / %.0f MB", static_cast<double>(metrics_->gpuMemoryUsedBytes) / (1024.0 * 1024.0),
                static_cast<double>(metrics_->gpuMemoryBudgetBytes) / (1024.0 * 1024.0));
    ImGui::Separator();
    ImGui::Text("Physics bodies: %u active / %u total", metrics_->activePhysicsBodies, metrics_->totalPhysicsBodies);
    ImGui::Text("Terrain chunks: %u loaded / %u total", metrics_->loadedTerrainChunks, metrics_->totalTerrainChunks);
    ImGui::Separator();
    ImGui::Text("Process: %.0f MB RSS, %.1f%% CPU", static_cast<double>(metrics_->processMemoryBytes) / (1024.0 * 1024.0),
                static_cast<double>(metrics_->processCpuPercent));
    ImGui::Separator();
    ImGui::Text("Notifications in queue: %zu", notifications_->count());
}

void DiagnosticsPlugin::drawProfilerSection() {
    if (!ImGui::CollapsingHeader("Profiler", ImGuiTreeNodeFlags_DefaultOpen)) return;

    const auto& events = profiler_->events();
    int spikeCount = 0, stallCount = 0;
    for (const auto& event : events) {
        if (event.kind == core::ProfilerEventKind::Spike) ++spikeCount;
        if (event.kind == core::ProfilerEventKind::StallStart) ++stallCount;
    }
    ImGui::Text("Logged events: %zu (%d spikes, %d stalls)", events.size(), spikeCount, stallCount);
    if (!events.empty()) {
        const core::ProfilerEvent& latest = events.back();
        const char* kindName = latest.kind == core::ProfilerEventKind::Spike     ? "Spike"
                                : latest.kind == core::ProfilerEventKind::StallStart ? "Stall start"
                                                                                       : "Stall end";
        ImGui::Text("Most recent: %s at %.2f ms", kindName, latest.frameTimeMs);
    }
    if (ImGui::Button("Clear Events")) profiler_->clearEvents();

    ImGui::Separator();
    ImGui::TextUnformatted("Performance Recording");
    bool recording = profiler_->isRecording();
    if (recording) {
        ImGui::TextColored(ImVec4(0.90f, 0.30f, 0.30f, 1.0f), "Recording... %zu samples",
                            profiler_->recordingSampleCount());
        if (ImGui::Button("Stop Recording")) profiler_->stopRecording();
    } else {
        if (ImGui::Button("Start Recording")) {
            profiler_->clearRecording();
            profiler_->startRecording();
            lastRecordingStatus_.clear();
        }
    }
    ImGui::InputText("##recording_path", recordingPathBuffer_, sizeof(recordingPathBuffer_));
    ImGui::SameLine();
    bool canSave = !recording && profiler_->recordingSampleCount() > 0;
    ImGui::BeginDisabled(!canSave);
    if (ImGui::Button("Save JSON")) {
        bool ok = profiler_->writeRecordingToJsonFile(recordingPathBuffer_);
        lastRecordingStatus_ = ok ? "Saved." : "Failed to write file.";
    }
    ImGui::EndDisabled();
    if (!lastRecordingStatus_.empty()) ImGui::TextDisabled("%s", lastRecordingStatus_.c_str());
}

namespace {
// One line per real component kind this engine actually defines and
// Explorer/Inspector already treat as meaningful (see
// EntityClassification.cpp's own priority ladder for the same set) --
// a real, live checklist, not a placeholder property sheet.
template <typename T>
void componentRow(core::ECS& ecs, core::EntityId entity, const char* label) {
    bool has = ecs.hasComponent<T>(entity);
    ImGui::TextColored(has ? ImVec4(0.35f, 0.80f, 0.40f, 1.0f) : ImVec4(0.45f, 0.46f, 0.50f, 1.0f), "%s %s",
                        has ? "[x]" : "[ ]", label);
}
} // namespace

void DiagnosticsPlugin::drawSelectedEntitySection(core::ECS& ecs, core::EntityId selected) const {
    if (!ImGui::CollapsingHeader("Selected Entity", ImGuiTreeNodeFlags_DefaultOpen)) return;
    if (selected == core::kNullEntity) {
        ImGui::TextDisabled("Nothing selected.");
        return;
    }

    ImGui::Text("Entity ID: %u", static_cast<uint32_t>(selected));
    EntityCategory category = classifyEntity(ecs, selected);
    ImGui::Text("Category: %s", categoryDisplayName(category));

    ImGui::Separator();
    componentRow<core::Transform>(ecs, selected, "Transform");
    componentRow<core::Renderable>(ecs, selected, "Renderable");
    componentRow<core::ColliderShape>(ecs, selected, "ColliderShape");
    componentRow<core::RigidBody>(ecs, selected, "RigidBody");
    componentRow<core::Interactable>(ecs, selected, "Interactable");
    componentRow<core::WorldProp>(ecs, selected, "WorldProp");
    componentRow<core::OreNode>(ecs, selected, "OreNode");
    componentRow<core::ShopStall>(ecs, selected, "ShopStall");
    componentRow<core::UpgradeStation>(ecs, selected, "UpgradeStation");
    componentRow<core::TeleportPad>(ecs, selected, "TeleportPad");
    componentRow<core::NavMarker>(ecs, selected, "NavMarker");
    componentRow<core::TerrainChunkTag>(ecs, selected, "TerrainChunkTag");
}

void DiagnosticsPlugin::drawPanel(core::ECS& ecs, core::EntityId selected, const std::vector<core::EntityId>&) {
    ImGui::Begin(name());
    drawPluginHeader("Diagnostics");

    size_t entityCount = 0;
    for (auto entity : ecs.view<core::Transform>()) {
        (void)entity;
        ++entityCount;
    }
    ImGui::Text("ECS entities: %zu", entityCount);
    ImGui::Separator();

    drawMetricsSection();
    drawProfilerSection();
    drawSelectedEntitySection(ecs, selected);

    drawPluginFooter();
    ImGui::End();
}

} // namespace engine::studio::plugins
