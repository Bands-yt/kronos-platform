#include "brokenbones/BrokenBonesGame.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <SDL2/SDL.h>

#include "brokenbones/HudStyle.hpp"
#include "core/Application.hpp"
#include "runtime/GameLoop.hpp"

namespace engine::brokenbones {

using namespace hud;

namespace {

constexpr const char* kTitleItems[] = {"PLAY", "LEADERBOARD", "ACHIEVEMENTS", "STATS", "REBIRTH", "SETTINGS", "QUIT"};
constexpr const char* kPauseItems[] = {"RESUME", "SETTINGS",  "LEADERBOARD", "ACHIEVEMENTS",
                                       "STATS",  "REBIRTH",   "MAIN MENU",   "QUIT"};
constexpr const char* kSettingsItems[] = {"VOLUME", "MUSIC", "MOUSE SENSITIVITY", "SLOW MOTION",
                                          "CAMERA SHAKE", "TIPS", "REPLAY TUTORIAL", "BACK"};
constexpr const char* kRebirthItems[] = {"REBIRTH NOW", "CANCEL"};
constexpr const char* kBackItems[] = {"BACK"};

struct MenuItems {
    const char* const* items;
    int count;
};

MenuItems menuItems(BrokenBonesGame::Menu menu) {
    using Menu = BrokenBonesGame::Menu;
    switch (menu) {
        case Menu::Title: return {kTitleItems, static_cast<int>(std::size(kTitleItems))};
        case Menu::Pause: return {kPauseItems, static_cast<int>(std::size(kPauseItems))};
        case Menu::Settings: return {kSettingsItems, static_cast<int>(std::size(kSettingsItems))};
        case Menu::Rebirth: return {kRebirthItems, static_cast<int>(std::size(kRebirthItems))};
        case Menu::Achievements:
        case Menu::Leaderboard:
        case Menu::Stats: return {kBackItems, 1};
        case Menu::None: break;
    }
    return {nullptr, 0};
}

std::string formatPlayTime(double seconds) {
    int minutes = static_cast<int>(seconds / 60.0);
    char text[32];
    std::snprintf(text, sizeof(text), "%dh %02dm", minutes / 60, minutes % 60);
    return text;
}

} // namespace

int BrokenBonesGame::menuItemCount() const { return menuItems(menu_).count; }

void BrokenBonesGame::openMenu(Menu menu) {
    if (menu == Menu::Settings || menu == Menu::Achievements || menu == Menu::Stats || menu == Menu::Leaderboard ||
        menu == Menu::Rebirth) {
        if (menu_ == Menu::Title || menu_ == Menu::Pause) settingsReturn_ = menu_;
    }
    menu_ = menu;
    menuCursor_ = 0;
    app_.gameLoop()->setTimeScale(0.0f);
    app_.setMovementInputSuspended(true);
    app_.input().setRelativeMouseMode(false);
    sounds_.stopAllLoops();
}

void BrokenBonesGame::closeMenu() {
    menu_ = Menu::None;
    app_.gameLoop()->setTimeScale(1.0f);
    app_.setMovementInputSuspended(phase_ != Phase::Walking);
    app_.input().setRelativeMouseMode(true);
    save();
}

void BrokenBonesGame::tickMenu(bool backPressed) {
    int count = menuItemCount();
    if (pressed("BrokenBonesMenuUp", menuUpWasDown_)) {
        menuCursor_ = (menuCursor_ + count - 1) % count;
        sounds_.play(Sfx::Click, 0.5f, 1.2f);
    }
    if (pressed("BrokenBonesMenuDown", menuDownWasDown_)) {
        menuCursor_ = (menuCursor_ + 1) % count;
        sounds_.play(Sfx::Click, 0.5f, 1.2f);
    }
    bool left = pressed("BrokenBonesMenuLeft", menuLeftWasDown_);
    bool right = pressed("BrokenBonesMenuRight", menuRightWasDown_);
    if (menu_ == Menu::Settings && (left || right)) adjustSetting(right ? 1 : -1);
    if (menu_ == Menu::Leaderboard && (left || right)) {
        boardTab_ = (boardTab_ + (right ? 1 : kBoardStatCount - 1)) % kBoardStatCount;
        sounds_.play(Sfx::Click, 0.6f, 1.1f);
    }
    if (pressed("BrokenBonesMenuSelect", menuSelectWasDown_)) activateMenuItem();

    if (!backPressed) return;
    sounds_.play(Sfx::Click, 0.6f, 0.85f);
    switch (menu_) {
        case Menu::Pause: closeMenu(); break;
        case Menu::Settings:
        case Menu::Achievements:
        case Menu::Leaderboard:
        case Menu::Stats: openMenu(settingsReturn_); break;
        case Menu::Rebirth: openMenu(settingsReturn_); break;
        case Menu::Title:
        case Menu::None: break;
    }
}

void BrokenBonesGame::activateMenuItem() {
    std::string item = menuItems(menu_).items[menuCursor_];
    sounds_.play(Sfx::Click, 0.8f);
    if (item == "PLAY" || item == "RESUME") {
        closeMenu();
    } else if (item == "SETTINGS") {
        openMenu(Menu::Settings);
    } else if (item == "ACHIEVEMENTS") {
        openMenu(Menu::Achievements);
    } else if (item == "STATS") {
        openMenu(Menu::Stats);
    } else if (item == "LEADERBOARD") {
        openMenu(Menu::Leaderboard);
    } else if (item == "MAIN MENU") {
        openMenu(Menu::Title);
    } else if (item == "QUIT") {
        SDL_Event quit{};
        quit.type = SDL_QUIT;
        SDL_PushEvent(&quit);
    } else if (item == "BACK") {
        openMenu(settingsReturn_);
    } else if (item == "REBIRTH") {
        if (!canRebirth(progress_)) {
            showToast("REACH LEVEL " + std::to_string(rebirthLevelRequired(progress_)) + " TO REBIRTH", kRed, 2.0f);
            sounds_.play(Sfx::Denied, 0.6f);
        } else if (phase_ != Phase::Walking) {
            showToast("FINISH YOUR RUN FIRST", kRed, 2.0f);
            sounds_.play(Sfx::Denied, 0.6f);
        } else {
            openMenu(Menu::Rebirth);
        }
    } else if (item == "CANCEL") {
        openMenu(settingsReturn_);
    } else if (item == "REBIRTH NOW") {
        if (!rebirth(progress_)) return;
        level_ = progress_.level;
        refillContracts(progress_, contractRng_);
        grantAchievements(nullptr);
        save();
        closeMenu();
        rebuildMap();
        respawnPlayer();
        char text[64];
        std::snprintf(text, sizeof(text), "REBORN!  CASH x%.1f FOREVER", rebirthMultiplier(progress_));
        showToast(text, kGold, 3.5f);
        sounds_.play(Sfx::Fanfare, 1.0f);
        std::fprintf(stdout, "brokenbones: rebirth %d.\n", progress_.rebirths);
    } else if (item == "REPLAY TUTORIAL") {
        progress_.tutorialStep = 0;
        progress_.settings.tips = true;
        showToast("TUTORIAL RESTARTED", kGreen, 1.5f);
    } else {
        adjustSetting(1);
    }
}

void BrokenBonesGame::adjustSetting(int direction) {
    GameSettings& settings = progress_.settings;
    float step = 0.1f * static_cast<float>(direction);
    auto nudge = [step](float value, float lo, float hi) {
        return std::clamp(std::round((value + step) * 10.0f) / 10.0f, lo, hi);
    };
    std::string item = menuItems(menu_).items[menuCursor_];
    if (item == "VOLUME") {
        settings.volume = nudge(settings.volume, 0.0f, 1.0f);
    } else if (item == "MUSIC") {
        settings.music = nudge(settings.music, 0.0f, 1.0f);
    } else if (item == "MOUSE SENSITIVITY") {
        settings.sensitivity = nudge(settings.sensitivity, 0.2f, 3.0f);
    } else if (item == "SLOW MOTION") {
        settings.slowMotion = !settings.slowMotion;
    } else if (item == "CAMERA SHAKE") {
        settings.cameraShake = !settings.cameraShake;
    } else if (item == "TIPS") {
        settings.tips = !settings.tips;
    } else {
        return;
    }
    applySettings();
    sounds_.play(Sfx::Click, 0.6f, 1.0f + 0.1f * static_cast<float>(direction));
}

void BrokenBonesGame::applySettings() {
    app_.audio().setMasterVolume(progress_.settings.volume);
    app_.characterController().settingsMutable().mouseSensitivity = 0.15f * progress_.settings.sensitivity;
}

void BrokenBonesGame::notifyTutorial(TutorialEvent event) {
    int next = advanceTutorial(progress_.tutorialStep, event);
    if (next == progress_.tutorialStep) return;
    progress_.tutorialStep = next;
    save();
}

void BrokenBonesGame::grantAchievements(const RunSummary* run) {
    std::vector<Achievement> unlocked = unlockAchievements(progress_, run);
    if (unlocked.empty()) return;
    if (bannerQueue_.empty()) {
        bannerSeconds_ = 3.0f;
        queueSound(0.3f, Sfx::Fanfare, 0.7f, 1.15f);
    }
    for (Achievement achievement : unlocked) {
        bannerQueue_.push_back(achievement);
        std::fprintf(stdout, "brokenbones: achievement unlocked -- %s (+$%d).\n", achievementInfo(achievement).name,
                     achievementInfo(achievement).reward);
    }
}

void BrokenBonesGame::triggerSlowMotion() {
    if (!progress_.settings.slowMotion || slowMoCooldown_ > 0.0f || phase_ != Phase::Falling) return;
    slowMoSeconds_ = 0.8f;
    slowMoCooldown_ = 4.0f;
}

void BrokenBonesGame::drawOverlays() {
    core::UIRenderer& ui = app_.uiRenderer();
    const glm::vec2 screen(static_cast<float>(app_.window().width()), static_cast<float>(app_.window().height()));
    auto centered = [&](const std::string& text, float y, float scale, glm::vec4 color) {
        glm::vec2 size = ui.measureText(text, scale);
        ui.drawText(text, glm::vec2((screen.x - size.x) * 0.5f, y), scale, color);
    };

    const char* tip = progress_.settings.tips ? tutorialTip(progress_.tutorialStep, phase_ == Phase::Falling) : nullptr;
    if (tip != nullptr && phase_ != Phase::Results && !shopOpen_) {
        glm::vec2 size = ui.measureText(tip, 0.7f) + glm::vec2(40.0f, 24.0f);
        glm::vec2 at((screen.x - size.x) * 0.5f, screen.y - 140.0f);
        ui.drawRect(at, size, glm::vec4(0.05f, 0.25f, 0.12f, 0.85f));
        ui.drawText(tip, at + glm::vec2(20.0f, 10.0f), 0.7f, kWhite);
    }

    if (bannerQueue_.empty() || bannerSeconds_ <= 0.0f) return;
    const AchievementInfo& info = achievementInfo(bannerQueue_.front());
    float alpha = std::min(1.0f, bannerSeconds_ * 2.0f);
    glm::vec2 size(520.0f, 92.0f);
    glm::vec2 at((screen.x - size.x) * 0.5f, 14.0f);
    ui.drawRect(at, size, glm::vec4(0.12f, 0.08f, 0.0f, 0.85f * alpha));
    centered("ACHIEVEMENT UNLOCKED", at.y + 8.0f, 0.55f, glm::vec4(kGold.r, kGold.g, kGold.b, alpha));
    centered(info.name, at.y + 30.0f, 0.95f, glm::vec4(1.0f, 1.0f, 1.0f, alpha));
    char line[96];
    std::snprintf(line, sizeof(line), "%s   +$%d", info.description, info.reward);
    centered(line, at.y + 64.0f, 0.5f, glm::vec4(kGreen.r, kGreen.g, kGreen.b, alpha));
}

void BrokenBonesGame::drawMenu() {
    core::UIRenderer& ui = app_.uiRenderer();
    const glm::vec2 screen(static_cast<float>(app_.window().width()), static_cast<float>(app_.window().height()));
    auto centered = [&](const std::string& text, float y, float scale, glm::vec4 color) {
        glm::vec2 size = ui.measureText(text, scale);
        ui.drawText(text, glm::vec2((screen.x - size.x) * 0.5f, y), scale, color);
    };
    char line[160];

    ui.drawRect(glm::vec2(0.0f), screen, glm::vec4(0.0f, 0.0f, 0.02f, menu_ == Menu::Title ? 0.45f : 0.6f));

    float listTop = screen.y * 0.42f;
    if (menu_ == Menu::Title) {
        centered("BROKEN BONES", screen.y * 0.14f, 2.8f, kGold);
        centered("Jump. Fall. Break everything. Get paid.", screen.y * 0.14f + 92.0f, 0.75f, kWhite);
        std::snprintf(line, sizeof(line), "LEVEL %d     $%d     %d BONES     %d/%zu ACHIEVEMENTS%s", progress_.level,
                      progress_.cash, skeletonBoneCount(progress_), achievementsUnlocked(progress_), kAchievementCount,
                      progress_.rebirths > 0 ? ("     REBIRTH " + std::to_string(progress_.rebirths)).c_str() : "");
        centered(line, screen.y * 0.14f + 128.0f, 0.6f, kDim);
    } else if (menu_ == Menu::Pause) {
        centered("PAUSED", screen.y * 0.2f, 1.8f, kWhite);
    } else if (menu_ == Menu::Settings) {
        centered("SETTINGS", screen.y * 0.18f, 1.5f, kWhite);
        listTop = screen.y * 0.32f;
    } else if (menu_ == Menu::Rebirth) {
        centered("REBIRTH?", screen.y * 0.18f, 1.8f, kGold);
        std::snprintf(line, sizeof(line), "You go back to level %d with no gear or regular upgrades.",
                      1 + 3 * progress_.count(ShopItem::HeadStart));
        centered(line, screen.y * 0.18f + 70.0f, 0.7f, kWhite);
        centered("You keep your maps, rebirth upgrades, achievements and stats.", screen.y * 0.18f + 100.0f, 0.7f, kWhite);
        Progress preview = progress_;
        preview.rebirths += 1;
        std::snprintf(line, sizeof(line), "Every run pays x%.1f forever (now x%.1f)", rebirthMultiplier(preview),
                      rebirthMultiplier(progress_));
        centered(line, screen.y * 0.18f + 136.0f, 0.8f, kGreen);
        std::snprintf(line, sizeof(line), "Unlocks new upgrades on the REBIRTH shop page.  Next rebirth at level %d.",
                      rebirthLevelRequired(preview));
        centered(line, screen.y * 0.18f + 168.0f, 0.65f, kGold);
        listTop = screen.y * 0.5f;
    } else if (menu_ == Menu::Achievements) {
        std::snprintf(line, sizeof(line), "ACHIEVEMENTS  %d / %zu", achievementsUnlocked(progress_), kAchievementCount);
        centered(line, 40.0f, 1.2f, kGold);
        constexpr size_t kRows = (kAchievementCount + 2) / 3;
        constexpr float kCellH = 46.0f;
        const float kCellW = std::min(420.0f, (screen.x - 64.0f) / 3.0f);
        float left = (screen.x - 3.0f * kCellW - 32.0f) * 0.5f;
        for (size_t i = 0; i < kAchievementCount; ++i) {
            auto achievement = static_cast<Achievement>(i);
            const AchievementInfo& info = achievementInfo(achievement);
            bool done = hasAchievement(progress_, achievement);
            auto column = static_cast<float>(i / kRows);
            glm::vec2 at(left + column * (kCellW + 16.0f),
                         100.0f + static_cast<float>(i % kRows) * (kCellH + 4.0f));
            ui.drawRect(at, glm::vec2(kCellW, kCellH),
                        done ? glm::vec4(0.35f, 0.25f, 0.02f, 0.75f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.06f));
            ui.drawText(info.name, at + glm::vec2(12.0f, 4.0f), 0.6f, done ? kGold : kDim);
            ui.drawText(info.description, at + glm::vec2(12.0f, 25.0f), 0.45f, done ? kWhite : kDim);
            std::snprintf(line, sizeof(line), done ? "DONE" : "$%d", info.reward);
            glm::vec2 tagSize = ui.measureText(line, 0.55f);
            ui.drawText(line, glm::vec2(at.x + kCellW - tagSize.x - 12.0f, at.y + 12.0f), 0.55f, done ? kGreen : kDim);
        }
        listTop = 100.0f + kRows * (kCellH + 4.0f) + 20.0f;
    } else if (menu_ == Menu::Stats) {
        centered("STATS", 40.0f, 1.4f, kWhite);
        const LifetimeStats& stats = progress_.stats;
        const PersonalBests& bests = progress_.bests;
        std::vector<std::pair<std::string, std::string>> lifetime = {
            {"Runs", std::to_string(progress_.runs)},
            {"Bones broken", std::to_string(progress_.totalBones)},
            {"Cash earned", "$" + std::to_string(stats.cashEarned)},
            {"Time played", formatPlayTime(stats.playSeconds)},
            {"Distance fallen", std::to_string(static_cast<long long>(stats.metresFallen)) + " m"},
            {"Splashdowns", std::to_string(stats.splashdowns)},
            {"Bombs set off", std::to_string(stats.bombs)},
            {"Rockets fired", std::to_string(stats.rockets)},
            {"Super boosts", std::to_string(stats.superBoosts)},
            {"Best level", std::to_string(progress_.bestLevel)},
            {"Rebirths", std::to_string(progress_.rebirths)},
            {"Injuries", std::to_string(stats.injuries)},
        };
        std::vector<std::pair<std::string, std::string>> best = {
            {"Most bones", std::to_string(bests.bones)},
            {"Biggest combo", "x" + std::to_string(bests.combo)},
            {"Biggest payday", "$" + std::to_string(bests.payout)},
            {"Longest fall", std::to_string(static_cast<int>(bests.fall)) + " m"},
            {"Top speed", std::to_string(static_cast<int>(bests.speed)) + " m/s"},
            {"Hardest hit", std::to_string(static_cast<int>(bests.hit)) + " m/s"},
            {"Skeleton", std::to_string(skeletonBoneCount(progress_)) + " bones"},
            {"Achievements", std::to_string(achievementsUnlocked(progress_)) + "/" + std::to_string(kAchievementCount)},
        };
        auto column = [&](const char* title, const std::vector<std::pair<std::string, std::string>>& rows, float x) {
            ui.drawRect(glm::vec2(x, 100.0f), glm::vec2(400.0f, 50.0f + 30.0f * static_cast<float>(rows.size())), kPanel);
            ui.drawText(title, glm::vec2(x + 16.0f, 110.0f), 0.7f, kGold);
            for (size_t i = 0; i < rows.size(); ++i) {
                float y = 146.0f + 30.0f * static_cast<float>(i);
                ui.drawText(rows[i].first, glm::vec2(x + 16.0f, y), 0.6f, kDim);
                glm::vec2 valueSize = ui.measureText(rows[i].second, 0.6f);
                ui.drawText(rows[i].second, glm::vec2(x + 384.0f - valueSize.x, y), 0.6f, kWhite);
            }
        };
        column("LIFETIME", lifetime, screen.x * 0.5f - 416.0f);
        column("PERSONAL BESTS", best, screen.x * 0.5f + 16.0f);
        listTop = 100.0f + 50.0f + 30.0f * static_cast<float>(lifetime.size()) + 30.0f;
    } else if (menu_ == Menu::Leaderboard) {
        centered("LEADERBOARD", 36.0f, 1.4f, kGold);
        auto stat = static_cast<BoardStat>(boardTab_);
        float tabsWidth = 0.0f;
        for (size_t t = 0; t < kBoardStatCount; ++t) {
            tabsWidth += ui.measureText(boardStatName(static_cast<BoardStat>(t)), 0.6f).x + 28.0f;
        }
        float tabX = (screen.x - tabsWidth) * 0.5f;
        for (size_t t = 0; t < kBoardStatCount; ++t) {
            const char* name = boardStatName(static_cast<BoardStat>(t));
            glm::vec2 tabSize = ui.measureText(name, 0.6f) + glm::vec2(20.0f, 10.0f);
            bool current = t == boardTab_;
            ui.drawRect(glm::vec2(tabX, 96.0f), tabSize,
                        current ? glm::vec4(1.0f, 0.8f, 0.2f, 0.25f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.06f));
            ui.drawText(name, glm::vec2(tabX + 10.0f, 101.0f), 0.6f, current ? kGold : kDim);
            tabX += tabSize.x + 8.0f;
        }
        constexpr float kRowH = 36.0f;
        const float width = std::min(760.0f, screen.x - 48.0f);
        float left = (screen.x - width) * 0.5f;
        std::vector<BoardRow> rows = boardStandings(progress_.leaderboard, stat);
        for (size_t i = 0; i < rows.size(); ++i) {
            const BoardRow& row = rows[i];
            float y = 146.0f + kRowH * static_cast<float>(i);
            ui.drawRect(glm::vec2(left, y), glm::vec2(width, kRowH - 4.0f),
                        row.player ? glm::vec4(0.35f, 0.25f, 0.02f, 0.75f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.06f));
            glm::vec4 color = row.player ? kGold : kWhite;
            std::snprintf(line, sizeof(line), "#%zu", i + 1);
            ui.drawText(line, glm::vec2(left + 14.0f, y + 6.0f), 0.65f, i < 3 ? kGold : kDim);
            ui.drawText(row.name, glm::vec2(left + 70.0f, y + 6.0f), 0.65f, color);
            if (row.player) {
                std::snprintf(line, sizeof(line), "L%d %s  %d inj.", row.record.level,
                              mapThemeInfo(row.record.map).name, row.record.injuries);
                ui.drawText(line, glm::vec2(left + width * 0.5f, y + 9.0f), 0.5f, kDim);
            }
            std::string value = formatBoardValue(stat, row.value);
            glm::vec2 valueSize = ui.measureText(value, 0.65f);
            ui.drawText(value, glm::vec2(left + width - valueSize.x - 14.0f, y + 6.0f), 0.65f, color);
        }
        listTop = 146.0f + kRowH * static_cast<float>(rows.size()) + 16.0f;
    }

    MenuItems items = menuItems(menu_);
    for (int i = 0; i < items.count; ++i) {
        std::string label = items.items[i];
        const GameSettings& settings = progress_.settings;
        if (menu_ == Menu::Settings) {
            auto onOff = [](bool on) { return std::string(on ? "ON" : "OFF"); };
            auto percent = [](float v) { return "   < " + std::to_string(static_cast<int>(std::round(v * 100.0f))) + "% >"; };
            if (label == "VOLUME") label += percent(settings.volume);
            if (label == "MUSIC") label += percent(settings.music);
            if (label == "MOUSE SENSITIVITY") {
                std::snprintf(line, sizeof(line), "   < %.1fx >", settings.sensitivity);
                label += line;
            }
            if (label == "SLOW MOTION") label += "   " + onOff(settings.slowMotion);
            if (label == "CAMERA SHAKE") label += "   " + onOff(settings.cameraShake);
            if (label == "TIPS") label += "   " + onOff(settings.tips);
        }
        bool selected = i == menuCursor_;
        bool disabled = label == "REBIRTH" && !canRebirth(progress_);
        if (disabled) label += "  (LEVEL " + std::to_string(rebirthLevelRequired(progress_)) + ")";
        float y = listTop + 46.0f * static_cast<float>(i);
        glm::vec2 size = ui.measureText(label, 0.85f);
        // The title list sits left of the avatar, which stands in the middle.
        const float centerX = menu_ == Menu::Title ? std::max(screen.x * 0.2f, size.x * 0.5f + 40.0f) : screen.x * 0.5f;
        if (selected) {
            ui.drawRect(glm::vec2(centerX - size.x * 0.5f - 24.0f, y - 6.0f), size + glm::vec2(48.0f, 12.0f),
                        glm::vec4(1.0f, 0.8f, 0.2f, 0.22f));
        }
        ui.drawText(label, glm::vec2(centerX - size.x * 0.5f, y),
                    0.85f, disabled ? glm::vec4(0.5f, 0.5f, 0.55f, 0.9f) : (selected ? kGold : kWhite));
    }

    const char* footer = menu_ == Menu::Settings      ? "[UP/DOWN] choose    [LEFT/RIGHT] change    [ESC] back"
                         : menu_ == Menu::Leaderboard ? "[LEFT/RIGHT] switch board    [ESC] back"
                         : menu_ == Menu::Title ? "[UP/DOWN] choose    [ENTER] select"
                                                : "[UP/DOWN] choose    [ENTER] select    [ESC] back";
    centered(footer, screen.y - 44.0f, 0.55f, kDim);
}

} // namespace engine::brokenbones
