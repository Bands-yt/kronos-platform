#pragma once

#include "core/GameMode.hpp"
#include "despair/HorrorAIManager.hpp"
#include "despair/LootSystem.hpp"
#include "despair/SanitySystem.hpp"

#include <string>

namespace engine::despair {

// PROJECT: DESPAIR: first-person rules, interaction, HUD and the escape loop.
class DespairGameMode final : public core::GameMode {
public:
    void beforeMovement(core::GameModeContext& ctx) override;
    void updateSystems(core::GameModeContext& ctx, float dt) override;
    void tickLookAt(core::GameModeContext& ctx, float dt, core::EntityId character, bool interactDown,
                    core::EntityId lookAtTarget) override;
    bool consumeInteractPress(core::GameModeContext& ctx, core::EntityId character,
                              const glm::vec3& characterPos) override;
    bool allowDoorToggle(core::GameModeContext& ctx, core::EntityId door, core::EntityId character) override;
    void onInteract(core::GameModeContext& ctx, core::EntityId target, core::EntityId character,
                    const glm::vec3& characterPos) override;
    void setInteractionPrompt(const std::string& prompt) override { interactionPrompt_ = prompt; }
    void afterPhysics(core::GameModeContext& ctx, float dt) override;
    [[nodiscard]] bool restartRequested() const override { return restartRequested_; }

private:
    void showFloatingText(const std::string& text);

    SanitySystem sanity_;
    HorrorAIManager ai_{sanity_};
    std::string interactionPrompt_;
    std::string floatingText_;
    float floatingTextTimer_ = 0.0f;
    float baseRunSpeed_ = 0.0f;
    float elapsedSeconds_ = 0.0f;
    bool restartRequested_ = false;
    bool toggleMenuWasDown_ = false;
    bool hotbarKeyWasDown_[kHotbarSlotCount] = {};
};

} // namespace engine::despair
