#include "core/ECS.hpp"
#include "core/HotReloadModuleAbi.hpp"
#include "StatefulState.hpp"

namespace {

class StatefulModuleB final : public engine::core::IHotReloadableModule {
public:
    void onLoad(engine::core::ECS&) override { kronosState<StatefulCounter>().label += "B"; }
    void tick(float, engine::core::ECS& ecs) override {
        auto& state = kronosState<StatefulCounter>();
        state.ticks += 10;
        state.history.push_back(state.ticks);
        for (auto entity : ecs.view<StatefulProbe>()) {
            auto& probe = ecs.raw().get<StatefulProbe>(entity);
            probe.ticks = state.ticks;
            probe.historySize = static_cast<int>(state.history.size());
            probe.label = state.label;
        }
    }
};

} // namespace

KRONOS_HOT_RELOAD_STATE(StatefulCounter, 1)

extern "C" int kronosHotReloadAbiVersion() { return engine::core::kHotReloadModuleAbiVersion; }
extern "C" engine::core::IHotReloadableModule* kronosCreateHotReloadModule() { return new StatefulModuleB(); }
extern "C" void kronosDestroyHotReloadModule(engine::core::IHotReloadableModule* module) { delete module; }
