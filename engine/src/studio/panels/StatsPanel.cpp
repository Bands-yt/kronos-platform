#include "studio/panels/StatsPanel.hpp"

#include <algorithm>

#include <imgui.h>

namespace engine::studio::panels {

namespace {
// Real, distinct green/yellow/red -- task category 1's "color-coded
// severity (green/yellow/red)", not a single accent tinted three
// different alphas. Deliberately the same hue family
// studio::Notification's severity colors already use (Warning/Error),
// so a "this is bad" signal reads consistently across every panel in
// Studio, not just this one.
ImVec4 severityColor(core::PerformanceSeverity severity) {
    switch (severity) {
        case core::PerformanceSeverity::Good: return ImVec4(0.35f, 0.80f, 0.40f, 1.0f);
        case core::PerformanceSeverity::Warning: return ImVec4(0.90f, 0.75f, 0.25f, 1.0f);
        case core::PerformanceSeverity::Critical: return ImVec4(0.90f, 0.30f, 0.30f, 1.0f);
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}
} // namespace

void StatsPanel::draw(const core::PerformanceMetrics& metrics) {
    ImGui::Begin("Stats");

    core::PerformanceSeverity frameSeverity = core::classifyFrameTimeSeverity(metrics.frameTimeMs);
    ImGui::TextColored(severityColor(frameSeverity), "%.1f fps (%.2f ms)", metrics.fps, metrics.frameTimeMs);
    frameTimeHistory_.push(metrics.frameTimeMs);
    ImGui::PlotLines("##frametime_graph", frameTimeHistory_.samples().data(),
                      static_cast<int>(frameTimeHistory_.samples().size()), 0, nullptr, 0.0f,
                      std::max(33.4f, frameTimeHistory_.max()), ImVec2(0.0f, 40.0f));

    ImGui::Separator();
    ImGui::Text("Draw calls: %u", metrics.drawCalls);
    drawCallHistory_.push(static_cast<float>(metrics.drawCalls));
    ImGui::PlotLines("##drawcalls_graph", drawCallHistory_.samples().data(),
                      static_cast<int>(drawCallHistory_.samples().size()), 0, nullptr, 0.0f,
                      std::max(1.0f, drawCallHistory_.max()), ImVec2(0.0f, 40.0f));
    ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(metrics.triangleCount));

    ImGui::Separator();
    double usedMb = static_cast<double>(metrics.gpuMemoryUsedBytes) / (1024.0 * 1024.0);
    double budgetMb = static_cast<double>(metrics.gpuMemoryBudgetBytes) / (1024.0 * 1024.0);
    core::PerformanceSeverity memSeverity = core::classifyMemorySeverity(metrics.gpuMemoryUsedBytes, metrics.gpuMemoryBudgetBytes);
    ImGui::TextColored(severityColor(memSeverity), "GPU memory: %.0f / %.0f MB", usedMb, budgetMb);
    float memFraction = metrics.gpuMemoryBudgetBytes > 0
                             ? static_cast<float>(static_cast<double>(metrics.gpuMemoryUsedBytes) /
                                                   static_cast<double>(metrics.gpuMemoryBudgetBytes))
                             : 0.0f;
    ImGui::ProgressBar(memFraction);
    memoryFractionHistory_.push(memFraction);
    ImGui::PlotLines("##memory_graph", memoryFractionHistory_.samples().data(),
                      static_cast<int>(memoryFractionHistory_.samples().size()), 0, nullptr, 0.0f, 1.0f,
                      ImVec2(0.0f, 40.0f));

    ImGui::Separator();
    ImGui::Text("Physics bodies: %u active / %u total", metrics.activePhysicsBodies, metrics.totalPhysicsBodies);
    ImGui::Text("Terrain chunks: %u loaded / %u total", metrics.loadedTerrainChunks, metrics.totalTerrainChunks);

    ImGui::Separator();
    ImGui::Text("Process: %.0f MB RSS, %.1f%% CPU", static_cast<double>(metrics.processMemoryBytes) / (1024.0 * 1024.0),
                static_cast<double>(metrics.processCpuPercent));

    ImGui::End();
}

} // namespace engine::studio::panels
