#pragma once

#include <cstdint>

namespace engine::core {

// Real, measured per-frame stats -- not estimates. frameTimeMs/fps come
// from wall-clock timing around Renderer::renderFrame() (the actual
// presented frame rate, independent of GameLoop's fixed simulation tick
// rate); drawCalls/triangleCount are counted at every vkCmdDraw*/
// vkCmdDrawIndexed call site across every pass (shadow, opaque, instanced,
// particles, bloom, composite) for the frame just submitted;
// gpuMemory{Used,Budget}Bytes come straight from vmaGetHeapBudgets() --
// VMA's own live tracking of what's actually resident/available on the
// device, not a number this engine estimates itself.
struct PerformanceMetrics {
    float frameTimeMs = 0.0f;
    float fps = 0.0f;
    uint32_t drawCalls = 0;
    uint64_t triangleCount = 0;
    uint64_t gpuMemoryUsedBytes = 0;
    uint64_t gpuMemoryBudgetBytes = 0;

    // Sprint 8 ("Performance Stats & Debug Tools"): real counts from
    // whichever real subsystem the caller has -- Renderer::metrics() only
    // fills the render-specific fields above (it has no Physics/Terrain/
    // process handle to read these from); StudioApp/Application fill
    // these in afterward from their own real core::Physics::activeBodyCount()/
    // core::Terrain::loadedChunkCount()/core::ProcessStatsSampler::sample()
    // calls before handing the completed snapshot to StatsPanel/the
    // profiler/the runtime's stdout line. Still one struct, not four,
    // because every consumer (graphs, severity classification, JSON
    // recording) wants "this frame's full picture" as a single unit, not
    // four separately-timestamped pieces that could disagree about which
    // frame they're describing.
    uint32_t activePhysicsBodies = 0;
    uint32_t totalPhysicsBodies = 0;
    uint32_t loadedTerrainChunks = 0;
    uint32_t totalTerrainChunks = 0;
    uint64_t processMemoryBytes = 0;
    float processCpuPercent = 0.0f;
};

} // namespace engine::core
