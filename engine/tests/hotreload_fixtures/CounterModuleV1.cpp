// Real, minimal IHotReloadableModule -- "V1" behavior, see
// CounterComponent.hpp's own comment for what this fixture pair proves.
#include "core/ECS.hpp"
#include "core/HotReloadModuleAbi.hpp"
#include "CounterComponent.hpp"

namespace {

class CounterModuleV1 final : public engine::core::IHotReloadableModule {
public:
    void tick(float /*dt*/, engine::core::ECS& ecs) override {
        auto view = ecs.view<CounterComponent>();
        for (auto entity : view) {
            view.get<CounterComponent>(entity).value += 1; // V1: +1 per tick
        }
    }
};

} // namespace

extern "C" int kronosHotReloadAbiVersion() { return engine::core::kHotReloadModuleAbiVersion; }
extern "C" engine::core::IHotReloadableModule* kronosCreateHotReloadModule() { return new CounterModuleV1(); }
extern "C" void kronosDestroyHotReloadModule(engine::core::IHotReloadableModule* module) { delete module; }
