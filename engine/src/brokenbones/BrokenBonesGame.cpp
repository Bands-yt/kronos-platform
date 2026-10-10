#include "brokenbones/BrokenBonesGame.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <filesystem>

#include <SDL2/SDL.h>
#include <glm/gtc/matrix_transform.hpp>

#include "brokenbones/HudStyle.hpp"
#include "core/Application.hpp"
#include "core/Components.hpp"
#include "core/ParticleSystem.hpp"
#include "core/RiggedAvatar.hpp"
#include "runtime/GameLoop.hpp"

namespace engine::brokenbones {

namespace {

constexpr float kFallVelocityToRagdoll = -8.0f;
constexpr float kMinImpactSpeed = 3.0f;
constexpr float kRocketTriggerSpeed = 6.0f;
constexpr float kResultsSeconds = 6.5f;
constexpr float kBaseFov = 70.0f;
constexpr float kBombBlastSpeed = 16.0f;

using namespace hud;

constexpr const char* kBuyActions[] = {"BrokenBonesBuy1", "BrokenBonesBuy2", "BrokenBonesBuy3",
                                       "BrokenBonesBuy4", "BrokenBonesBuy5", "BrokenBonesBuy6",
                                       "BrokenBonesBuy7", "BrokenBonesBuy8", "BrokenBonesBuy9"};

const glm::vec3 kBalloonOffsets[3] = {{-0.55f, 2.3f, 0.05f}, {0.1f, 2.75f, -0.15f}, {0.6f, 2.35f, 0.1f}};
const glm::vec4 kBalloonColors[3] = {{0.95f, 0.15f, 0.2f, 1.0f}, {1.0f, 0.85f, 0.1f, 1.0f}, {0.15f, 0.55f, 1.0f, 1.0f}};

void setVisible(core::ECS& ecs, core::EntityId entity, bool visible) {
    if (auto* renderable = ecs.tryGetComponent<core::Renderable>(entity)) renderable->visible = visible;
}

} // namespace

BrokenBonesGame::BrokenBonesGame(core::Application& app, core::ProceduralMaterialLibrary materials, uint32_t firstSeed,
                                 std::string savePath)
    : app_(app), materials_(materials), seedRng_(firstSeed), savePath_(std::move(savePath)),
      contractRng_(firstSeed ^ 0x9e3779b9u) {
    std::string error;
    humanoid_ = core::buildHumanoidRagdoll(core::applyBodyProportionsToSkeleton(core::buildHumanoidSkeleton(), {}), error);
    if (!error.empty()) std::fprintf(stderr, "brokenbones: humanoid ragdoll unavailable: %s\n", error.c_str());

    int refund = 0;
    if (loadProgress(progress_, savePath_, error, &refund)) {
        std::fprintf(stdout, "brokenbones: loaded save -- $%d, level %d.\n", progress_.cash, progress_.level);
        if (refund > 0) {
            showToast("JETPACK RETIRED - REFUNDED $" + std::to_string(refund), kGreen, 4.0f);
            std::fprintf(stdout, "brokenbones: refunded $%d for the retired jetpack gear.\n", refund);
        }
    } else {
        std::fprintf(stdout, "brokenbones: starting fresh (%s).\n", error.c_str());
    }
    refillContracts(progress_, contractRng_);
    save();

    using platform_adapters::InputBinding;
    using platform_adapters::PhysicalInputKind;
    auto bind = [&](const char* action, SDL_Scancode key) {
        app_.input().bindAction(action, InputBinding{PhysicalInputKind::KeyboardKey, key});
    };
    bind("BrokenBonesFlop", SDL_SCANCODE_F);
    bind("BrokenBonesRestart", SDL_SCANCODE_R);
    bind("BrokenBonesShop", SDL_SCANCODE_B);
    bind("BrokenBonesBomb", SDL_SCANCODE_X);
    bind("BrokenBonesRocket", SDL_SCANCODE_Q);
    bind("BrokenBonesFloats", SDL_SCANCODE_LSHIFT);
    bind("BrokenBonesShopPrev", SDL_SCANCODE_LEFT);
    bind("BrokenBonesShopNext", SDL_SCANCODE_RIGHT);
    bind("BrokenBonesMap", SDL_SCANCODE_M);
    bind("BrokenBonesMenuUp", SDL_SCANCODE_UP);
    bind("BrokenBonesMenuDown", SDL_SCANCODE_DOWN);
    bind("BrokenBonesMenuLeft", SDL_SCANCODE_LEFT);
    bind("BrokenBonesMenuRight", SDL_SCANCODE_RIGHT);
    bind("BrokenBonesMenuSelect", SDL_SCANCODE_RETURN);
    bind("BrokenBonesMenuBack", SDL_SCANCODE_ESCAPE);
    const SDL_Scancode buyKeys[kBuyKeyCount] = {SDL_SCANCODE_1, SDL_SCANCODE_2, SDL_SCANCODE_3,
                                                SDL_SCANCODE_4, SDL_SCANCODE_5, SDL_SCANCODE_6,
                                                SDL_SCANCODE_7, SDL_SCANCODE_8, SDL_SCANCODE_9};
    for (size_t i = 0; i < kBuyKeyCount; ++i) bind(kBuyActions[i], buyKeys[i]);

    // The ragdoll spawns where the player capsule stands, so the two must not collide.
    app_.physics().setLayerCollision(core::CollisionLayer::Debris, core::CollisionLayer::Character, false);
    createGearVisuals();

    std::error_code ec;
    std::filesystem::path soundDir = std::filesystem::temp_directory_path(ec) / "kronos_brokenbones_sfx";
    if (ec || !sounds_.load(app_.audio(), soundDir.string())) {
        std::fprintf(stderr, "brokenbones: sound effects unavailable; playing silently.\n");
    }
    auto musicStart = std::chrono::steady_clock::now();
    if (ec || !music_.load(app_.audio(), soundDir.string())) {
        std::fprintf(stderr, "brokenbones: music unavailable.\n");
    } else {
        std::fprintf(stdout, "brokenbones: %zu music tracks synthesized in %.0f ms.\n", kTrackCount,
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - musicStart).count());
    }
    effects_.attach(app_.ecs());
    applySettings();
}

BrokenBonesGame::~BrokenBonesGame() {
    music_.stop();
    app_.physics().destroyRagdoll(ragdoll_);
    app_.physics().setImpactRecording(false);
}

bool BrokenBonesGame::start(int level) {
    level_ = level > 0 ? level : progress_.level;
    if (humanoid_.desc.parts.empty()) return false;
    if (!rebuildMap()) return false;
    respawnPlayer();
    // Dev capture hook: skip the title and flop off the top after N seconds.
    if (const char* autoplay = std::getenv("KRONOS_BROKENBONES_AUTOPLAY")) {
        autoFlopSeconds_ = std::max(0.5f, static_cast<float>(std::atof(autoplay)));
        closeMenu();
    } else {
        openMenu(Menu::Title);
    }
    return true;
}

void BrokenBonesGame::finish() {
    if (phase_ == Phase::Falling) {
        endRun(run_.distanceFallen() >= 0.5f * layout_.height ? RunEndReason::EndedEarly : RunEndReason::None);
    }
    sounds_.stopAllLoops();
    music_.stop();
}

void BrokenBonesGame::queueSound(float delaySeconds, Sfx sfx, float volume, float pitch) {
    soundQueue_.push_back({delaySeconds, sfx, volume, pitch});
}

void BrokenBonesGame::updateSounds(float dt) {
    thudCooldown_ = std::max(0.0f, thudCooldown_ - dt);
    for (auto it = soundQueue_.begin(); it != soundQueue_.end();) {
        it->delay -= dt;
        if (it->delay <= 0.0f) {
            sounds_.play(it->sfx, it->volume, it->pitch);
            it = soundQueue_.erase(it);
        } else {
            ++it;
        }
    }

    if (phase_ != Phase::Falling || ragdoll_ == core::Physics::kInvalidRagdoll) {
        sounds_.stopAllLoops();
        wasFloating_ = false;
        return;
    }
    float speed = glm::length(app_.physics().ragdollPartVelocity(ragdoll_, 0));
    float windVolume = std::clamp((speed - 4.0f) / 45.0f, 0.0f, 1.0f) * (boostState_.floatsLifting ? 0.35f : 0.85f);
    sounds_.setLoop(Sfx::Wind, windVolume, 0.75f + std::min(speed, 80.0f) / 100.0f);
    if (rocket_.burning()) {
        sounds_.setLoop(Sfx::RocketBurn, 0.9f, rocketSuper_ ? 1.3f : 1.0f);
    } else {
        sounds_.stopLoop(Sfx::RocketBurn);
    }
    if (boostState_.floatsLifting && !wasFloating_) sounds_.play(Sfx::FloatInflate, 0.8f);
    wasFloating_ = boostState_.floatsLifting;
}

