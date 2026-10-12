#include "despair/DespairGameMode.hpp"

#include "core/Camera.hpp"
#include "core/CharacterController.hpp"
#include "core/Components.hpp"
#include "core/Interactable.hpp"
#include "core/Physics.hpp"
#include "core/Renderer.hpp"
#include "core/Scripting.hpp"
#include "core/UIRenderer.hpp"
#include "despair/EscapeGameLoop.hpp"
#include "despair/FPSPlayerController.hpp"
#include "despair/InteractionSystem.hpp"
#include "despair/VhsBodycamEffects.hpp"
#include "platform_adapters/UnifiedInput.hpp"

#include <SDL2/SDL.h>

namespace engine::despair {

using namespace core;

void DespairGameMode::showFloatingText(const std::string& text) {
    floatingText_ = text;
    floatingTextTimer_ = 3.0f;
}

void DespairGameMode::beforeMovement(GameModeContext& ctx) {
    ECS& ecs = ctx.ecs;
    CharacterController& controller = ctx.controller;
    if (ecs.tryGetComponent<FPSPlayerSettings>(controller.entity()) != nullptr) {
        configureFirstPersonCamera(controller);

        if (baseRunSpeed_ <= 0.0f) baseRunSpeed_ = controller.settings().runSpeed;
        auto* stamina = ecs.tryGetComponent<StaminaState>(controller.entity());
        bool exhausted = stamina != nullptr && stamina->exhausted;
        controller.settingsMutable().runSpeed =
            exhausted ? controller.settings().walkSpeed : baseRunSpeed_;
    }}

void DespairGameMode::updateSystems(GameModeContext& ctx, float dt) {
    sanity_.update(dt, ctx.ecs);
}

void DespairGameMode::tickLookAt(GameModeContext& ctx, float dt, EntityId character, bool interactDown,
                                 EntityId lookAtTarget) {
    ECS& ecs = ctx.ecs;
    for (EntityId searchedContainer : tickContainerSearches(dt, ecs, interactDown, lookAtTarget)) {
        ctx.scripting.fireInteract(static_cast<uint32_t>(searchedContainer), static_cast<uint32_t>(character));
        std::string lootDescription = grantContainerLoot(ecs, searchedContainer, character, sanity_);
        showFloatingText(lootDescription.empty() ? "Searched. Nothing here." : lootDescription);
    }}

bool DespairGameMode::consumeInteractPress(GameModeContext& ctx, EntityId character, const glm::vec3& characterPos) {
    ECS& ecs = ctx.ecs;
    Physics& physics = ctx.physics;
    auto* hidingState = ecs.tryGetComponent<PlayerHidingState>(character);
    if (hidingState != nullptr && hidingState->currentSpot != kNullEntity) {
        if (auto* spot = ecs.tryGetComponent<HidingSpot>(hidingState->currentSpot)) {
            if (tryToggleHiding(*spot, hidingState->currentSpot, *hidingState, characterPos) ==
                HidingTransition::Exited) {
                physics.setPosition(character, ecs, hidingState->positionBeforeHiding);
                showFloatingText("Left hiding spot.");
            }
        }
        return true;
    }    return false;
}

bool DespairGameMode::allowDoorToggle(GameModeContext& ctx, EntityId target, EntityId character) {
    ECS& ecs = ctx.ecs;
    Physics& physics = ctx.physics;
    bool canToggle = true;
    if (auto* lockedDoor = ecs.tryGetComponent<LockedDoor>(target)) {
        auto* keycardInventory = ecs.tryGetComponent<KeycardInventory>(character);
        KeycardInventory emptyInventory;
        const KeycardInventory& inventory =
            keycardInventory != nullptr ? *keycardInventory : emptyInventory;

        DoorUnlockResult unlockResult;
        bool isBlastDoor = ecs.hasComponent<BlastDoorTag>(target);
        if (isBlastDoor) {
            auto* objective = ecs.tryGetComponent<ObjectiveManager>(character);
            ObjectiveManager emptyObjective;
            const ObjectiveManager& objectiveRef =
                objective != nullptr ? *objective : emptyObjective;
            unlockResult = tryUnlockExitWithObjective(*lockedDoor, objectiveRef);
        } else {
            unlockResult = tryUnlockDoor(*lockedDoor, inventory);
        }
        if (unlockResult == DoorUnlockResult::Unlocked) {
            physics.detachBody(target, ecs);
            showFloatingText("Unlocked.");
        } else if (unlockResult == DoorUnlockResult::DeniedMissingKeycard) {
            canToggle = false;
            if (isBlastDoor) {
                auto* objective = ecs.tryGetComponent<ObjectiveManager>(character);
                int collected = objective != nullptr ? objective->keycardsCollected : 0;
                showFloatingText("Locked -- need " +
                                         std::to_string(kKeycardsRequiredForExit - collected) +
                                         " more keycard(s).");
            } else {
                showFloatingText(std::string("Locked -- requires ") +
                                         keycardTierName(lockedDoor->requiredTier) + " keycard.");
            }
        }
    }    return canToggle;
}

void DespairGameMode::onInteract(GameModeContext& ctx, EntityId target, EntityId character,
                                 const glm::vec3& characterPos) {
    ECS& ecs = ctx.ecs;
    Physics& physics = ctx.physics;
    if (auto* keycard = ecs.tryGetComponent<Keycard>(target)) {
        auto* keycardInventory = ecs.tryGetComponent<KeycardInventory>(character);
        if (keycardInventory == nullptr) keycardInventory = &ecs.addComponent<KeycardInventory>(character);
        if (addKeycardTier(*keycardInventory, keycard->tier)) {
            addKeycardToObjective(ecs, character);
            showFloatingText(std::string("Picked up ") + keycardTierName(keycard->tier) + " keycard.");
        }
    }    if (auto* spot = ecs.tryGetComponent<HidingSpot>(target)) {
        auto* hidingState = ecs.tryGetComponent<PlayerHidingState>(character);
        if (hidingState == nullptr) hidingState = &ecs.addComponent<PlayerHidingState>(character);
        HidingTransition result = tryToggleHiding(*spot, target, *hidingState, characterPos);
        if (result == HidingTransition::Entered) {
            physics.setPosition(character, ecs, spot->interiorPosition);
            physics.setHorizontalVelocity(character, ecs, {0.0f, 0.0f});
            physics.setVerticalVelocity(character, ecs, 0.0f);
            showFloatingText("Hiding...");
        } else if (result == HidingTransition::Denied) {
            showFloatingText("Already occupied.");
        }
    }    if (auto* breaker = ecs.tryGetComponent<PowerBreaker>(target)) {
        bool activated = toggleBreaker(*breaker);
        if (auto* renderable = ecs.tryGetComponent<Renderable>(target)) {
            renderable->baseColor = activated ? glm::vec4(0.15f, 0.75f, 0.15f, 1.0f) : glm::vec4(0.75f, 0.15f, 0.10f, 1.0f);
        }
        auto poweredLights = ecs.view<BreakerPoweredLight, Light>();
        for (auto lightEntity : poweredLights) {
            auto& poweredLight = poweredLights.get<BreakerPoweredLight>(lightEntity);
            auto& light = poweredLights.get<Light>(lightEntity);
            light.intensity = activated ? poweredLight.litIntensity : 0.0f;
        }
        showFloatingText(activated ? "Breaker activated." : "Breaker deactivated.");
    }}

void DespairGameMode::afterPhysics(GameModeContext& ctx, float dt) {
    ECS& ecs = ctx.ecs;
    Physics& physics = ctx.physics;
    CharacterController& controller = ctx.controller;
    Camera& camera = ctx.camera;
    UIRenderer& ui = ctx.ui;
    Renderer& renderer = ctx.renderer;
    auto& input = ctx.input;
    updateFirstPersonPlayer(ecs, physics, controller.entity(), camera,
                                      controller.settings(), input.isActionDown("Crouch"), dt);

    if (ecs.tryGetComponent<FPSPlayerSettings>(controller.entity()) != nullptr) {
        ui.beginFrame(VkExtent2D{ctx.windowWidth, ctx.windowHeight});
        if (auto* stamina = ecs.tryGetComponent<StaminaState>(controller.entity())) {
            float staminaFrac = stamina->max > 0.0f ? glm::clamp(stamina->current / stamina->max, 0.0f, 1.0f) : 0.0f;
            constexpr glm::vec2 kStaminaBarSize(220.0f, 14.0f);
            constexpr glm::vec2 kStaminaBarPos(24.0f, 24.0f);
            ui.drawRect(kStaminaBarPos, kStaminaBarSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));
            glm::vec4 staminaColor = stamina->exhausted ? glm::vec4(0.55f, 0.15f, 0.15f, 0.85f)
                                                         : glm::vec4(0.75f, 0.65f, 0.2f, 0.85f);
            ui.drawRect(kStaminaBarPos, glm::vec2(kStaminaBarSize.x * staminaFrac, kStaminaBarSize.y), staminaColor);
        }

        {
            auto* objective = ecs.tryGetComponent<ObjectiveManager>(controller.entity());
            int collected = objective != nullptr ? objective->keycardsCollected : 0;
            std::string keycardText =
                "Keycards Found: " + std::to_string(collected) + "/" + std::to_string(kKeycardsRequiredForExit);
            ui.drawText(keycardText, glm::vec2(24.0f, 48.0f), 0.7f, glm::vec4(1.0f, 1.0f, 1.0f, 0.9f));
            ui.drawText("Objective: Collect keycards and reach the exit", glm::vec2(24.0f, 70.0f), 0.6f,
                                 glm::vec4(0.85f, 0.85f, 0.85f, 0.85f));
        }

        EntityId hotbarCharacter = controller.entity();
        if (auto* hotbar = ecs.tryGetComponent<HotbarInventory>(hotbarCharacter)) {
            static const char* kHotbarKeyNames[kHotbarSlotCount] = {"HotbarUseSlot1", "HotbarUseSlot2",
                                                                              "HotbarUseSlot3", "HotbarUseSlot4"};
            for (int i = 0; i < kHotbarSlotCount; ++i) {
                bool keyDown = input.isActionDown(kHotbarKeyNames[i]);
                if (keyDown && !hotbarKeyWasDown_[i]) {
                    std::string used = useHotbarSlot(ecs, *hotbar, i, hotbarCharacter, sanity_);
                    if (!used.empty()) showFloatingText(used);
                }
                hotbarKeyWasDown_[i] = keyDown;
            }

            constexpr glm::vec2 kHotbarSlotSize(56.0f, 56.0f);
            constexpr float kHotbarSlotGap = 8.0f;
            float hotbarWidth = kHotbarSlotCount * kHotbarSlotSize.x + (kHotbarSlotCount - 1) * kHotbarSlotGap;
            glm::vec2 hotbarOrigin((static_cast<float>(ctx.windowWidth) - hotbarWidth) * 0.5f,
                                   static_cast<float>(ctx.windowHeight) - kHotbarSlotSize.y - 24.0f);
            for (int i = 0; i < kHotbarSlotCount; ++i) {
                const HotbarSlot& slot = hotbar->slots[static_cast<std::size_t>(i)];
                glm::vec2 slotPos(hotbarOrigin.x + static_cast<float>(i) * (kHotbarSlotSize.x + kHotbarSlotGap), hotbarOrigin.y);
                ui.drawRect(slotPos, kHotbarSlotSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.55f));

                if (slot.kind == LootKind::None) continue;
                glm::vec4 iconColor = slot.kind == LootKind::SanityInjector
                                           ? glm::vec4(0.25f, 0.75f, 0.65f, 0.9f)
                                           : glm::vec4(0.8f, 0.7f, 0.2f, 0.9f);
                constexpr glm::vec2 kIconInset(8.0f, 8.0f);
                ui.drawRect(slotPos + kIconInset, kHotbarSlotSize - kIconInset * 2.0f, iconColor);

                std::string countText = std::to_string(slot.count);
                glm::vec2 countPos = slotPos + glm::vec2(kHotbarSlotSize.x - 14.0f, kHotbarSlotSize.y - 18.0f);
                ui.drawText(countText, countPos, 0.8f, glm::vec4(1.0f));
            }
        }

