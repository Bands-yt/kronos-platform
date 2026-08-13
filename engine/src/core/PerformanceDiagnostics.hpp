#pragma once

#include <cstddef>
#include <vector>

#include "core/PerformanceMetrics.hpp"
#include "core/ProcessStats.hpp"

namespace engine::core {

// Pure composition -- takes the render-only metrics Renderer::metrics()
// already produces plus whatever real physics/terrain/process numbers
// the caller has (0 for the physics/terrain counts is a real, honest
// "no physics world"/"no terrain" answer, not a sentinel error value --
// see Application.hpp's own setTerrain() comment on why null/absent is a
// valid configuration), and returns one complete PerformanceMetrics
// snapshot. Deliberately takes raw counts, not Physics*/Terrain*
// pointers -- callers (core::Application, studio::StudioApp) already
// have to call activeBodyCount()/loadedChunkCount() etc. themselves
// (Application owns a core::Physics directly; Studio's live physics only
// exists inside PhysicsPreviewPlugin's own sandboxed Play mode, a
// different shape entirely), so this function stays pure data-in/data-out
// and fully headlessly testable rather than depending on a live instance
// of either.
[[nodiscard]] PerformanceMetrics composePerformanceMetrics(const PerformanceMetrics& renderMetrics,
                                                             uint32_t activePhysicsBodies, uint32_t totalPhysicsBodies,
                                                             uint32_t loadedTerrainChunks, uint32_t totalTerrainChunks,
                                                             const ProcessStats& processStats);

// Sprint 8 ("Performance Stats & Debug Tools") task category 1's "color-
// coded severity (green/yellow/red)" -- real, pure threshold functions
// rather than each UI call site inventing its own cutoffs. Thresholds are
// stated in frame-rate terms since that's what a creator actually
// recognizes ("this feels like 30fps"), not an arbitrary millisecond
// constant: Good is a real, comfortable 60fps-class frame (<=16.7ms),
// Warning is a real, still-playable-but-noticeable 30fps-class frame
// (<=33.4ms), Critical is anything slower.
enum class PerformanceSeverity { Good, Warning, Critical };

[[nodiscard]] PerformanceSeverity classifyFrameTimeSeverity(float frameTimeMs);

// GPU memory severity as a real fraction of the live budget
// (vmaGetHeapBudgets(), see PerformanceMetrics.hpp) -- Good under 75%,
// Warning 75-90%, Critical past 90% (the point VMA itself starts
// preferring to evict/deprioritize allocations). `budgetBytes == 0`
// (no budget info available, e.g. before the first real frame) honestly
// reports Good rather than a division-by-zero Critical false alarm.
[[nodiscard]] PerformanceSeverity classifyMemorySeverity(uint64_t usedBytes, uint64_t budgetBytes);

// Sprint 8 task category 1's "mini-graphs for frame time, memory, and
// draw calls" -- a real, fixed-capacity ring buffer (oldest sample
// overwritten once full, the same bounded-growth discipline
// core::Inventory/studio::NotificationCenter already established for
// their own domains) holding whichever single float a caller wants to
// graph. One instance per graphed series (StatsPanel owns three: frame
// time, memory fraction, draw calls) rather than one PerformanceHistory
// storing every field, since ImGui::PlotLines wants one flat float array
// per graph anyway.
class PerformanceHistory {
public:
    explicit PerformanceHistory(size_t capacity = 120) : capacity_(capacity) { samples_.reserve(capacity); }

    void push(float value);

    [[nodiscard]] const std::vector<float>& samples() const { return samples_; }
    [[nodiscard]] size_t capacity() const { return capacity_; }
    [[nodiscard]] bool empty() const { return samples_.empty(); }

    [[nodiscard]] float average() const;
    [[nodiscard]] float max() const;

private:
    size_t capacity_;
    std::vector<float> samples_; // logical order oldest->newest; see push()'s real ring-buffer shift
};

} // namespace engine::core