bool BrokenBonesGame::rebuildMap() {
    vkDeviceWaitIdle(app_.renderer().device());
    uint32_t seed = seedRng_();
    layout_ = generateCliffLayout(seed, level_, MapOptions{progress_.map, altitudeMultiplier(progress_)});
    GpuUpload gpu{&app_.meshLibrary(), app_.renderer().allocator(), app_.renderer().device(),
                  app_.renderer().commandPool(), app_.renderer().graphicsQueue()};
    if (!map_.build(layout_, app_.ecs(), app_.physics(), gpu, materials_)) return false;
    app_.camera().farPlane = std::max(500.0f, layout_.height * 2.0f + 150.0f);
    const MapThemeInfo& theme = mapThemeInfo(layout_.theme);
    core::Application::AtmosphereOverride atmosphere;
    atmosphere.fogColor = theme.fogColor;
    // Thinner fog on taller cliffs so the bottom stays in view from the top.
    atmosphere.fogDensity = 0.0012f * std::sqrt(std::min(1.0f, 300.0f / layout_.height));
    atmosphere.skyZenithColor = theme.skyZenith;
    atmosphere.skyHorizonColor = theme.skyHorizon;
    atmosphere.overrideSun = true;
    atmosphere.sunDirectionWS = -layout_.sunDirection;
    switch (layout_.theme) {
    case MapTheme::Coast:
        atmosphere.sunColor = {1.0f, 0.95f, 0.86f};
        atmosphere.sunIntensity = 4.2f;
        atmosphere.ambient = {0.20f, 0.23f, 0.29f};
        atmosphere.ambientGround = {0.13f, 0.12f, 0.10f};
        break;
    case MapTheme::Glacier:
        atmosphere.sunColor = {0.92f, 0.96f, 1.0f};
        atmosphere.sunIntensity = 3.8f;
        atmosphere.ambient = {0.22f, 0.26f, 0.33f};
        atmosphere.ambientGround = {0.16f, 0.18f, 0.21f};
        break;
    case MapTheme::Canyon:
        atmosphere.sunColor = {1.0f, 0.88f, 0.70f};
        atmosphere.sunIntensity = 4.4f;
        atmosphere.ambient = {0.22f, 0.20f, 0.20f};
        atmosphere.ambientGround = {0.17f, 0.12f, 0.08f};
        break;
    case MapTheme::Volcano:
        atmosphere.sunColor = {1.0f, 0.72f, 0.50f};
        atmosphere.sunIntensity = 3.6f;
        atmosphere.ambient = {0.22f, 0.15f, 0.13f};
        atmosphere.ambientGround = {0.20f, 0.10f, 0.06f};
        break;
    }
    app_.setAtmosphereOverride(atmosphere);
    std::fprintf(stdout,
                 "brokenbones: level %d %s cliff built -- seed %u, %s, %.0f m tall, %zu ledges, %zu boulders, %zu beams, "
                 "%zu entities.\n",
                 level_, theme.name, seed, layout_.rockName, layout_.height, layout_.ledges.size(), layout_.boulders.size(),
                 layout_.beams.size(), map_.entityCount());
    return true;
}

void BrokenBonesGame::respawnPlayer() {
    core::ECS& ecs = app_.ecs();
    core::Physics& physics = app_.physics();
    core::EntityId character = app_.characterController().entity();
    physics.setPosition(character, ecs, layout_.spawnPoint);
    physics.setHorizontalVelocity(character, ecs, glm::vec2(0.0f));
    physics.setVerticalVelocity(character, ecs, 0.0f);
    app_.characterController().setInitialCameraAngles(layout_.spawnYawDegrees, -12.0f);
    app_.setMovementInputSuspended(menu_ != Menu::None);
    app_.camera().verticalFovDegrees = kBaseFov;
    walkingSeconds_ = 0.0f;
    boostState_ = BoostState{};
    rocket_.reset();
    rocketTiming_.cancel();
    injuries_.begin();
    updateBleeding();
    phase_ = Phase::Walking;
}

bool BrokenBonesGame::pressed(const char* action, bool& wasDown) {
    bool down = app_.input().isActionDown(action);
    bool result = down && !wasDown;
    wasDown = down;
    return result;
}

void BrokenBonesGame::showToast(std::string text, glm::vec4 color, float seconds) {
    toast_ = std::move(text);
    toastColor_ = color;
    toastSeconds_ = seconds;
}

void BrokenBonesGame::save() {
    std::string error;
    if (!saveProgress(progress_, savePath_, error)) std::fprintf(stderr, "brokenbones: save failed: %s\n", error.c_str());
}

void BrokenBonesGame::tick(float dt) {
    auto tickStart = std::chrono::steady_clock::now();
    Phase phaseBefore = phase_;
    bool backPressed = pressed("BrokenBonesMenuBack", menuBackWasDown_);
    if (menu_ == Menu::None && backPressed) {
        if (shopOpen_) {
            shopOpen_ = false;
            notifyTutorial(TutorialEvent::ClosedShop);
        } else {
            openMenu(Menu::Pause);
        }
        backPressed = false;
    }
    if (menu_ != Menu::None) {
        if (phase_ == Phase::Walking) {
            app_.tickLocalAvatarIdle(dt);
            const bool fromTitle = menu_ == Menu::Title || (menu_ != Menu::Pause && menu_ != Menu::Rebirth &&
                                                             settingsReturn_ == Menu::Title);
            const auto* body = app_.ecs().tryGetComponent<core::Transform>(app_.characterController().entity());
            if (fromTitle && body != nullptr) {
                // Look at the avatar from the front.
                glm::vec3 facing = body->rotation * glm::vec3(0.0f, 0.0f, 1.0f);
                facing.y = 0.0f;
                facing = glm::length(facing) > 0.01f ? glm::normalize(facing) : glm::vec3(0.0f, 0.0f, 1.0f);
                const glm::vec3 focus = body->position + glm::vec3(0.0f, 0.35f, 0.0f);
                core::Camera& camera = app_.camera();
                camera.position = focus + facing * 5.5f + glm::vec3(0.0f, 0.7f, 0.0f);
                const glm::vec3 look = glm::normalize(focus - camera.position);
                camera.yawDegrees = glm::degrees(std::atan2(look.z, look.x));
                camera.pitchDegrees = glm::degrees(std::asin(look.y));
            }
        }
        tickMenu(backPressed);
        updateSounds(dt);
        updateMusic(dt);
        drawHud();
        drawMenu();
        return;
    }

    progress_.stats.playSeconds += dt;
    slowMoCooldown_ = std::max(0.0f, slowMoCooldown_ - dt);
    float timeScale = 1.0f;
    if (slowMoSeconds_ > 0.0f) {
        slowMoSeconds_ = std::max(0.0f, slowMoSeconds_ - dt);
        // Hold at quarter speed, then ease back to full speed over the last 0.35 s.
        float ease = 1.0f - std::clamp(slowMoSeconds_ / 0.35f, 0.0f, 1.0f);
        timeScale = 0.25f + 0.75f * ease * ease;
    }
    app_.gameLoop()->setTimeScale(timeScale);
    float gameDt = dt * timeScale;

    bool restartPressed = pressed("BrokenBonesRestart", restartWasDown_);
    bombCooldown_ = std::max(0.0f, bombCooldown_ - gameDt);

    switch (phase_) {
        case Phase::Walking:
            if (restartPressed) {
                rebuildMap();
                respawnPlayer();
                break;
            }
            tickWalking(dt);
            break;
        case Phase::Falling:
            if (restartPressed) {
                bool realFall = run_.distanceFallen() >= 0.5f * layout_.height;
                endRun(realFall ? RunEndReason::EndedEarly : RunEndReason::None);
                if (!realFall) resultsSeconds_ = kResultsSeconds;
                break;
            }
            tickFalling(gameDt);
            break;
        case Phase::Results:
            if (restartPressed) resultsSeconds_ = kResultsSeconds;
            tickResults(dt);
            break;
    }
    updateGearVisuals(dt);
    updateBleeding();
    effects_.update(gameDt);
    injuries_.update(gameDt, phase_ == Phase::Falling);
    updateSounds(dt);
    updateMusic(dt);
    toastSeconds_ = std::max(0.0f, toastSeconds_ - dt);
    injuryTextSeconds_ = std::max(0.0f, injuryTextSeconds_ - dt);
    injuryFlash_ = std::max(0.0f, injuryFlash_ - dt * 2.0f);
    dustCooldown_ = std::max(0.0f, dustCooldown_ - gameDt);
    overlayClock_ += dt;
    bannerSeconds_ = std::max(0.0f, bannerSeconds_ - dt);
    if (bannerSeconds_ <= 0.0f && !bannerQueue_.empty()) {
        bannerQueue_.erase(bannerQueue_.begin());
        if (!bannerQueue_.empty()) {
            bannerSeconds_ = 3.0f;
            sounds_.play(Sfx::Fanfare, 0.7f, 1.15f);
        }
    }
    drawHud();
    drawOverlays();

    static constexpr const char* kPhaseNames[] = {"walking", "falling", "results"};
    double logicMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - tickStart).count();
    if (dt > 0.1f || logicMs > 30.0) {
        std::fprintf(stdout, "brokenbones: slow frame %.0f ms (%s -> %s, game logic %.1f ms)\n", dt * 1000.0f,
                     kPhaseNames[static_cast<int>(phaseBefore)], kPhaseNames[static_cast<int>(phase_)], logicMs);
        std::fflush(stdout);
    }
}

