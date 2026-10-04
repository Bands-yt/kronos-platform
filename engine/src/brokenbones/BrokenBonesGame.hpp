#pragma once

#include <array>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "brokenbones/Boosts.hpp"
#include "brokenbones/CliffGenerator.hpp"
#include "brokenbones/CliffMap.hpp"
#include "brokenbones/Effects.hpp"
#include "brokenbones/Injuries.hpp"
#include "brokenbones/Meta.hpp"
#include "brokenbones/Music.hpp"
#include "brokenbones/RunRules.hpp"
#include "brokenbones/Shop.hpp"
#include "brokenbones/SoundBank.hpp"
#include "core/ECS.hpp"
#include "core/HumanoidRagdoll.hpp"
#include "core/Physics.hpp"
#include "core/ProceduralMaterials.hpp"

namespace engine::core { class Application; }

namespace engine::brokenbones {

// Drives a Broken Bones session: walk to the edge as the normal avatar, go
// ragdoll once falling, score the fall, then rebuild a fresh cliff.
class BrokenBonesGame {
public:
    enum class Phase { Walking, Falling, Results };
    enum class Menu { None, Title, Pause, Settings, Achievements, Stats, Rebirth, Leaderboard };

    BrokenBonesGame(core::Application& app, core::ProceduralMaterialLibrary materials, uint32_t firstSeed,
                    std::string savePath);
    ~BrokenBonesGame();

    // level <= 0 resumes the saved level.
    [[nodiscard]] bool start(int level);
    void tick(float dt);
    // Credits and saves a run still in progress; call before the application shuts down.
    void finish();

    [[nodiscard]] Phase phase() const { return phase_; }
    [[nodiscard]] Menu menu() const { return menu_; }
    [[nodiscard]] int level() const { return level_; }
    [[nodiscard]] const CliffLayout& layout() const { return layout_; }

private:
    bool rebuildMap();
    void respawnPlayer();
    bool startFalling();
    void detonateBomb();
    void fireRocket(float delaySeconds = 0.0f);
    void igniteRocket(bool super);
    void explodeRocket(glm::vec3 centre, bool midAir);
    void announceBreaks(const std::vector<BoneBreak>& breaks, const char* prefix);
    void handleInjuries(const std::vector<InjuryHit>& hits, glm::vec3 point, float impactSpeed);
    void updateBleeding();
    void updateMusic(float dt);
    void drawInjuryOverlay(glm::vec2 screen);
    void endRun(RunEndReason reason);
    void tickWalking(float dt);
    void tickShop();
    void switchMap(MapTheme theme);
    [[nodiscard]] float payoutMultiplier() const;
    void tickFalling(float dt);
    void tickResults(float dt);
    void applyRagdollPose();
    void updateRagdollCamera(float dt, glm::vec3 focus, float speed);
    void createGearVisuals();
    void updateGearVisuals(float dt);
    void save();
    void showToast(std::string text, glm::vec4 color, float seconds);
    bool pressed(const char* action, bool& wasDown);
    void drawHud();
    void queueSound(float delaySeconds, Sfx sfx, float volume = 1.0f, float pitch = 1.0f);
    void updateSounds(float dt);
    void drawShop();

    void openMenu(Menu menu);
    void closeMenu();
    void tickMenu(bool backPressed);
    void activateMenuItem();
    void adjustSetting(int direction);
    void drawMenu();
    void drawOverlays();
    void applySettings();
    void notifyTutorial(TutorialEvent event);
    void grantAchievements(const RunSummary* run);
    void triggerSlowMotion();
    [[nodiscard]] int menuItemCount() const;

    core::Application& app_;
    core::ProceduralMaterialLibrary materials_;
    std::mt19937 seedRng_;
    std::string savePath_;
    Progress progress_;

    int level_ = 1;
    CliffLayout layout_;
    CliffMap map_;

