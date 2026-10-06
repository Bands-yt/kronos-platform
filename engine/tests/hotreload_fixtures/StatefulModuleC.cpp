#include <utility>

#include "core/ECS.hpp"
#include "core/HotReloadModuleAbi.hpp"
#include "StatefulState.hpp"

namespace {

class StatefulModuleC final : public engine::core::IHotReloadableModule {
public:
    void tick(float, engine::core::ECS& ecs) override {
        auto& state = kronosState<StatefulCounterV2>();
        state.ticks += 1000;
        state.history.push_back(static_cast<int>(state.ticks));
        for (auto entity : ecs.view<StatefulProbe>()) {
            auto& probe = ecs.raw().get<StatefulProbe>(entity);
            probe.ticks = static_cast<int>(state.ticks);
            probe.historySize = static_cast<int>(state.history.size());
            probe.label = state.label + "+v" + std::to_string(state.migratedFromVersion);
        }
    }
};

} // namespace

KRONOS_HOT_RELOAD_STATE(StatefulCounterV2, 2)

extern "C" int kronosHotReloadMigrateState(const KronosHotReloadStateLayout* oldLayout, void* oldState, void* newState) {
    if (oldLayout->version != 1 || oldLayout->size != sizeof(StatefulCounter)) return 0;
    auto& from = *static_cast<StatefulCounter*>(oldState);
    auto& to = *static_cast<StatefulCounterV2*>(newState);
    to.ticks = from.ticks;
    to.history = std::move(from.history);
    to.label = from.label;
    to.migratedFromVersion = 1;
    return 1;
}

extern "C" int kronosHotReloadAbiVersion() { return engine::core::kHotReloadModuleAbiVersion; }
extern "C" engine::core::IHotReloadableModule* kronosCreateHotReloadModule() { return new StatefulModuleC(); }
extern "C" void kronosDestroyHotReloadModule(engine::core::IHotReloadableModule* module) { delete module; }