void BrokenBonesGame::tickWalking(float dt) {
    if (pressed("BrokenBonesShop", shopWasDown_)) {
        shopOpen_ = !shopOpen_;
        sounds_.play(Sfx::Click, 0.7f, shopOpen_ ? 1.0f : 0.8f);
        notifyTutorial(shopOpen_ ? TutorialEvent::OpenedShop : TutorialEvent::ClosedShop);
    }
    if (shopOpen_) tickShop();
    if (pressed("BrokenBonesMap", mapWasDown_)) {
        MapTheme next = nextOwnedMap(progress_);
        if (next == progress_.map) {
            showToast("BUY MORE MAPS IN THE SHOP [B]", kDim, 1.5f);
            sounds_.play(Sfx::Denied, 0.6f);
        } else {
            sounds_.play(Sfx::Click, 0.8f);
            switchMap(next);
        }
        return;
    }

    // The capsule's Transform only syncs from Jolt on the next physics step.
    walkingSeconds_ += dt;
    bool flopPressed = pressed("BrokenBonesFlop", flopWasDown_);
    bool bombPressed = pressed("BrokenBonesBomb", bombWasDown_);
    bool rocketPressed = pressed("BrokenBonesRocket", rocketWasDown_);
    if (walkingSeconds_ < 0.3f) return;
    if (autoFlopSeconds_ > 0.0f && walkingSeconds_ >= autoFlopSeconds_) {
        const AxisBox& board = layout_.divingBoard;
        app_.physics().setPosition(app_.characterController().entity(), app_.ecs(),
                                   glm::vec3(board.center.x, layout_.height + 1.2f, board.center.z + board.halfExtents.z + 2.0f));
        autoFlopSeconds_ = 0.0f;
        return;
    }

    core::EntityId character = app_.characterController().entity();
    auto* transform = app_.ecs().tryGetComponent<core::Transform>(character);
    if (transform == nullptr) return;
    glm::vec3 velocity = app_.physics().getLinearVelocity(character, app_.ecs());

    if (bombPressed) {
        if (!progress_.has(ShopItem::Bomb)) {
            showToast("NO BOMBS - BUY SOME IN THE SHOP [B]", kDim, 1.5f);
            sounds_.play(Sfx::Denied, 0.6f);
        } else if (startFalling()) {
            detonateBomb();
        }
        return;
    }
    if (rocketPressed) {
        if (!progress_.has(ShopItem::Rocket)) {
            showToast("NO ROCKETS - BUY SOME IN THE SHOP [B]", kDim, 1.5f);
            sounds_.play(Sfx::Denied, 0.6f);
        } else if (startFalling()) {
            glm::vec3 forward = app_.camera().forward();
            glm::vec3 flat = glm::length(glm::vec2(forward.x, forward.z)) > 1e-3f
                                 ? glm::normalize(glm::vec3(forward.x, 0.0f, forward.z))
                                 : glm::vec3(0.0f, 0.0f, 1.0f);
            app_.physics().addRagdollVelocity(ragdoll_, flat * 22.0f + glm::vec3(0.0f, 10.0f, 0.0f));
            fireRocket(0.9f);
        }
        return;
    }
    if (flopPressed) {
        if (startFalling() && progress_.has(ShopItem::SpringShoes)) {
            app_.physics().addRagdollVelocity(ragdoll_, springLeapVelocity(progress_, app_.camera().forward()));
            showToast("BOING!", kGold, 1.0f);
            sounds_.play(Sfx::Boing, 0.9f);
        }
        return;
    }
    if (velocity.y < kFallVelocityToRagdoll || transform->position.y < layout_.height - 4.0f) startFalling();
}

void BrokenBonesGame::tickShop() {
    bool prev = pressed("BrokenBonesShopPrev", pagePrevWasDown_);
    bool next = pressed("BrokenBonesShopNext", pageNextWasDown_);
    if (prev || next) {
        shopPage_ = (shopPage_ + (next ? 1 : kShopPageCount - 1)) % kShopPageCount;
        sounds_.play(Sfx::Click, 0.6f, 1.1f);
    }
    const ShopPage& page = shopPage(shopPage_);
    for (size_t i = 0; i < kBuyKeyCount; ++i) {
        if (!pressed(kBuyActions[i], buyWasDown_[i]) || i >= page.count) continue;
        ShopItem item = page.items[i];
        int price = shopPrice(progress_, item);
        BuyResult result = buyItem(progress_, item);
        if (result == BuyResult::Bought) {
            save();
            showToast(std::string("BOUGHT ") + shopItemInfo(item).name + "  -$" + std::to_string(price), kGreen, 1.6f);
            sounds_.play(Sfx::Coin, 0.8f);
            std::fprintf(stdout, "brokenbones: bought %s for $%d, $%d left.\n", shopItemInfo(item).name, price,
                         progress_.cash);
            notifyTutorial(TutorialEvent::Bought);
            grantAchievements(nullptr);
            if (item == ShopItem::GlacierMap) switchMap(MapTheme::Glacier);
            if (item == ShopItem::CanyonMap) switchMap(MapTheme::Canyon);
            if (item == ShopItem::VolcanoMap) switchMap(MapTheme::Volcano);
            if (item == ShopItem::Altitude) switchMap(progress_.map);
            if (item == ShopItem::MoreBones) {
                showToast("SKELETON GREW TO " + std::to_string(skeletonBoneCount(progress_)) + " BONES", kGreen, 2.0f);
                refillContracts(progress_, contractRng_);
            }
        } else {
            showToast(buyResultText(result, item), kRed, 1.4f);
            sounds_.play(Sfx::Denied, 0.6f);
        }
    }
}

void BrokenBonesGame::switchMap(MapTheme theme) {
    progress_.map = theme;
    save();
    rebuildMap();
    respawnPlayer();
    showToast(std::string(mapThemeInfo(theme).name) + " - " + std::to_string(static_cast<int>(layout_.height)) + " m",
              kGold, 2.0f);
}

float BrokenBonesGame::payoutMultiplier() const {
    return cashBonusMultiplier(progress_) * mapThemeInfo(layout_.theme).cashMultiplier * rebirthMultiplier(progress_) *
           goldenBonesMultiplier(progress_);
}

bool BrokenBonesGame::startFalling() {
    core::ECS& ecs = app_.ecs();
    core::Physics& physics = app_.physics();
    core::EntityId character = app_.characterController().entity();
    glm::vec3 velocity = physics.getLinearVelocity(character, ecs);

    glm::mat4 meshWorld(1.0f);
    std::vector<glm::mat4> initialParts;
    const std::vector<glm::mat4>* initialPose = nullptr;
    const std::vector<core::EntityId>& skinned = app_.localPlayerSkinnedEntities();
    if (!skinned.empty()) {
        auto* transform = ecs.tryGetComponent<core::Transform>(skinned.front());
        auto* skin = ecs.tryGetComponent<core::SkinnedRenderable>(skinned.front());
        if (transform != nullptr) meshWorld = transform->matrix();
        if (skin != nullptr && skin->skinningMatrices.size() == humanoid_.jointOwner.size()) {
            initialParts = core::ragdollPartsFromSkinning(humanoid_, skin->skinningMatrices, meshWorld);
            initialPose = &initialParts;
        }
    } else if (auto* transform = ecs.tryGetComponent<core::Transform>(character)) {
        meshWorld = glm::translate(glm::mat4(1.0f), transform->position - glm::vec3(0.0f, 0.9f, 0.0f));
    }

    ragdoll_ = physics.createRagdoll(humanoid_.desc, meshWorld, initialPose, velocity, core::CollisionLayer::Debris);
    if (ragdoll_ == core::Physics::kInvalidRagdoll) {
        std::fprintf(stderr, "brokenbones: ragdoll creation failed; staying in walking mode.\n");
        return false;
    }

    // A little random tumble so no two falls land the same way.
    std::uniform_real_distribution<float> tumble(-1.0f, 1.0f);
    glm::vec3 twist(tumble(shakeRng_), tumble(shakeRng_) * 0.3f, tumble(shakeRng_));
    physics.addRagdollImpulse(ragdoll_, static_cast<int>(core::HumanoidRagdollPart::Head), twist * 4.0f);
    physics.addRagdollImpulse(ragdoll_, static_cast<int>(core::HumanoidRagdollPart::LowerLegL), -twist * 2.0f);
    physics.addRagdollImpulse(ragdoll_, static_cast<int>(core::HumanoidRagdollPart::LowerLegR), -twist * 2.0f);

    // Park the capsule back at spawn; it is frozen while input is suspended.
    app_.setMovementInputSuspended(true);
    physics.setPosition(character, ecs, layout_.spawnPoint);
    physics.setHorizontalVelocity(character, ecs, glm::vec2(0.0f));
    physics.setVerticalVelocity(character, ecs, 0.0f);

    physics.setImpactRecording(true, kMinImpactSpeed);
    (void)physics.drainImpactEvents();

    applyRagdollPose();
    glm::vec3 pelvis = partWorld_.empty() ? glm::vec3(meshWorld[3]) : glm::vec3(partWorld_[0][3]);
    run_.begin(pelvis.y, breakSpeedMultiplier(progress_), skeletonBoneCount(progress_));
    boosts_.beginRun(progress_);
    rocket_.reset();
    rocketTiming_.cancel();
    rocketSuper_ = false;
    shopOpen_ = false;
    runBombs_ = runRockets_ = runSuperBoosts_ = runMisfires_ = 0;
    injuries_.begin();
    boardPlacements_.clear();
    notifyTutorial(TutorialEvent::StartedFalling);
    cameraFocus_ = pelvis;
    cameraYaw_ = app_.camera().yawDegrees;
    cameraPitch_ = std::min(app_.camera().pitchDegrees, -10.0f);
    endReason_ = RunEndReason::None;
    phase_ = Phase::Falling;
    return true;
}

