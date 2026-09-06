// Real, minimal IHotReloadableModule -- "V2" behavior, a deliberately
// different increment so the test can tell which real code is running.
// See CounterComponent.hpp's own comment for what this fixture pair
// proves.
#include "core/ECS.hpp"
#include "core/HotReloadModuleAbi.hpp"
#include "CounterComponent.hpp"

namespace {

class CounterModuleV2 final : public engine::core::IHotReloadableModule {
public:
    void tick(float /*dt*/, engine::core::ECS& ecs) override {
        auto view = ecs.view<CounterComponent>();
        for (auto entity : view) {
            view.get<CounterComponent>(entity).value += 100; // V2: +100 per tick
        }
    }
};

} // namespace

extern "C" int kronosHotReloadAbiVersion() { return engine::core::kHotReloadModuleAbiVersion; }
extern "C" engine::core::IHotReloadableModule* kronosCreateHotReloadModule() { return new CounterModuleV2(); }
extern "C" void kronosDestroyHotReloadModule(engine::core::IHotReloadableModule* module) { delete module; }