        if (!interactionPrompt_.empty()) {
            std::string promptText = "[E] " + interactionPrompt_;
            glm::vec2 size = ui.measureText(promptText, 0.7f);
            glm::vec2 pos((static_cast<float>(ctx.windowWidth) - size.x) * 0.5f,
                           static_cast<float>(ctx.windowHeight) * 0.5f + 40.0f);
            ui.drawRect(pos + glm::vec2(-12.0f, -8.0f), size + glm::vec2(24.0f, 16.0f),
                                  glm::vec4(0.0f, 0.0f, 0.0f, 0.6f));
            ui.drawText(promptText, pos, 0.7f, glm::vec4(1.0f, 1.0f, 0.85f, 1.0f));
        }

        if (floatingTextTimer_ > 0.0f) {
            floatingTextTimer_ -= dt;
            glm::vec2 size = ui.measureText(floatingText_, 0.75f);
            glm::vec2 pos((static_cast<float>(ctx.windowWidth) - size.x) * 0.5f,
                           static_cast<float>(ctx.windowHeight) * 0.5f - 80.0f);
            ui.drawText(floatingText_, pos, 0.75f, glm::vec4(1.0f, 0.9f, 0.6f, 1.0f));
        }
    }

    ai_.update(dt, ecs, physics);

    EntityId despairCharacter = controller.entity();
    if (auto* escapeState = ecs.tryGetComponent<EscapeGameState>(despairCharacter)) {
        elapsedSeconds_ += dt;

        bool toggleMenuDown = input.isActionDown("ToggleMenu");
        if (toggleMenuDown && !toggleMenuWasDown_) {
            restartRequested_ = true;
            SDL_Event quitEvent;
            quitEvent.type = SDL_QUIT;
            SDL_PushEvent(&quitEvent);
        }
        toggleMenuWasDown_ = toggleMenuDown;

        glm::vec3 despairPlayerPos(0.0f);
        if (auto* transform = ecs.tryGetComponent<Transform>(despairCharacter)) despairPlayerPos = transform->position;

        auto* sanity = ecs.tryGetComponent<SanityState>(despairCharacter);
        bool sanityDepleted = sanity != nullptr && sanity->current <= 0.0f;

        bool caughtByThreat = false;
        float nearestThreatDistance = -1.0f;
        auto tormentorView = ecs.view<Transform, TormentorAIState>();
        for (auto entity : tormentorView) {
            auto& tormentor = tormentorView.get<TormentorAIState>(entity);
            if (tormentor.behavior != TormentorBehaviorState::Hunting) continue;
            glm::vec3 threatPos = tormentorView.get<Transform>(entity).position;
            float distance = glm::distance(despairPlayerPos, threatPos);
            if (nearestThreatDistance < 0.0f || distance < nearestThreatDistance) nearestThreatDistance = distance;
            if (isPlayerCaught(despairPlayerPos, threatPos)) {
                caughtByThreat = true;
                break;
            }
        }
        if (!caughtByThreat) {
            auto cullerView = ecs.view<Transform, DespairCullerAIState>();
            for (auto entity : cullerView) {
                auto& culler = cullerView.get<DespairCullerAIState>(entity);
                if (culler.behavior != CullerBehaviorState::Hunting) continue;
                glm::vec3 threatPos = cullerView.get<Transform>(entity).position;
                float distance = glm::distance(despairPlayerPos, threatPos);
                if (nearestThreatDistance < 0.0f || distance < nearestThreatDistance) nearestThreatDistance = distance;
                if (isPlayerCaught(despairPlayerPos, threatPos)) {
                    caughtByThreat = true;
                    break;
                }
            }
        }
        constexpr float kDespairStaticGraceSeconds = 1.5f;
        constexpr float kDespairAmbientStaticScale = 0.2f;
        float staticIntensity;
        if (caughtByThreat) {
            staticIntensity = 1.0f;
        } else if (elapsedSeconds_ < kDespairStaticGraceSeconds) {
            staticIntensity = 0.0f;
        } else {
            staticIntensity =
                computeVhsStaticNoiseIntensity(nearestThreatDistance) * kDespairAmbientStaticScale;
        }
        renderer.setVhsStaticNoiseIntensity(staticIntensity);

        bool escapedThroughBlastDoor = false;
        auto blastDoorView = ecs.view<BlastDoorTag, LockedDoor>();
        for (auto entity : blastDoorView) {
            escapedThroughBlastDoor = !blastDoorView.get<LockedDoor>(entity).locked;
            break;
        }

        EscapeOutcome previousOutcome = escapeState->outcome;
        updateEscapeGameState(*escapeState, sanityDepleted, caughtByThreat, escapedThroughBlastDoor);
        if (escapeState->outcome != previousOutcome) {
            switch (escapeState->outcome) {
                case EscapeOutcome::Victory:
                    showFloatingText("You escaped the facility. VICTORY.");
                    break;
                case EscapeOutcome::CaughtByHunter:
                    showFloatingText("Something caught you. GAME OVER.");
                    break;
                case EscapeOutcome::LostToMadness:
                    showFloatingText("Your mind is gone. GAME OVER.");
                    break;
                case EscapeOutcome::InProgress:
                    break;
            }
        }

        if (escapeState->outcome != EscapeOutcome::InProgress) {
            glm::vec2 screenSize(static_cast<float>(ctx.windowWidth), static_cast<float>(ctx.windowHeight));
            ui.drawRect(glm::vec2(0.0f), screenSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.78f));

            std::string title;
            glm::vec4 titleColor(1.0f);
            switch (escapeState->outcome) {
                case EscapeOutcome::Victory:
                    title = "YOU ESCAPED";
                    titleColor = glm::vec4(0.75f, 0.9f, 0.75f, 1.0f);
                    break;
                case EscapeOutcome::CaughtByHunter:
                    title = "YOU DIED";
                    titleColor = glm::vec4(0.85f, 0.15f, 0.15f, 1.0f);
                    break;
                case EscapeOutcome::LostToMadness:
                    title = "YOUR MIND IS GONE";
                    titleColor = glm::vec4(0.7f, 0.2f, 0.75f, 1.0f);
                    break;
                case EscapeOutcome::InProgress:
                    break;
            }

            constexpr float kTitleScale = 3.0f;
            glm::vec2 titleSize = ui.measureText(title, kTitleScale);
            glm::vec2 titlePos((screenSize.x - titleSize.x) * 0.5f, screenSize.y * 0.4f);
            ui.drawText(title, titlePos, kTitleScale, titleColor);

            const std::string subtitle = "Press ESC to restart";
            constexpr float kSubtitleScale = 1.2f;
            glm::vec2 subtitleSize = ui.measureText(subtitle, kSubtitleScale);
            glm::vec2 subtitlePos((screenSize.x - subtitleSize.x) * 0.5f, titlePos.y + titleSize.y + 20.0f);
            ui.drawText(subtitle, subtitlePos, kSubtitleScale, glm::vec4(0.85f, 0.85f, 0.85f, 0.9f));
        }
    }}

} // namespace engine::despair