void BrokenBonesGame::announceBreaks(const std::vector<BoneBreak>& breaks, const char* prefix) {
    if (breaks.empty()) {
        showToast(prefix, kOrange, 1.8f);
        return;
    }
    char text[96];
    int snapped = 0;
    for (const BoneBreak& bone : breaks) snapped += bone.count;
    if (snapped == 1) {
        std::snprintf(text, sizeof(text), "%s %s SNAPPED!", prefix, boneName(breaks.front().part));
    } else {
        std::snprintf(text, sizeof(text), "%s %d BONES SNAPPED!", prefix, snapped);
    }
    showToast(text, kOrange, 2.0f);
    for (size_t i = 0; i < breaks.size(); ++i) {
        const BoneBreak& bone = breaks[i];
        queueSound(0.05f + 0.07f * static_cast<float>(i),
                   bone.part == core::HumanoidRagdollPart::Head ? Sfx::SkullCrack : Sfx::Crack, 0.9f,
                   0.85f + 0.06f * static_cast<float>(i % 5));
    }
    for (const BoneBreak& bone : breaks) {
        std::fprintf(stdout, "brokenbones: %s broke at %.1f m/s (combo x%d)\n", boneName(bone.part), bone.impactSpeed,
                     bone.combo);
    }
}

void BrokenBonesGame::detonateBomb() {
    if (!progress_.has(ShopItem::Bomb) || bombCooldown_ > 0.0f || partWorld_.empty()) return;
    --progress_.owned[static_cast<size_t>(ShopItem::Bomb)];
    ++runBombs_;
    ++progress_.stats.bombs;
    save();
    bombCooldown_ = 1.0f;

    float power = blastPowerMultiplier(progress_);
    glm::vec3 pelvis(partWorld_[0][3]);
    glm::vec3 launch = bombLaunchVelocity(app_.camera().forward()) * (1.0f + 0.5f * (power - 1.0f));
    auto breaks = applyBlast(app_.physics(), ragdoll_, run_, pelvis, kBombBlastSpeed * power, launch, shakeRng_);

    if (auto* transform = app_.ecs().tryGetComponent<core::Transform>(bombBurstEntity_)) transform->position = pelvis;
    if (auto* emitter = app_.ecs().tryGetComponent<core::ParticleEmitter>(bombBurstEntity_)) emitter->settings.enabled = true;
    effects_.burst(pelvis, explosionSmoke(power));
    shake_ = 1.0f;
    sounds_.play(Sfx::Bomb, 1.0f);
    announceBreaks(breaks, "BOOM!");
    std::fprintf(stdout, "brokenbones: bomb detonated, %d left.\n", progress_.count(ShopItem::Bomb));
}

void BrokenBonesGame::fireRocket(float delaySeconds) {
    if (!progress_.has(ShopItem::Rocket) || rocket_.live() || rocketTiming_.active()) return;
    --progress_.owned[static_cast<size_t>(ShopItem::Rocket)];
    ++runRockets_;
    ++progress_.stats.rockets;
    save();
    rocketTiming_.start(shakeRng_, level_);
    rocketGuardSeconds_ = delaySeconds;
    sounds_.play(Sfx::Click, 0.8f, 1.3f);
    showToast("[Q] IN THE GREEN = SUPER BOOST", kGreen, 1.5f);
    std::fprintf(stdout, "brokenbones: rocket primed, %d left.\n", progress_.count(ShopItem::Rocket));
}

void BrokenBonesGame::igniteRocket(bool super) {
    rocketSuper_ = super;
    float delay = std::max(0.0f, rocketGuardSeconds_);
    rocket_.ignite(delay, rocketTuning(progress_, super));
    queueSound(delay, Sfx::RocketIgnite, super ? 1.0f : 0.9f, super ? 1.25f : 1.0f);
    if (super) {
        ++runSuperBoosts_;
        ++progress_.stats.superBoosts;
        sounds_.play(Sfx::Contract, 0.9f);
        showToast("SUPER BOOST!", kGold, 1.8f);
        shake_ = std::max(shake_, 0.5f);
    } else {
        showToast("ROCKET LIT - NOSEDIVE!", kOrange, 1.5f);
    }
    std::fprintf(stdout, "brokenbones: rocket lit%s (thrust %.0f, burn %.1f s).\n", super ? " -- SUPER BOOST" : "",
                 rocket_.tuning().thrust, rocket_.tuning().burnSeconds);
}

void BrokenBonesGame::explodeRocket(glm::vec3 centre, bool midAir) {
    rocket_.reset();
    float power = blastPowerMultiplier(progress_) * (rocketSuper_ ? 1.5f : 1.0f);
    std::uniform_real_distribution<float> side(-1.0f, 1.0f);
    // In mid-air the blast keeps the fall's momentum; only a real impact is allowed to stop the body.
    float up = midAir ? 5.0f : RocketController::kBlastLaunch * power;
    float sideways = midAir ? 9.0f : 6.0f;
    glm::vec3 launch(side(shakeRng_) * sideways, up, side(shakeRng_) * sideways);
    auto breaks = applyBlast(app_.physics(), ragdoll_, run_, centre, RocketController::kBlastSpeed * power, launch,
                             shakeRng_, midAir ? 0.0f : RocketController::kBlastAbsorb);
    if (auto* transform = app_.ecs().tryGetComponent<core::Transform>(bombBurstEntity_)) transform->position = centre;
    if (auto* emitter = app_.ecs().tryGetComponent<core::ParticleEmitter>(bombBurstEntity_)) emitter->settings.enabled = true;
    effects_.burst(centre, explosionSmoke(power));
    shake_ = 1.0f;
    sounds_.stopLoop(Sfx::RocketBurn);
    sounds_.play(Sfx::RocketBlast, 1.0f);
    triggerSlowMotion();
    announceBreaks(breaks, "KABOOM!");
    std::fprintf(stdout, "brokenbones: rocket exploded %s at (%.1f, %.1f, %.1f), %zu bones.\n",
                 midAir ? "in mid-air" : "on impact", centre.x, centre.y, centre.z, breaks.size());
}

void BrokenBonesGame::applyRagdollPose() {
    if (!app_.physics().getRagdollPartTransforms(ragdoll_, partWorld_) || partWorld_.empty()) return;
    // The mesh transform follows the pelvis so culling bounds stay near the body.
    glm::mat4 meshWorld = glm::translate(glm::mat4(1.0f), glm::vec3(partWorld_[0][3]));
    core::computeRagdollSkinningMatrices(humanoid_, partWorld_, meshWorld, skinning_);
    for (core::EntityId entity : app_.localPlayerSkinnedEntities()) {
        if (auto* transform = app_.ecs().tryGetComponent<core::Transform>(entity)) {
            transform->position = glm::vec3(meshWorld[3]);
            transform->rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            transform->scale = glm::vec3(1.0f);
        }
        if (auto* skin = app_.ecs().tryGetComponent<core::SkinnedRenderable>(entity)) skin->skinningMatrices = skinning_;
    }
}

