#include "brokenbones/BrokenBonesGame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Application.hpp"
#include "core/Components.hpp"

namespace engine::brokenbones {

using core::HumanoidRagdollPart;

void BrokenBonesGame::handleInjuries(const std::vector<InjuryHit>& hits, glm::vec3 point, float impactSpeed) {
    core::Physics& physics = app_.physics();
    for (const InjuryHit& hit : hits) {
        const InjuryInfo& info = injuryInfo(hit.injury);
        ++progress_.stats.injuries;
        progress_.injuriesSeen |= InjuryTracker::bit(hit.injury);
        injuryText_ = std::string(info.name) + "!  " + info.effect;
        injuryTextSeconds_ = 2.6f;
        injuryFlash_ = 1.0f;
        shake_ = std::min(1.0f, shake_ + 0.4f);

        switch (hit.injury) {
            case Injury::Concussion: queueSound(0.1f, Sfx::Ringing, 0.6f); break;
            case Injury::KnockedOut:
                sounds_.play(Sfx::Heartbeat, 1.0f);
                triggerSlowMotion();
                break;
            case Injury::Whiplash: sounds_.play(Sfx::Crack, 0.8f, 1.35f); break;
            case Injury::DislocatedShoulder:
                sounds_.play(Sfx::Crack, 0.9f, 0.7f);
                physics.setRagdollJointLimp(ragdoll_, static_cast<int>(hit.part));
                break;
            case Injury::CompoundFracture:
                sounds_.play(Sfx::Squelch, 1.0f);
                effects_.burst(point, bloodBurst(std::max(impactSpeed, 25.0f)));
                break;
            case Injury::InternalBleeding:
            case Injury::PuncturedLung: sounds_.play(Sfx::Squelch, 0.7f, 0.8f); break;
            case Injury::SpinalInjury:
                sounds_.play(Sfx::SkullCrack, 1.0f, 0.6f);
                for (HumanoidRagdollPart leg : {HumanoidRagdollPart::UpperLegL, HumanoidRagdollPart::LowerLegL,
                                                HumanoidRagdollPart::UpperLegR, HumanoidRagdollPart::LowerLegR}) {
                    physics.setRagdollJointLimp(ragdoll_, static_cast<int>(leg));
                }
                triggerSlowMotion();
                break;
        }
        std::fprintf(stdout, "brokenbones: injury -- %s (%s) at %.1f m/s\n", info.name, boneName(hit.part), impactSpeed);
    }
}

void BrokenBonesGame::updateBleeding() {
    core::ECS& ecs = app_.ecs();
    const auto& bleeding = injuries_.bleeding();
    bool live = phase_ != Phase::Walking && !partWorld_.empty() && ragdoll_ != core::Physics::kInvalidRagdoll;
    for (size_t i = 0; i < bleedEntities_.size(); ++i) {
        auto* emitter = ecs.tryGetComponent<core::ParticleEmitter>(bleedEntities_[i]);
        if (emitter == nullptr) continue;
        emitter->settings.enabled = live && i < bleeding.size();
        if (!emitter->settings.enabled) continue;
        auto part = static_cast<size_t>(bleeding[i]);
        if (auto* transform = ecs.tryGetComponent<core::Transform>(bleedEntities_[i])) {
            transform->position = glm::vec3(partWorld_[part][3]);
        }
        glm::vec3 velocity = app_.physics().ragdollPartVelocity(ragdoll_, static_cast<int>(part));
        emitter->settings.velocityMin = velocity * 0.85f + glm::vec3(-0.8f, -0.5f, -0.8f);
        emitter->settings.velocityMax = velocity * 0.85f + glm::vec3(0.8f, 1.2f, 0.8f);
    }
}

void BrokenBonesGame::updateMusic(float dt) {
    bool fromTitle = menu_ == Menu::Title || (menu_ != Menu::None && menu_ != Menu::Pause &&
                                              menu_ != Menu::Rebirth && settingsReturn_ == Menu::Title);
    if (fromTitle) {
        music_.play(Track::Title);
    } else {
        music_.play(phase_ == Phase::Falling ? Track::Freefall : Track::Cliff);
    }
    float duck = menu_ != Menu::None && !fromTitle ? 0.4f : 1.0f;
    duck *= 1.0f - 0.8f * injuries_.blackout();
    music_.setDuck(duck);
    music_.setVolume(progress_.settings.music);
    music_.update(dt);
}

void BrokenBonesGame::drawInjuryOverlay(glm::vec2 screen) {
    core::UIRenderer& ui = app_.uiRenderer();
    auto edges = [&](glm::vec4 color, float thickness) {
        if (color.a <= 0.0f) return;
        ui.drawRect(glm::vec2(0.0f), glm::vec2(screen.x, thickness), color);
        ui.drawRect(glm::vec2(0.0f, screen.y - thickness), glm::vec2(screen.x, thickness), color);
        ui.drawRect(glm::vec2(0.0f), glm::vec2(thickness, screen.y), color);
        ui.drawRect(glm::vec2(screen.x - thickness, 0.0f), glm::vec2(thickness, screen.y), color);
    };
    if (phase_ == Phase::Walking) return;

    // Layered edge bands make a soft vignette with the flat-rect UI.
    float pulse = 0.5f + 0.5f * std::sin(overlayClock_ * 4.0f);
    float bleed = injuries_.bleeding().empty() ? 0.0f : 0.12f + 0.08f * pulse;
    float hurt = std::max(bleed, 0.35f * injuryFlash_);
    float daze = injuries_.daze() * (0.18f + 0.1f * std::sin(overlayClock_ * 2.2f));
    for (int band = 0; band < 4; ++band) {
        float thickness = 40.0f + 45.0f * static_cast<float>(band);
        edges(glm::vec4(0.55f, 0.0f, 0.0f, hurt * 0.4f), thickness);
        edges(glm::vec4(0.05f, 0.02f, 0.12f, daze * 0.45f), thickness);
    }

    float blackout = injuries_.blackout();
    if (blackout > 0.0f) {
        ui.drawRect(glm::vec2(0.0f), screen, glm::vec4(0.0f, 0.0f, 0.0f, std::min(0.97f, blackout)));
        if (blackout > 0.4f) {
            const char* text = "KNOCKED OUT";
            glm::vec2 size = ui.measureText(text, 1.4f);
            ui.drawText(text, glm::vec2((screen.x - size.x) * 0.5f, screen.y * 0.45f), 1.4f,
                        glm::vec4(0.7f, 0.7f, 0.75f, blackout));
        }
    }
}

} // namespace engine::brokenbones