    core::HumanoidRagdoll humanoid_;
    core::Physics::RagdollHandle ragdoll_ = core::Physics::kInvalidRagdoll;
    std::vector<glm::mat4> partWorld_;
    std::vector<glm::mat4> skinning_;

    Phase phase_ = Phase::Walking;
    RunTracker run_;
    RunEndReason endReason_ = RunEndReason::None;
    RunPayout payout_;
    bool leveledUp_ = false;
    std::vector<std::pair<std::string, int>> contractsCompleted_;
    std::vector<std::string> newBests_;
    std::mt19937 contractRng_;
    float resultsSeconds_ = 0.0f;
    float walkingSeconds_ = 0.0f;

    BoostController boosts_;
    BoostState boostState_;
    RocketController rocket_;
    RocketTimingGame rocketTiming_;
    float rocketGuardSeconds_ = 0.0f;
    bool rocketSuper_ = false;
    float bombCooldown_ = 0.0f;
    bool shopOpen_ = false;

    core::EntityId rocketEntity_ = core::kNullEntity;
    core::EntityId rocketFlameEntity_ = core::kNullEntity;
    core::EntityId bombBurstEntity_ = core::kNullEntity;
    core::EntityId rocketSmokeEntity_ = core::kNullEntity;
    std::array<core::EntityId, InjuryTracker::kMaxBleeds> bleedEntities_{};
    EffectsPool effects_;
    float dustCooldown_ = 0.0f;
    std::array<core::EntityId, 3> balloonEntities_{core::kNullEntity, core::kNullEntity, core::kNullEntity};
    float gearClock_ = 0.0f;

    glm::vec3 cameraFocus_{0.0f};
    float cameraYaw_ = 90.0f;
    float cameraPitch_ = -20.0f;
    float shake_ = 0.0f;
    std::mt19937 shakeRng_{7};

    struct QueuedSound {
        float delay;
        Sfx sfx;
        float volume;
        float pitch;
    };
    SoundBank sounds_;
    MusicPlayer music_;
    std::vector<QueuedSound> soundQueue_;
    float thudCooldown_ = 0.0f;
    bool wasFloating_ = false;

    std::string toast_;
    glm::vec4 toastColor_{1.0f};
    float toastSeconds_ = 0.0f;
    bool restartWasDown_ = false;
    bool flopWasDown_ = false;
    bool bombWasDown_ = false;
    bool rocketWasDown_ = false;
    bool shopWasDown_ = false;
    static constexpr size_t kBuyKeyCount = 9;
    std::array<bool, kBuyKeyCount> buyWasDown_{};
    bool pagePrevWasDown_ = false;
    bool pageNextWasDown_ = false;
    bool mapWasDown_ = false;
    size_t shopPage_ = 0;

    Menu menu_ = Menu::Title;
    Menu settingsReturn_ = Menu::Title;
    int menuCursor_ = 0;
    bool rebirthArmed_ = false;
    float autoFlopSeconds_ = 0.0f;
    bool menuUpWasDown_ = false;
    bool menuDownWasDown_ = false;
    bool menuLeftWasDown_ = false;
    bool menuRightWasDown_ = false;
    bool menuSelectWasDown_ = false;
    bool menuBackWasDown_ = false;

    float slowMoSeconds_ = 0.0f;
    float slowMoCooldown_ = 0.0f;
    int runBombs_ = 0;
    int runRockets_ = 0;
    int runSuperBoosts_ = 0;
    int runMisfires_ = 0;
    std::vector<Achievement> bannerQueue_;
    float bannerSeconds_ = 0.0f;

    InjuryTracker injuries_;
    std::string injuryText_;
    float injuryTextSeconds_ = 0.0f;
    float injuryFlash_ = 0.0f;
    float overlayClock_ = 0.0f;
    float splashSpeed_ = 0.0f;
    std::vector<std::pair<BoardStat, int>> boardPlacements_;
    size_t boardTab_ = 0;
};

} // namespace engine::brokenbones