void BrokenBonesGame::tickFalling(float dt) {
    core::Physics& physics = app_.physics();
    applyRagdollPose();
    if (partWorld_.empty()) return;

    bool rocketHit = false;
    glm::vec3 rocketHitPoint(0.0f);
    for (const core::Physics::ImpactEvent& impact : physics.drainImpactEvents()) {
        int a = physics.ragdollPartIndexForBody(ragdoll_, impact.bodyA);
        int b = physics.ragdollPartIndexForBody(ragdoll_, impact.bodyB);
        if ((a >= 0) == (b >= 0)) continue; // self-contact, or not ours
        int part = a >= 0 ? a : b;
        bool offTheTop = partWorld_[0][3].y < layout_.height - 3.0f;
        if (!rocketHit && rocket_.armed() && offTheTop && impact.impactSpeed >= kRocketTriggerSpeed) {
            rocketHit = true;
            rocketHitPoint = impact.point;
        }
        auto hitPart = static_cast<core::HumanoidRagdollPart>(part);
        auto broken = run_.registerImpact(hitPart, impact.impactSpeed);
        float breakSpeed = boneBreakSpeed(hitPart) * breakSpeedMultiplier(progress_);
        handleInjuries(injuries_.registerImpact(hitPart, impact.impactSpeed, breakSpeed, broken.has_value()),
                       impact.point, impact.impactSpeed);
        if (impact.impactSpeed >= 6.0f && (dustCooldown_ <= 0.0f || impact.impactSpeed >= 25.0f)) {
            effects_.burst(impact.point, impactDust(layout_.rockTint, impact.impactSpeed));
            dustCooldown_ = 0.08f;
        }
        if (!broken) {
            if (impact.impactSpeed >= RunTracker::kCountedHitSpeed && thudCooldown_ <= 0.0f) {
                std::uniform_real_distribution<float> pitch(0.85f, 1.15f);
                sounds_.play(Sfx::Thud, std::clamp(impact.impactSpeed / 25.0f, 0.2f, 1.0f), pitch(shakeRng_));
                thudCooldown_ = 0.06f;
                std::fprintf(stdout, "brokenbones: hit at (%.1f, %.1f, %.1f) %.1f m/s\n", impact.point.x,
                             impact.point.y, impact.point.z, impact.impactSpeed);
            }
            continue;
        }
        physics.setRagdollJointLimp(ragdoll_, part);
        effects_.burst(impact.point, boneChips(broken->count));
        {
            std::uniform_real_distribution<float> pitch(0.9f, 1.15f);
            bool skull = broken->part == core::HumanoidRagdollPart::Head;
            sounds_.play(skull ? Sfx::SkullCrack : Sfx::Crack, std::clamp(0.6f + broken->impactSpeed / 50.0f, 0.6f, 1.2f),
                         pitch(shakeRng_));
            if (broken->combo > 1) {
                queueSound(0.08f, Sfx::ComboChime, 0.6f, 1.0f + 0.12f * static_cast<float>(std::min(broken->combo, 8) - 2));
            }
        }
        std::string text = std::string(boneName(broken->part)) +
                           (broken->count > 1 ? " x" + std::to_string(broken->count) : std::string()) + " SNAPPED!";
        if (broken->combo > 1) text += "  COMBO x" + std::to_string(broken->combo);
        showToast(text, broken->combo > 1 ? kGold : kRed, 2.0f);
        shake_ = std::min(1.0f, shake_ + broken->impactSpeed / 30.0f);
        if (broken->impactSpeed >= 40.0f) triggerSlowMotion();
        std::fprintf(stdout, "brokenbones: %s broke at %.1f m/s (combo x%d) at (%.1f, %.1f, %.1f)\n",
                     boneName(broken->part), broken->impactSpeed, broken->combo, impact.point.x, impact.point.y,
                     impact.point.z);
    }
    if (rocketHit) explodeRocket(rocketHitPoint, false);

    if (pressed("BrokenBonesBomb", bombWasDown_)) detonateBomb();
    bool rocketKey = pressed("BrokenBonesRocket", rocketWasDown_);
    if (rocketTiming_.active()) {
        rocketGuardSeconds_ -= dt;
        TimingResult timing = rocketKey ? rocketTiming_.lock() : rocketTiming_.update(dt);
        if (timing == TimingResult::Hit) {
            igniteRocket(true);
        } else if (timing == TimingResult::TimedOut) {
            igniteRocket(false);
        } else if (timing == TimingResult::Miss) {
            rocketSuper_ = false;
            ++runMisfires_;
            showToast("MISFIRE!", kRed, 1.5f);
            sounds_.play(Sfx::Denied, 0.8f);
            explodeRocket(glm::vec3(partWorld_[static_cast<size_t>(core::HumanoidRagdollPart::Chest)][3]), true);
        }
    } else if (rocketKey) {
        if (rocket_.live()) {
            showToast("ONE ROCKET AT A TIME", kDim, 1.0f);
        } else if (progress_.has(ShopItem::Rocket)) {
            fireRocket();
        }
    }
    if (rocket_.update(dt, app_.camera().forward(), physics, ragdoll_)) {
        explodeRocket(glm::vec3(partWorld_[static_cast<size_t>(core::HumanoidRagdollPart::Head)][3]), true);
    }
    physics.addRagdollVelocity(ragdoll_, airDragDeltaV(physics.ragdollPartVelocity(ragdoll_, 0), dt));
    {
        glm::vec3 head = physics.ragdollPartVelocity(ragdoll_, static_cast<int>(core::HumanoidRagdollPart::Head));
        glm::vec3 chest = physics.ragdollPartVelocity(ragdoll_, static_cast<int>(core::HumanoidRagdollPart::Chest));
        if (auto snap = injuries_.registerNeckSnap(glm::length(head - chest))) {
            handleInjuries({*snap}, glm::vec3(partWorld_[static_cast<size_t>(core::HumanoidRagdollPart::Head)][3]), 0.0f);
        }
    }

    BoostInput input;
    input.floats = app_.input().isActionDown("BrokenBonesFloats");
    input.aim = app_.camera().forward();
    bool hadHelium = !boosts_.helium().empty();
    boostState_ = boosts_.update(dt, input, physics, ragdoll_);
    if (hadHelium && boosts_.helium().empty()) {
        showToast("FLOATS POPPED", kOrange, 1.5f);
        sounds_.play(Sfx::FloatPop, 0.9f);
    }

    float maxSpeed = 0.0f;
    for (size_t p = 0; p < partWorld_.size(); ++p) {
        maxSpeed = std::max(maxSpeed, glm::length(physics.ragdollPartVelocity(ragdoll_, static_cast<int>(p))));
    }
    glm::vec3 pelvis(partWorld_[0][3]);
    float pelvisSpeed = glm::length(physics.ragdollPartVelocity(ragdoll_, 0));
    splashSpeed_ = pelvisSpeed;
    updateRagdollCamera(dt, pelvis, pelvisSpeed);

    RunEndReason reason = run_.update(dt, pelvis, maxSpeed, layout_);
    if (reason != RunEndReason::None) endRun(reason);
}

void BrokenBonesGame::endRun(RunEndReason reason) {
    endReason_ = reason;
    leveledUp_ = run_.bonesBroken() >= bonesNeededForLevel(level_);
    resultsSeconds_ = 0.0f;
    phase_ = Phase::Results;
    boostState_ = BoostState{};
    rocket_.reset();
    app_.physics().setImpactRecording(false);

    payout_ = computeRunPayout(run_, level_, reason, payoutMultiplier(), injuries_.cash());
    contractsCompleted_.clear();
    newBests_.clear();
    if (!payout_.abandoned) {
        for (auto it = progress_.contracts.begin(); it != progress_.contracts.end();) {
            if (contractMet(*it, run_, reason)) {
                contractsCompleted_.emplace_back(contractText(*it), it->reward);
                payout_.contracts += it->reward;
                it = progress_.contracts.erase(it);
            } else {
                ++it;
            }
        }
        payout_.total += payout_.contracts;
        newBests_ = updatePersonalBests(progress_.bests, run_, payout_.total);
        ++progress_.runs;
        RunRecord record;
        record.run = progress_.runs;
        record.cash = payout_.total;
        record.bones = run_.bonesBroken();
        record.fall = std::max(0.0f, run_.distanceFallen());
        record.speed = run_.topSpeed();
        record.injuries = injuries_.count();
        record.level = level_;
        record.map = layout_.theme;
        boardPlacements_ = submitRun(progress_.leaderboard, record);
    }
    progress_.cash += payout_.total;
    progress_.totalBones += run_.bonesBroken();
    if (leveledUp_) {
        progress_.level = level_ + 1;
        progress_.bestLevel = std::max(progress_.bestLevel, progress_.level);
    }
    progress_.stats.cashEarned += payout_.total;
    progress_.stats.metresFallen += std::max(0.0f, run_.distanceFallen());
    if (reason == RunEndReason::Splashdown) ++progress_.stats.splashdowns;
    refillContracts(progress_, contractRng_);
    RunSummary summary = summarizeRun(run_, reason, layout_.theme);
    summary.bombs = runBombs_;
    summary.rockets = runRockets_;
    summary.superBoosts = runSuperBoosts_;
    summary.misfires = runMisfires_;
    summary.injuries = injuries_.count();
    summary.knockedOut = injuries_.has(Injury::KnockedOut);
    grantAchievements(&summary);
    notifyTutorial(TutorialEvent::RunEnded);
    slowMoSeconds_ = 0.0f;
    app_.gameLoop()->setTimeScale(1.0f);
    save();

    sounds_.stopAllLoops();
    if (reason == RunEndReason::Splashdown) {
        sounds_.play(Sfx::Splash, 1.0f);
        if (!partWorld_.empty()) {
            glm::vec3 entry(partWorld_[0][3].x, layout_.waterY + 0.1f, partWorld_[0][3].z);
            bool lava = layout_.theme == MapTheme::Volcano;
            effects_.burst(entry, splashColumn(splashSpeed_, lava));
            effects_.burst(entry, splashRing(splashSpeed_, lava));
            effects_.burst(entry, splashMist(splashSpeed_, lava));
            if (lava) effects_.burst(entry, explosionSmoke(2.0f));
        }
    }
    float cue = 0.5f;
    if (payout_.total > 0) {
        queueSound(cue, Sfx::Coin, 0.8f);
        cue += 0.35f;
    }
    for (size_t i = 0; i < contractsCompleted_.size(); ++i, cue += 0.45f) queueSound(cue, Sfx::Contract, 0.8f);
    bool podium = std::any_of(boardPlacements_.begin(), boardPlacements_.end(),
                              [](const auto& placed) { return placed.second <= 3; });
    if (leveledUp_ || !newBests_.empty() || podium) queueSound(cue + 0.1f, Sfx::Fanfare, 0.8f);
    std::fprintf(stdout,
                 "brokenbones: run over (%s) -- %d/%d bones, best combo x%d, %d hits, fell %.1f m, top speed %.1f "
                 "m/s, hardest hit %.1f m/s, %d injuries, %zu contracts, +$%d (now $%d)%s\n",
                 reason == RunEndReason::None ? "abandoned" : runEndReasonText(reason), run_.bonesBroken(),
                 run_.totalBones(), run_.bestCombo(), run_.hits(), run_.distanceFallen(), run_.topSpeed(),
                 run_.hardestImpact(), injuries_.count(), contractsCompleted_.size(), payout_.total, progress_.cash,
                 leveledUp_ ? " -- LEVEL UP" : "");
}

void BrokenBonesGame::tickResults(float dt) {
    applyRagdollPose();
    if (!partWorld_.empty()) {
        updateRagdollCamera(dt, glm::vec3(partWorld_[0][3]), 0.0f);
    }
    resultsSeconds_ += dt;
    if (resultsSeconds_ < kResultsSeconds) return;

    app_.physics().destroyRagdoll(ragdoll_);
    ragdoll_ = core::Physics::kInvalidRagdoll;
    partWorld_.clear();
    level_ = progress_.level;
    rebuildMap();
    respawnPlayer();
}

void BrokenBonesGame::updateRagdollCamera(float dt, glm::vec3 focus, float speed) {
    glm::vec2 mouse = app_.input().mouseDelta();
    float sensitivity = 0.15f * progress_.settings.sensitivity;
    cameraYaw_ += mouse.x * sensitivity;
    cameraPitch_ = std::clamp(cameraPitch_ - mouse.y * sensitivity, -80.0f, 50.0f);

    if (glm::distance(cameraFocus_, focus) > 30.0f) cameraFocus_ = focus;
    cameraFocus_ = glm::mix(cameraFocus_, focus, 1.0f - std::exp(-14.0f * dt));

    core::Camera& camera = app_.camera();
    float daze = injuries_.daze();
    camera.yawDegrees = cameraYaw_ + daze * 5.0f * std::sin(overlayClock_ * 1.3f);
    camera.pitchDegrees = cameraPitch_ + daze * 3.5f * std::sin(overlayClock_ * 1.9f + 0.7f);
    glm::vec3 back = -camera.forward();
    glm::vec3 pivot = cameraFocus_ + glm::vec3(0.0f, 0.6f, 0.0f);
    float distance = 7.5f + std::min(speed, 50.0f) * 0.06f;

    // Ragdoll bodies carry kNullEntity, so hits on the body itself are ignored.
    constexpr float kRayStart = 0.6f;
    core::Physics::RaycastHit hit = app_.physics().raycast(pivot + back * kRayStart, back, distance - kRayStart);
    if (hit.hit && hit.entity != core::kNullEntity) distance = std::max(1.0f, kRayStart + hit.distance - 0.4f);

    glm::vec3 position = pivot + back * distance;
    if (shake_ > 0.0f) {
        std::uniform_real_distribution<float> jitter(-1.0f, 1.0f);
        if (progress_.settings.cameraShake) {
            position += glm::vec3(jitter(shakeRng_), jitter(shakeRng_), jitter(shakeRng_)) * (shake_ * 0.35f);
        }
        shake_ = std::max(0.0f, shake_ - dt * 2.5f);
    }
    camera.position = position;
    camera.verticalFovDegrees = kBaseFov + std::min(speed, 60.0f) * 0.25f;
}

void BrokenBonesGame::createGearVisuals() {
    core::ECS& ecs = app_.ecs();
    core::Renderer& renderer = app_.renderer();
    uint32_t rocketMesh = app_.meshLibrary().registerMesh(core::Mesh::createCapsule(
        renderer.allocator(), renderer.device(), renderer.commandPool(), renderer.graphicsQueue(), 0.13f, 0.42f));
    uint32_t balloonMesh = app_.meshLibrary().registerMesh(core::Mesh::createCapsule(
        renderer.allocator(), renderer.device(), renderer.commandPool(), renderer.graphicsQueue(), 0.32f, 0.1f));

    rocketEntity_ = ecs.createEntity("BrokenBonesRocket");
    auto& rocket = ecs.addComponent<core::Renderable>(rocketEntity_);
    rocket.meshHandle = rocketMesh;
    rocket.baseColor = glm::vec4(0.85f, 0.12f, 0.1f, 1.0f);
    rocket.metallic = 0.6f;
    rocket.roughness = 0.35f;
    rocket.visible = false;

    for (size_t i = 0; i < balloonEntities_.size(); ++i) {
        balloonEntities_[i] = ecs.createEntity("BrokenBonesFloat");
        auto& balloon = ecs.addComponent<core::Renderable>(balloonEntities_[i]);
        balloon.meshHandle = balloonMesh;
        balloon.baseColor = kBalloonColors[i];
        balloon.metallic = 0.0f;
        balloon.roughness = 0.25f;
        balloon.visible = false;
    }

    rocketFlameEntity_ = ecs.createEntity("BrokenBonesRocketFlame");
    auto& flame = ecs.addComponent<core::ParticleEmitter>(rocketFlameEntity_).settings;
    flame.enabled = false;
    flame.looping = true;
    flame.emissionRate = 140.0f;
    flame.particleLifetime = 0.4f;
    flame.particleLifetimeVariance = 0.12f;
    flame.gravity = glm::vec3(0.0f);
    flame.sizeStart = 0.3f;
    flame.sizeEnd = 0.05f;
    flame.colorStart = glm::vec4(1.0f, 0.85f, 0.4f, 1.0f);
    flame.colorEnd = glm::vec4(0.9f, 0.2f, 0.05f, 0.0f);

    rocketSmokeEntity_ = ecs.createEntity("BrokenBonesRocketSmoke");
    ecs.addComponent<core::ParticleEmitter>(rocketSmokeEntity_).settings = rocketSmoke();
    for (core::EntityId& bleed : bleedEntities_) {
        bleed = ecs.createEntity("BrokenBonesBleed");
        ecs.addComponent<core::ParticleEmitter>(bleed).settings = bloodDrip();
    }

    bombBurstEntity_ = ecs.createEntity("BrokenBonesBombBurst");
    auto& burst = ecs.addComponent<core::ParticleEmitter>(bombBurstEntity_).settings;
    burst.enabled = false;
    burst.looping = false;
    burst.emissionRate = 220.0f;
    burst.particleLifetime = 0.9f;
    burst.particleLifetimeVariance = 0.3f;
    burst.velocityMin = glm::vec3(-10.0f);
    burst.velocityMax = glm::vec3(10.0f);
    burst.gravity = glm::vec3(0.0f, -3.0f, 0.0f);
    burst.sizeStart = 0.6f;
    burst.sizeEnd = 0.1f;
    burst.colorStart = glm::vec4(1.0f, 0.8f, 0.3f, 1.0f);
    burst.colorEnd = glm::vec4(0.25f, 0.22f, 0.2f, 0.0f);
}

void BrokenBonesGame::updateGearVisuals(float dt) {
    core::ECS& ecs = app_.ecs();
    gearClock_ += dt;
    if (humanoid_.partJoint.empty()) return;

    glm::mat4 meshWorld(1.0f);
    const std::vector<glm::mat4>* skinning = nullptr;
    if (phase_ != Phase::Walking && !partWorld_.empty()) {
        meshWorld = glm::translate(glm::mat4(1.0f), glm::vec3(partWorld_[0][3]));
        skinning = &skinning_;
    } else if (!app_.localPlayerSkinnedEntities().empty()) {
        core::EntityId avatar = app_.localPlayerSkinnedEntities().front();
        auto* transform = ecs.tryGetComponent<core::Transform>(avatar);
        auto* skin = ecs.tryGetComponent<core::SkinnedRenderable>(avatar);
        if (transform != nullptr && skin != nullptr) {
            meshWorld = transform->matrix();
            skinning = &skin->skinningMatrices;
        }
    }

    const size_t chestJoint = static_cast<size_t>(humanoid_.partJoint[static_cast<size_t>(core::HumanoidRagdollPart::Chest)]);
    bool posed = skinning != nullptr && chestJoint < skinning->size();
    bool showRocket = posed && (rocket_.live() || rocketTiming_.active() || (phase_ == Phase::Walking && progress_.has(ShopItem::Rocket)));
    bool showFloats = posed && progress_.has(ShopItem::Floats) &&
                      (phase_ == Phase::Walking || !boosts_.helium().empty());
    setVisible(ecs, rocketEntity_, showRocket);
    for (core::EntityId balloon : balloonEntities_) setVisible(ecs, balloon, showFloats);
    auto* flame = ecs.tryGetComponent<core::ParticleEmitter>(rocketFlameEntity_);
    if (flame != nullptr) flame->settings.enabled = posed && rocket_.burning();
    auto* smoke = ecs.tryGetComponent<core::ParticleEmitter>(rocketSmokeEntity_);
    if (smoke != nullptr) smoke->settings.enabled = posed && rocket_.burning();
    if (!posed) return;

    const core::RagdollPartDesc& chest = humanoid_.desc.parts[static_cast<size_t>(core::HumanoidRagdollPart::Chest)];
    glm::vec3 chestCentre = 0.5f * (chest.from + chest.to);
    glm::mat4 chestFrame = meshWorld * (*skinning)[chestJoint];
    glm::quat chestRotation = glm::quat_cast(glm::mat3(chestFrame));
    auto toWorld = [&](glm::vec3 modelPoint) { return glm::vec3(chestFrame * glm::vec4(modelPoint, 1.0f)); };

    if (auto* transform = ecs.tryGetComponent<core::Transform>(rocketEntity_)) {
        transform->position = toWorld(chestCentre + glm::vec3(0.0f, -0.05f, -0.28f));
        transform->rotation = chestRotation;
    }
    if (flame != nullptr && flame->settings.enabled) {
        glm::vec3 nozzle = toWorld(chestCentre + glm::vec3(0.0f, -0.65f, -0.28f));
        glm::vec3 tip = toWorld(chestCentre + glm::vec3(0.0f, 0.5f, -0.28f));
        if (auto* transform = ecs.tryGetComponent<core::Transform>(rocketFlameEntity_)) transform->position = nozzle;
        glm::vec3 bodyVelocity = app_.physics().ragdollPartVelocity(ragdoll_, 0);
        glm::vec3 exhaust = bodyVelocity + glm::normalize(nozzle - tip) * 12.0f;
        flame->settings.velocityMin = exhaust - glm::vec3(1.5f);
        flame->settings.velocityMax = exhaust + glm::vec3(1.5f);
        if (smoke != nullptr) {
            if (auto* transform = ecs.tryGetComponent<core::Transform>(rocketSmokeEntity_)) transform->position = nozzle;
            glm::vec3 drift = bodyVelocity * 0.15f + glm::normalize(nozzle - tip) * 3.0f;
            smoke->settings.velocityMin = drift - glm::vec3(0.8f);
            smoke->settings.velocityMax = drift + glm::vec3(0.8f);
        }
    }

    glm::vec3 chestWorld = toWorld(chestCentre);
    float lift = boostState_.floatsLifting ? 0.35f : 0.0f;
    for (size_t i = 0; i < balloonEntities_.size(); ++i) {
        if (auto* transform = ecs.tryGetComponent<core::Transform>(balloonEntities_[i])) {
            float bob = 0.12f * std::sin(gearClock_ * 2.2f + static_cast<float>(i) * 2.1f);
            transform->position = chestWorld + kBalloonOffsets[i] + glm::vec3(0.0f, bob + lift, 0.0f);
            transform->scale = glm::vec3(1.0f, 1.2f, 1.0f);
        }
    }
}

void BrokenBonesGame::drawShop() {
    core::UIRenderer& ui = app_.uiRenderer();
    const glm::vec2 screen(static_cast<float>(app_.window().width()), static_cast<float>(app_.window().height()));
    constexpr float kRowHeight = 50.0f;
    const ShopPage& page = shopPage(shopPage_);
    glm::vec2 size(660.0f, 150.0f + kRowHeight * 7.0f);
    glm::vec2 pos = (screen - size) * 0.5f;
    ui.drawRect(pos, size, glm::vec4(0.02f, 0.03f, 0.05f, 0.88f));
    ui.drawText("BONE SHOP", pos + glm::vec2(24.0f, 18.0f), 1.1f, kGold);
    std::string cash = "$" + std::to_string(progress_.cash);
    glm::vec2 cashSize = ui.measureText(cash, 1.1f);
    ui.drawText(cash, glm::vec2(pos.x + size.x - cashSize.x - 24.0f, pos.y + 18.0f), 1.1f, kGreen);

    float tabX = pos.x + 24.0f;
    for (size_t t = 0; t < kShopPageCount; ++t) {
        const char* name = shopPage(t).name;
        glm::vec2 tabSize = ui.measureText(name, 0.6f) + glm::vec2(20.0f, 10.0f);
        bool current = t == shopPage_;
        ui.drawRect(glm::vec2(tabX, pos.y + 56.0f), tabSize,
                    current ? glm::vec4(1.0f, 0.8f, 0.2f, 0.25f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.06f));
        ui.drawText(name, glm::vec2(tabX + 10.0f, pos.y + 61.0f), 0.6f, current ? kGold : kDim);
        tabX += tabSize.x + 8.0f;
    }

    char line[160];
    for (size_t i = 0; i < page.count; ++i) {
        ShopItem item = page.items[i];
        const ShopItemInfo& info = shopItemInfo(item);
        float y = pos.y + 100.0f + kRowHeight * static_cast<float>(i);
        int owned = progress_.count(item);
        int price = shopPrice(progress_, item);
        bool maxed = owned >= info.maxOwned;
        bool levelLocked = progress_.bestLevel < info.unlockLevel;
        bool needsPrereq =
            info.prerequisite != kNoPrerequisite && !progress_.has(static_cast<ShopItem>(info.prerequisite));
        bool rebirthLocked = progress_.rebirths < info.rebirthsRequired;
        bool locked = levelLocked || needsPrereq || rebirthLocked;
        bool affordable = progress_.cash >= price;

        ui.drawRect(glm::vec2(pos.x + 16.0f, y), glm::vec2(size.x - 32.0f, kRowHeight - 6.0f),
                    glm::vec4(1.0f, 1.0f, 1.0f, 0.06f));
        std::snprintf(line, sizeof(line), "[%zu] %s", i + 1, info.name);
        ui.drawText(line, glm::vec2(pos.x + 28.0f, y + 2.0f), 0.65f, maxed || locked ? kDim : kWhite);
        ui.drawText(info.description, glm::vec2(pos.x + 28.0f, y + 23.0f), 0.48f, kDim);

        if (rebirthLocked) {
            std::snprintf(line, sizeof(line), "REBIRTH %d", info.rebirthsRequired);
        } else if (levelLocked) {
            std::snprintf(line, sizeof(line), "LEVEL %d", info.unlockLevel);
        } else if (needsPrereq) {
            std::snprintf(line, sizeof(line), "NEEDS %s", shopItemInfo(static_cast<ShopItem>(info.prerequisite)).name);
        } else if (item == ShopItem::MoreBones && maxed) {
            std::snprintf(line, sizeof(line), "%d BONES - MAXED", skeletonBoneCount(progress_));
        } else if (item == ShopItem::MoreBones) {
            std::snprintf(line, sizeof(line), "$%d   %d -> %d bones", price, skeletonBoneCount(progress_),
                          kBoneTiers[static_cast<size_t>(owned) + 1]);
        } else if (maxed) {
            if (info.consumable) {
                std::snprintf(line, sizeof(line), "FULL %d/%d", owned, info.maxOwned);
            } else {
                std::snprintf(line, sizeof(line), "MAXED");
            }
        } else if (info.maxOwned > 1) {
            std::snprintf(line, sizeof(line), "$%d   %d/%d", price, owned, info.maxOwned);
        } else {
            std::snprintf(line, sizeof(line), "$%d", price);
        }
        glm::vec2 tagSize = ui.measureText(line, 0.65f);
        glm::vec4 tagColor = locked ? kDim : (maxed ? kGreen : (!affordable ? kRed : kGold));
        ui.drawText(line, glm::vec2(pos.x + size.x - tagSize.x - 30.0f, y + 10.0f), 0.65f, tagColor);
    }
    ui.drawText("[1-9] buy    [LEFT/RIGHT] page    [B] close    [M] switch map", pos + glm::vec2(24.0f, size.y - 34.0f), 0.55f,
                kDim);
}

void BrokenBonesGame::drawHud() {
    core::UIRenderer& ui = app_.uiRenderer();
    const glm::vec2 screen(static_cast<float>(app_.window().width()), static_cast<float>(app_.window().height()));
    ui.beginFrame(VkExtent2D{app_.window().width(), app_.window().height()});
    char line[200];
    if (menu_ == Menu::Title) return;
    drawInjuryOverlay(screen);

    auto centered = [&](const std::string& text, float y, float scale, glm::vec4 color) {
        glm::vec2 size = ui.measureText(text, scale);
        ui.drawText(text, glm::vec2((screen.x - size.x) * 0.5f, y), scale, color);
    };
    auto bar = [&](const char* label, float fraction, glm::vec2 at, glm::vec4 color) {
        ui.drawText(label, at, 0.55f, kDim);
        glm::vec2 barPos = at + glm::vec2(150.0f, 2.0f);
        ui.drawRect(barPos, glm::vec2(120.0f, 14.0f), glm::vec4(1.0f, 1.0f, 1.0f, 0.15f));
        ui.drawRect(barPos, glm::vec2(120.0f * std::clamp(fraction, 0.0f, 1.0f), 14.0f), color);
    };

    bool walking = phase_ == Phase::Walking;
    ui.drawRect(glm::vec2(16.0f, 16.0f), glm::vec2(300.0f, walking ? 92.0f : 118.0f), kPanel);
    std::snprintf(line, sizeof(line), "LEVEL %d   %s %.0f m", level_, mapThemeInfo(layout_.theme).name, layout_.height);
    ui.drawText(line, glm::vec2(28.0f, 26.0f), 0.8f, kGold);
    std::snprintf(line, sizeof(line), "Break %d bones to climb higher", bonesNeededForLevel(level_));
    ui.drawText(line, glm::vec2(28.0f, 54.0f), 0.6f, kDim);

    if (walking) {
        ui.drawText("$" + std::to_string(progress_.cash), glm::vec2(28.0f, 74.0f), 0.7f, kGreen);

        float contractsY = 124.0f;
        ui.drawRect(glm::vec2(16.0f, contractsY - 10.0f),
                    glm::vec2(420.0f, 36.0f + 24.0f * static_cast<float>(progress_.contracts.size())), kPanel);
        ui.drawText("CONTRACTS", glm::vec2(28.0f, contractsY), 0.6f, kGold);
        for (size_t i = 0; i < progress_.contracts.size(); ++i) {
            const Contract& contract = progress_.contracts[i];
            float y = contractsY + 26.0f + 24.0f * static_cast<float>(i);
            ui.drawText(contractText(contract), glm::vec2(28.0f, y), 0.55f, kWhite);
            std::string reward = "$" + std::to_string(contract.reward);
            glm::vec2 rewardSize = ui.measureText(reward, 0.55f);
            ui.drawText(reward, glm::vec2(424.0f - rewardSize.x, y), 0.55f, kGreen);
        }

        std::snprintf(line, sizeof(line), "Walk off the edge!   [F] %s   [X] bomb (%d)   [Q] rocket (%d)   [B] shop   [M] map   [R] new cliff",
                      progress_.has(ShopItem::SpringShoes) ? "leap" : "flop", progress_.count(ShopItem::Bomb),
                      progress_.count(ShopItem::Rocket));
        centered(line, screen.y - 56.0f, 0.65f, kWhite);
        if (shopOpen_) drawShop();
    } else {
        std::snprintf(line, sizeof(line), "BONES %d / %d    HITS %d", run_.bonesBroken(), run_.totalBones(),
                      run_.hits());
        ui.drawText(line, glm::vec2(28.0f, 82.0f), 0.7f, kWhite);
        float speed = partWorld_.empty() ? 0.0f : glm::length(app_.physics().ragdollPartVelocity(ragdoll_, 0));
        std::snprintf(line, sizeof(line), "FALLEN %.0f m   SPEED %.0f m/s", run_.distanceFallen(), speed);
        ui.drawText(line, glm::vec2(28.0f, 106.0f), 0.6f, kDim);

        float gearY = 146.0f;
        int gearRows = progress_.has(ShopItem::Floats) + (rocket_.burning() ? 1 : 0) + 1;
        ui.drawRect(glm::vec2(16.0f, gearY - 10.0f), glm::vec2(300.0f, 12.0f + 26.0f * static_cast<float>(gearRows)),
                    kPanel);
        if (progress_.has(ShopItem::Floats)) {
            std::snprintf(line, sizeof(line), "[SHIFT] FLOAT %.1fs", boosts_.helium().remaining());
            bar(line, boosts_.helium().fraction(), glm::vec2(28.0f, gearY), glm::vec4(0.4f, 0.7f, 1.0f, 1.0f));
            gearY += 26.0f;
        }
        if (rocket_.burning()) {
            bar(rocketSuper_ ? "SUPER BOOST" : "ROCKET BURN", rocket_.burnFraction(), glm::vec2(28.0f, gearY),
                rocketSuper_ ? kGold : kOrange);
            gearY += 26.0f;
        }
        std::snprintf(line, sizeof(line), "[X] BOMBS %d    [Q] ROCKETS %d", progress_.count(ShopItem::Bomb),
                      progress_.count(ShopItem::Rocket));
        ui.drawText(line, glm::vec2(28.0f, gearY), 0.55f, kWhite);

        if (phase_ == Phase::Falling && rocketTiming_.active()) {
            constexpr float kBarWidth = 520.0f;
            constexpr float kBarHeight = 28.0f;
            glm::vec2 at((screen.x - kBarWidth) * 0.5f, screen.y * 0.64f);
            centered("PRESS [Q] IN THE GREEN!", at.y - 36.0f, 0.8f, kWhite);
            ui.drawRect(at - glm::vec2(4.0f), glm::vec2(kBarWidth, kBarHeight) + glm::vec2(8.0f), kPanel);
            ui.drawRect(at, glm::vec2(kBarWidth, kBarHeight), glm::vec4(0.75f, 0.12f, 0.08f, 0.85f));
            ui.drawRect(glm::vec2(at.x + kBarWidth * rocketTiming_.greenMin(), at.y),
                        glm::vec2(kBarWidth * (rocketTiming_.greenMax() - rocketTiming_.greenMin()), kBarHeight),
                        glm::vec4(0.15f, 0.9f, 0.3f, 0.95f));
            ui.drawRect(glm::vec2(at.x + kBarWidth * rocketTiming_.marker() - 3.0f, at.y - 8.0f),
                        glm::vec2(6.0f, kBarHeight + 16.0f), kWhite);
            std::snprintf(line, sizeof(line), "miss = KABOOM    wait %.1fs = normal rocket", rocketTiming_.timeLeft());
            centered(line, at.y + kBarHeight + 14.0f, 0.5f, kDim);
        }

        int combo = run_.liveCombo();
        if (phase_ == Phase::Falling && combo > 1) {
            std::snprintf(line, sizeof(line), "COMBO x%d", combo);
            centered(line, screen.y * 0.32f, 1.1f, kGold);
        }

        constexpr float kListWidth = 236.0f;
        float listX = screen.x - kListWidth - 16.0f;
        ui.drawRect(glm::vec2(listX, 16.0f), glm::vec2(kListWidth, 20.0f + 22.0f * core::kHumanoidRagdollPartCount),
                    kPanel);
        for (size_t p = 0; p < core::kHumanoidRagdollPartCount; ++p) {
            auto part = static_cast<core::HumanoidRagdollPart>(p);
            float rowY = 26.0f + 22.0f * static_cast<float>(p);
            int partBones = run_.bonesInPart(part);
            int broken = run_.brokenInPart(part);
            glm::vec4 color = broken == 0 ? kDim : (broken >= partBones ? kRed : kOrange);
            ui.drawText(boneName(part), glm::vec2(listX + 12.0f, rowY), 0.55f, color);
            if (partBones > 1) {
                std::snprintf(line, sizeof(line), "%d/%d", broken, partBones);
                glm::vec2 countSize = ui.measureText(line, 0.5f);
                ui.drawText(line, glm::vec2(listX + kListWidth - countSize.x - 10.0f, rowY), 0.5f, color);
            }
        }
        if (injuries_.count() > 0) {
            float injuryTop = 16.0f + 20.0f + 22.0f * core::kHumanoidRagdollPartCount + 12.0f;
            ui.drawRect(glm::vec2(listX, injuryTop),
                        glm::vec2(kListWidth, 34.0f + 22.0f * static_cast<float>(injuries_.count())), kPanel);
            ui.drawText("INJURIES", glm::vec2(listX + 12.0f, injuryTop + 8.0f), 0.55f, kRed);
            for (int i = 0; i < injuries_.count(); ++i) {
                ui.drawText(injuryInfo(injuries_.injuries()[static_cast<size_t>(i)].injury).name,
                            glm::vec2(listX + 12.0f, injuryTop + 32.0f + 22.0f * static_cast<float>(i)), 0.5f, kOrange);
            }
        }
    }

    if (toastSeconds_ > 0.0f) {
        glm::vec4 color = toastColor_;
        color.a = std::min(1.0f, toastSeconds_ * 2.0f);
        centered(toast_, screen.y * 0.22f, 1.3f, color);
    }
    if (injuryTextSeconds_ > 0.0f) {
        centered(injuryText_, screen.y * 0.22f + 52.0f, 0.9f,
                 glm::vec4(1.0f, 0.25f, 0.2f, std::min(1.0f, injuryTextSeconds_ * 2.0f)));
    }

    if (phase_ != Phase::Results) return;
    float extraRows = static_cast<float>(contractsCompleted_.size() + (newBests_.empty() ? 0 : 1) +
                                         (injuries_.count() > 0 ? 1 : 0) + (boardPlacements_.empty() ? 0 : 1));
    glm::vec2 panelSize(640.0f, 240.0f + 24.0f * extraRows);
    glm::vec2 panelPos = (screen - panelSize) * 0.5f;
    ui.drawRect(panelPos, panelSize, glm::vec4(0.0f, 0.0f, 0.0f, 0.65f));
    const char* reasonText = endReason_ == RunEndReason::Splashdown ? mapThemeInfo(layout_.theme).splashText
                                                                     : runEndReasonText(endReason_);
    std::string title = payout_.abandoned ? std::string("RUN ABANDONED - HALF BONE CASH")
                                          : std::string("RUN OVER - ") + reasonText;
    centered(title, panelPos.y + 18.0f, 1.0f, kGold);
    std::snprintf(line, sizeof(line), "%d bones broken   best combo x%d   %d hits", run_.bonesBroken(),
                  run_.bestCombo(), run_.hits());
    centered(line, panelPos.y + 60.0f, 0.75f, kWhite);
    std::snprintf(line, sizeof(line), "Fell %.0f m   Top speed %.0f m/s   Hardest hit %.0f m/s", run_.distanceFallen(),
                  run_.topSpeed(), run_.hardestImpact());
    centered(line, panelPos.y + 90.0f, 0.6f, kDim);
    std::snprintf(line, sizeof(line), "+$%d", payout_.total);
    centered(line, panelPos.y + 116.0f, 1.0f, kGreen);
    if (payout_.abandoned) {
        std::snprintf(line, sizeof(line), "bones $%d  x%.2f level", payout_.bones, payout_.levelMultiplier);
    } else {
        std::snprintf(line, sizeof(line),
                      "bones $%d  combo $%d  hits $%d  fall $%d  big hit $%d  splash $%d  injuries $%d  x%.2f level  "
                      "x%.2f bonus",
                      payout_.bones, payout_.combo, payout_.hits, payout_.distance, payout_.bigHit, payout_.splash,
                      payout_.injuries, payout_.levelMultiplier, payout_.cashMultiplier);
    }
    centered(line, panelPos.y + 148.0f, 0.48f, kDim);

    float y = panelPos.y + 172.0f;
    for (const auto& [text, reward] : contractsCompleted_) {
        std::snprintf(line, sizeof(line), "CONTRACT DONE: %s  +$%d", text.c_str(), reward);
        centered(line, y, 0.6f, kGreen);
        y += 24.0f;
    }
    if (injuries_.count() > 0) {
        std::string list = "INJURIES:";
        for (int i = 0; i < injuries_.count(); ++i) {
            list += (i == 0 ? " " : ", ") + std::string(injuryInfo(injuries_.injuries()[static_cast<size_t>(i)].injury).name);
        }
        if (injuries_.bleedSeconds() > 0.0f) list += "  (bled " + std::to_string(static_cast<int>(injuries_.bleedSeconds())) + "s)";
        centered(list, y, 0.55f, kOrange);
        y += 24.0f;
    }
    if (!boardPlacements_.empty()) {
        std::string board = "LEADERBOARD:";
        for (size_t i = 0; i < boardPlacements_.size(); ++i) {
            board += (i == 0 ? " #" : ",  #") + std::to_string(boardPlacements_[i].second) + " " +
                     boardStatName(boardPlacements_[i].first);
        }
        centered(board, y, 0.6f, kGold);
        y += 24.0f;
    }
    if (!newBests_.empty()) {
        std::string bests = "NEW BEST:";
        for (size_t i = 0; i < newBests_.size(); ++i) bests += (i == 0 ? " " : ", ") + newBests_[i];
        centered(bests, y, 0.6f, kGold);
        y += 24.0f;
    }
    if (leveledUp_) {
        std::snprintf(line, sizeof(line), "LEVEL UP! Next cliff: %.0f m", cliffHeightForLevel(level_ + 1));
        centered(line, y + 4.0f, 0.8f, kGold);
    } else {
        std::snprintf(line, sizeof(line), "Break %d bones to reach level %d", bonesNeededForLevel(level_), level_ + 1);
        centered(line, y + 4.0f, 0.7f, kWhite);
    }
    std::snprintf(line, sizeof(line), "New cliff in %.0f...   [R] skip",
                  std::ceil(std::max(0.0f, kResultsSeconds - resultsSeconds_)));
    centered(line, panelPos.y + panelSize.y - 30.0f, 0.55f, kDim);
}

} // namespace engine::brokenbones
