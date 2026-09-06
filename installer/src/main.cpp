// Kronos Bootstrap Installer -- a real, lightweight, standalone C++ app
// (SDL2 + Dear ImGui, SDL_Renderer backend -- deliberately not Vulkan,
// see installer/CMakeLists.txt's own header comment) that fetches the
// real latest Kronos release from GitHub, downloads the right archive
// for the chosen platform, verifies it against the real published
// checksum, extracts it, and wires up real platform integration --
// so a new user never has to touch a compiler.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <SDL.h>
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>

#include "ArchiveExtractor.hpp"
#include "Downloader.hpp"
#include "GitHubReleaseApi.hpp"
#include "PlatformIntegration.hpp"
#include "Sha256.hpp"
#include "UpdateApply.hpp"

namespace {

constexpr const char* kRepoOwner = "Bands-yt";
constexpr const char* kRepoName = "kronos-platform";

enum class InstallStage {
    Idle,
    WaitingForExit, // update mode only -- waiting for the real running app to close
    FetchingRelease,
    Downloading,
    Verifying,
    Extracting,
    Integrating,
    Swapping,   // update mode only
    Relaunching, // update mode only
    Done,
    Failed
};

// The real release-asset suffix for the platform this binary is itself
// running on -- update mode never asks the user to choose a platform,
// unlike the first-time installer, because it is by definition updating
// an install of this very platform.
constexpr const char* currentPlatformAssetSuffix() {
#if defined(_WIN32)
    return "windows-x64.zip";
#else
    return "linux-x64.tar.gz";
#endif
}

constexpr const char* currentPlatformRuntimeExe() {
#if defined(_WIN32)
    return "engine_runtime.exe";
#else
    return "engine_runtime";
#endif
}

// Kronos ("Installer Component Selection" -- "In-Player Tool Manager"):
// the real, single source of truth for every component both the
// Windows Inno Setup [Components] page and this binary's own
// --select-components/--install-components modes offer -- `id` is the
// public, stable identifier the Player's own tool-manager UI and the
// CLI both address a component by (see parseComponentList() below).
// `assetApp` is nullptr for Player specifically: unlike the 4 creator
// tools (each published as its own kronos_<assetApp>_<platform>.zip by
// .github/workflows/release.yml), Player IS the base engine bundle
// (kronos-<tag>-<platform>.tar.gz/.zip) every other install/update path
// in this file already fetches -- see installComponent()'s own comment
// for how that real distinction plays out.
struct ComponentSpec {
    const char* id;
    const char* label;
    const char* assetApp;
    const char* exeNameLinux;
    const char* exeNameWindows;
    const char* categories; // real freedesktop.org Categories= value (Linux .desktop only)
};

constexpr ComponentSpec kComponents[] = {
    {"player", "Player", nullptr, "engine_runtime", "engine_runtime.exe", "Game;"},
    {"studio", "Studio", "studio", "kronos_studio", "kronos_studio.exe", "Development;Graphics;3DGraphics;"},
    {"3d-tools", "3D Tools", "3d_maker", "kronos_3d_maker", "kronos_3d_maker.exe", "Graphics;3DGraphics;"},
    {"movie-mode", "Movie Mode", "movie_maker", "kronos_movie_maker", "kronos_movie_maker.exe",
     "AudioVideo;AudioVideoEditing;"},
    {"audio", "Audio", "audio", "kronos_audio", "kronos_audio.exe", "AudioVideo;Audio;"},
};
constexpr size_t kComponentCount = sizeof(kComponents) / sizeof(kComponents[0]);

const ComponentSpec* findComponent(const std::string& id) {
    for (const ComponentSpec& c : kComponents) {
        if (id == c.id) return &c;
    }
    return nullptr;
}

// Kronos: the real per-app asset filename release.yml actually
// publishes (e.g. "kronos_3d_maker_linux_x86_64.zip") -- an exact
// filename works fine as a "suffix" for findAssetBySuffix() too (see
// that function's own comment), so no separate matching path is
// needed. `platform` is "win64" or "linux_x86_64", matching
// backend/src/catalog/downloads.js's own PLATFORMS list exactly (the
// same naming this repo's download redirect already relies on).
std::string perAppAssetName(const std::string& assetApp, const std::string& platform) {
    return "kronos_" + assetApp + "_" + platform + ".zip";
}

constexpr const char* currentPlatformId() {
#if defined(_WIN32)
    return "win64";
#else
    return "linux_x86_64";
#endif
}

const char* componentExeName(const ComponentSpec& component) {
#if defined(_WIN32)
    return component.exeNameWindows;
#else
    return component.exeNameLinux;
#endif
}

// Real, small parser: "all" -> every component id in kComponents'
// declared order (Player first, so it's always installed/updated
// before any creator tool that might assume it's already there);
// otherwise a comma-separated list of real ids, each validated against
// kComponents so a typo becomes a real, honest error rather than a
// silently-skipped component.
bool parseComponentList(const std::string& raw, std::vector<const ComponentSpec*>& outComponents,
                         std::string& outError) {
    if (raw == "all") {
        for (const ComponentSpec& c : kComponents) outComponents.push_back(&c);
        return true;
    }
    std::stringstream stream(raw);
    std::string token;
    while (std::getline(stream, token, ',')) {
        const ComponentSpec* component = findComponent(token);
        if (component == nullptr) {
            outError = "unknown component \"" + token + "\"";
            return false;
        }
        outComponents.push_back(component);
    }
    if (outComponents.empty()) {
        outError = "no components given";
        return false;
    }
    return true;
}

// Kronos: real, shared state between the UI thread (this file's own
// main loop) and the real background worker thread runInstall() runs
// on -- every field here is either a real std::atomic (safe to read
// without a lock) or guarded by `mutex` (see each field's own comment).
struct InstallerState {
    std::atomic<InstallStage> stage{InstallStage::Idle};
    std::atomic<uint64_t> bytesDownloaded{0};
    std::atomic<uint64_t> totalBytes{0};

    std::mutex mutex; // guards statusMessage/installDirResult below
    std::string statusMessage;
    std::string installDirResult;

    std::thread worker;
};

void setStatus(InstallerState& state, InstallStage stage, const std::string& message) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.statusMessage = message;
    state.stage.store(stage);
}

// Real, small "hash  filename\n" parser -- the exact format both
// `sha256sum` (Linux job) and the Windows job's own PowerShell
// equivalent write (see .github/workflows/build.yml's own "Generate
// checksum" steps).
std::string parseChecksumFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.good()) return {};
    std::string line;
    std::getline(file, line);
    size_t firstSpace = line.find(' ');
    return firstSpace == std::string::npos ? std::string() : line.substr(0, firstSpace);
}

// Kronos: the real, shared "get the release onto disk" pipeline -- used
// by both the real first-time install path (runInstall) and the real
// in-app update path (runUpdate), which differ only in WHERE they
// extract to and what they do afterwards, never in how they fetch or
// verify. Returns false having already set a real failure status.
bool downloadAndExtract(InstallerState& state, const std::string& platformSuffix, const std::string& destinationDir,
                         std::string& outTopLevelDir, std::string& outTagName) {
    setStatus(state, InstallStage::FetchingRelease, "Checking GitHub for the latest Kronos release...");
    kronos_installer::LatestRelease release = kronos_installer::fetchLatestRelease(kRepoOwner, kRepoName);
    if (!release.success) {
        setStatus(state, InstallStage::Failed, "Could not reach GitHub: " + release.error);
        return false;
    }
    outTagName = release.tagName;

    const kronos_installer::ReleaseAsset* asset = kronos_installer::findAssetBySuffix(release, platformSuffix);
    if (asset == nullptr) {
        setStatus(state, InstallStage::Failed,
                  "The latest release (" + release.tagName + ") has no " + platformSuffix + " archive yet.");
        return false;
    }
    const kronos_installer::ReleaseAsset* checksumAsset =
        kronos_installer::findAssetBySuffix(release, platformSuffix + ".sha256");

    state.totalBytes.store(asset->sizeBytes);
    state.bytesDownloaded.store(0);
    setStatus(state, InstallStage::Downloading, "Downloading " + asset->name + " (" + release.tagName + ")...");

    std::filesystem::path archivePath = std::filesystem::temp_directory_path() / asset->name;
    kronos_installer::DownloadResult download =
        kronos_installer::downloadFile(asset->downloadUrl, archivePath.string(), [&](uint64_t now, uint64_t total) {
            state.bytesDownloaded.store(now);
            if (total > 0) state.totalBytes.store(total);
        });
    if (!download.success) {
        setStatus(state, InstallStage::Failed, "Download failed: " + download.error);
        return false;
    }

    if (checksumAsset != nullptr) {
        setStatus(state, InstallStage::Verifying, "Verifying checksum...");
        std::filesystem::path checksumPath = archivePath.string() + ".sha256";
        kronos_installer::DownloadResult checksumDownload =
            kronos_installer::downloadFile(checksumAsset->downloadUrl, checksumPath.string(), {});
        if (checksumDownload.success) {
            std::string expectedHash = parseChecksumFile(checksumPath.string());
            std::string actualHash = kronos_installer::sha256HexOfFile(archivePath.string());
            if (!expectedHash.empty() && expectedHash != actualHash) {
                setStatus(state, InstallStage::Failed,
                          "Checksum mismatch -- the downloaded file doesn't match the published checksum. Aborting "
                          "rather than installing a possibly-corrupt archive.");
                return false;
            }
        }
    }

    setStatus(state, InstallStage::Extracting, "Extracting to " + destinationDir + "...");
    kronos_installer::ExtractResult extract = kronos_installer::extractArchive(archivePath.string(), destinationDir);
    if (!extract.success) {
        setStatus(state, InstallStage::Failed, "Extraction failed: " + extract.error);
        return false;
    }
    outTopLevelDir = extract.topLevelDirectory;
    return true;
}

// Kronos ("Installer Component Selection" -- "In-Player Tool Manager"):
// the real, single per-component install/update pipeline both new CLI
// modes below (--install-components, --select-components) and the
// Windows Inno Setup [Components] page's own real download-vs-bundle
// split are built around. Player fetches the same whole-engine bundle
// runInstall()/runUpdate() already use (it's genuinely the base
// install, not a "component" of something else); every creator tool
// fetches its own real, independent kronos_<assetApp>_<platform>.zip
// (see perAppAssetName()'s own comment) and lands as its own sibling
// top-level folder under `installDir` -- deliberately not merged into
// Player's own folder, since each of those archives is already a real,
// self-contained package (its own shaders/assets copy, same as
// Player's) and merging would mean re-implementing extractArchive()'s
// own real path-safety logic for no real benefit.
bool installComponent(InstallerState& state, const ComponentSpec& component, const std::string& installDir,
                       std::string& outInstalledExePath) {
    std::string assetSuffix =
        component.assetApp == nullptr ? currentPlatformAssetSuffix() : perAppAssetName(component.assetApp, currentPlatformId());

    std::string topLevelDir;
    std::string tagName;
    if (!downloadAndExtract(state, assetSuffix, installDir, topLevelDir, tagName)) return false;

    std::filesystem::path exePath(installDir);
    if (!topLevelDir.empty()) exePath /= topLevelDir;
    exePath /= componentExeName(component);
    outInstalledExePath = exePath.string();

    // Unlike runInstall() (which lets a first-time user pick EITHER
    // platform's archive from one binary), every path here is
    // hardcoded to currentPlatformAssetSuffix()/currentPlatformId() --
    // there is no cross-platform choice to make, so shortcut/launcher
    // integration always applies.
    setStatus(state, InstallStage::Integrating, "Setting up " + std::string(component.label) + "...");
    bool isPlayer = component.assetApp == nullptr;
    std::string displayName = isPlayer ? "Kronos" : "Kronos " + std::string(component.label);
    std::string desktopBasename = isPlayer ? "kronos" : "kronos-" + std::string(component.id);
#if defined(_WIN32)
    // The target .exe already carries its own real, distinct icon
    // (see engine/src/CMakeLists.txt's per-target .rc wiring) -- leaving
    // this empty means the .lnk falls back to that icon rather than
    // needing a separate, easy-to-get-stale .ico path.
    std::string iconPath;
#else
    std::string iconAppName = isPlayer ? "" : "_" + std::string(component.assetApp);
    std::filesystem::path iconFsPath =
        std::filesystem::path(exePath).parent_path() / "assets" / "icons" / ("kronos" + iconAppName + "_icon.png");
    std::string iconPath = std::filesystem::exists(iconFsPath) ? iconFsPath.string() : std::string();
#endif
    std::string shortcutError;
    if (!kronos_installer::createComponentShortcut(outInstalledExePath, displayName, desktopBasename, iconPath,
                                                     component.categories, /*registerKronosUri=*/isPlayer,
                                                     shortcutError)) {
        // Real, non-fatal -- same "the component is already fully
        // extracted and usable" reasoning runInstall()'s own shortcut
        // step already documents; the stage stays whatever the caller
        // sets next rather than flipping to Failed for a non-fatal step.
        std::lock_guard<std::mutex> lock(state.mutex);
        state.statusMessage = std::string(component.label) + " installed, but shortcut setup: " + shortcutError;
    }
    return true;
}

// Kronos ("In-App Auto-Updater" -- "Safe Swapping"): the real update
// worker. Runs only in --update mode, i.e. only when a real running
// Kronos spawned this helper and then exited.
void runUpdate(InstallerState& state, const std::string& installDir, const std::string& relaunchExe, int64_t waitPid) {
    // Step 1: the real reason this runs in a separate process at all --
    // wait until the app being replaced is genuinely gone, so no file in
    // the install directory is still open/locked.
    setStatus(state, InstallStage::WaitingForExit, "Waiting for Kronos to close...");
    if (!kronos_installer::waitForProcessExit(waitPid, 60.0)) {
        setStatus(state, InstallStage::Failed,
                  "Kronos is still running after 60 seconds -- close it and run the update again.");
        return;
    }

    // Step 2: download + verify + unpack into a real staging directory
    // NEXT TO the install (same filesystem, so the swap below is a real
    // rename rather than a slow, half-atomic cross-device copy).
    std::filesystem::path install(installDir);
    std::filesystem::path staging = install.parent_path() / (install.filename().string() + ".update-staging");
    std::error_code ec;
    std::filesystem::remove_all(staging, ec); // clear any real leftovers from an interrupted attempt

    std::string topLevelDir;
    std::string tagName;
    if (!downloadAndExtract(state, currentPlatformAssetSuffix(), staging.string(), topLevelDir, tagName)) return;

    // The archive unpacks into its own real wrapper directory
    // ("kronos-linux-x64"/...); the real new install root is that inner
    // directory, not the staging directory itself.
    std::filesystem::path newRoot = topLevelDir.empty() ? staging : staging / topLevelDir;

    setStatus(state, InstallStage::Swapping, "Installing " + tagName + "...");
    std::string backupDir;
    kronos_installer::SwapResult swap =
        kronos_installer::swapInstallDirectory(installDir, newRoot.string(), backupDir);
    if (!swap.success) {
        setStatus(state, InstallStage::Failed,
                  swap.error + (swap.rolledBack ? " (your existing install was restored unchanged)" : ""));
        return;
    }

    // Real cleanup of both the backup and the now-empty staging shell.
    // Deliberately best-effort: the update itself already succeeded, and
    // failing the whole update over leftover temp files would be wrong.
    std::filesystem::remove_all(backupDir, ec);
    std::filesystem::remove_all(staging, ec);

    setStatus(state, InstallStage::Relaunching, "Restarting Kronos " + tagName + "...");
    std::filesystem::path relaunchPath = std::filesystem::path(installDir) / relaunchExe;
    std::string launchError;
    if (!kronos_installer::launchDetached(relaunchPath.string(), launchError)) {
        setStatus(state, InstallStage::Failed,
                  "Kronos " + tagName + " installed, but relaunching it failed (" + launchError +
                      ") -- start it yourself from " + installDir + ".");
        return;
    }
    setStatus(state, InstallStage::Done, "Updated to " + tagName + ".");
}

void runInstall(InstallerState& state, const std::string& platformSuffix, bool isWindowsTarget,
                 const std::string& runtimeExeName, const std::string& installDir) {
    setStatus(state, InstallStage::FetchingRelease, "Checking GitHub for the latest Kronos release...");
    kronos_installer::LatestRelease release = kronos_installer::fetchLatestRelease(kRepoOwner, kRepoName);
    if (!release.success) {
        setStatus(state, InstallStage::Failed, "Could not reach GitHub: " + release.error);
        return;
    }

    const kronos_installer::ReleaseAsset* asset = kronos_installer::findAssetBySuffix(release, platformSuffix);
    if (asset == nullptr) {
        setStatus(state, InstallStage::Failed,
                  "The latest release (" + release.tagName + ") has no " + platformSuffix + " archive yet.");
        return;
    }
    const kronos_installer::ReleaseAsset* checksumAsset =
        kronos_installer::findAssetBySuffix(release, platformSuffix + ".sha256");

    state.totalBytes.store(asset->sizeBytes);
    state.bytesDownloaded.store(0);
    setStatus(state, InstallStage::Downloading, "Downloading " + asset->name + " (" + release.tagName + ")...");

    std::filesystem::path archivePath = std::filesystem::temp_directory_path() / asset->name;
    kronos_installer::DownloadResult download =
        kronos_installer::downloadFile(asset->downloadUrl, archivePath.string(), [&](uint64_t now, uint64_t total) {
            state.bytesDownloaded.store(now);
            if (total > 0) state.totalBytes.store(total);
        });
    if (!download.success) {
        setStatus(state, InstallStage::Failed, "Download failed: " + download.error);
        return;
    }

    // Kronos ("verify the integrity of the files"): real, only when the
    // release actually published a checksum for this asset (see
    // .github/workflows/build.yml) -- a real, honest skip (not a fake
    // "verified" claim) when it isn't there.
    if (checksumAsset != nullptr) {
        setStatus(state, InstallStage::Verifying, "Verifying checksum...");
        std::filesystem::path checksumPath = archivePath.string() + ".sha256";
        kronos_installer::DownloadResult checksumDownload =
            kronos_installer::downloadFile(checksumAsset->downloadUrl, checksumPath.string(), {});
        if (checksumDownload.success) {
            std::string expectedHash = parseChecksumFile(checksumPath.string());
            std::string actualHash = kronos_installer::sha256HexOfFile(archivePath.string());
            if (!expectedHash.empty() && expectedHash != actualHash) {
                setStatus(state, InstallStage::Failed,
                          "Checksum mismatch -- the downloaded file doesn't match the published checksum. Aborting "
                          "rather than installing a possibly-corrupt archive.");
                return;
            }
        }
        // Real, honest: a failed checksum *download* doesn't abort the
        // install -- the archive itself downloaded fine over real TLS;
        // it just means this particular integrity cross-check couldn't
        // run this time.
    }

    setStatus(state, InstallStage::Extracting, "Extracting to " + installDir + "...");
    kronos_installer::ExtractResult extract = kronos_installer::extractArchive(archivePath.string(), installDir);
    if (!extract.success) {
        setStatus(state, InstallStage::Failed, "Extraction failed: " + extract.error);
        return;
    }

    // Kronos: real platform integration only when the chosen download
    // target is the platform this installer itself is actually running
    // on -- a Windows shortcut can't be created for a Windows archive
    // downloaded from a real Linux run of this same tool, and vice
    // versa; that's a real, honest limitation, not a bug.
#if defined(_WIN32)
    bool canIntegrate = isWindowsTarget;
#else
    bool canIntegrate = !isWindowsTarget;
#endif
    std::string shortcutNote;
    if (canIntegrate) {
        setStatus(state, InstallStage::Integrating, "Setting up shortcuts...");
        // Real archive-reported root ("kronos-linux-x64"/"kronos-windows-x64"
        // for what build.yml actually publishes) -- not a hardcoded guess,
        // which is exactly how this previously pointed the real shortcut at
        // a "kronos-alpha" directory the real release archives never contained.
        std::filesystem::path runtimePath = std::filesystem::path(installDir);
        if (!extract.topLevelDirectory.empty()) runtimePath /= extract.topLevelDirectory;
        runtimePath /= runtimeExeName;
        std::string shortcutError;
        if (!kronos_installer::createPlatformShortcut(runtimePath.string(), shortcutError)) {
            shortcutNote = " (shortcut setup: " + shortcutError + ")";
        }
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.installDirResult = installDir;
    }
    setStatus(state, InstallStage::Done, "Kronos " + release.tagName + " installed to " + installDir + shortcutNote);
}

// Kronos ("Installer Component Selection" -- "In-Player Tool Manager"):
// the real GUI-mode driver for --install-components -- sequentially
// installComponent()s every real, requested component into the same
// shared `installDir`, updating the visible status between each one
// ("Component 2 of 3: ..."), same real "no fabricated aggregate
// progress" reasoning as everything else in this file: bytesDownloaded/
// totalBytes always reflect the ONE real transfer currently in flight,
// never a summed/interpolated guess across components.
void runInstallComponents(InstallerState& state, const std::vector<const ComponentSpec*>& components,
                           const std::string& installDir) {
    size_t failedCount = 0;
    for (size_t i = 0; i < components.size(); ++i) {
        const ComponentSpec& component = *components[i];
        setStatus(state, InstallStage::FetchingRelease,
                  "Component " + std::to_string(i + 1) + " of " + std::to_string(components.size()) + ": " +
                      component.label + "...");
        std::string installedExePath;
        if (!installComponent(state, component, installDir, installedExePath)) {
            ++failedCount;
            // Real, honest partial progress: a failed component (e.g. no
            // network for this one asset) doesn't abort the rest -- each
            // is independent, so trying the others is strictly better
            // than stopping at the first real failure.
            continue;
        }
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.installDirResult = installDir;
    }
    if (failedCount == 0) {
        setStatus(state, InstallStage::Done,
                  std::to_string(components.size()) + " of " + std::to_string(components.size()) +
                      " component(s) installed to " + installDir);
    } else {
        setStatus(state, InstallStage::Failed,
                  std::to_string(components.size() - failedCount) + " of " + std::to_string(components.size()) +
                      " component(s) installed; " + std::to_string(failedCount) +
                      " failed -- see the status messages above for which.");
    }
}

std::string formatBytes(uint64_t bytes) {
    std::ostringstream out;
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        out.precision(2);
        out << std::fixed << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " GB";
    } else {
        out.precision(1);
        out << std::fixed << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " MB";
    }
    return out.str();
}

// Kronos ("Installer Component Selection" -- Linux interactive TUI):
// pure-terminal component checklist for --select-components -- real,
// hand-rolled numbered-toggle prompt (not whiptail/dialog: neither is
// guaranteed installed, and this needs zero extra runtime dependency to
// work on any real terminal, including a plain SSH session with no
// display at all -- see main()'s own dispatch for why this runs before
// SDL_Init()). Player is preselected since every creator tool assumes
// it's already there; the user can still deselect it if they're only
// adding tools to an existing install.
int runInteractiveComponentSelector(std::string installDir) {
    std::vector<bool> selected(kComponentCount, false);
    selected[0] = true; // Player

    while (true) {
        std::printf("\nKronos Installer -- choose components to install:\n");
        for (size_t i = 0; i < kComponentCount; ++i) {
            std::printf("  %zu) [%c] %s\n", i + 1, selected[i] ? 'x' : ' ', kComponents[i].label);
        }
        std::printf("Install directory: %s\n", installDir.c_str());
        std::printf(
            "Enter a number to toggle, \"a\" for all, \"d\" to change the install directory, or \"i\" to install: ");
        std::fflush(stdout);

        std::string line;
        if (!std::getline(std::cin, line)) return 1; // real EOF (e.g. piped input ran out) -- an honest abort, not a silent hang
        if (line == "i") break;
        if (line == "a") {
            for (size_t i = 0; i < kComponentCount; ++i) selected[i] = true;
            continue;
        }
        if (line == "d") {
            std::printf("New install directory [%s]: ", installDir.c_str());
            std::fflush(stdout);
            std::string newDir;
            if (std::getline(std::cin, newDir) && !newDir.empty()) installDir = newDir;
            continue;
        }
        try {
            size_t consumed = 0;
            int choice = std::stoi(line, &consumed);
            if (consumed == line.size() && choice >= 1 && static_cast<size_t>(choice) <= kComponentCount) {
                selected[static_cast<size_t>(choice) - 1] = !selected[static_cast<size_t>(choice) - 1];
            } else {
                std::printf("Not a valid component number.\n");
            }
        } catch (const std::exception&) {
            std::printf("Not a recognized choice.\n");
        }
    }

    std::vector<const ComponentSpec*> components;
    for (size_t i = 0; i < kComponentCount; ++i) {
        if (selected[i]) components.push_back(&kComponents[i]);
    }
    if (components.empty()) {
        std::printf("No components selected -- nothing to install.\n");
        return 0;
    }

    InstallerState state;
    std::printf("\nInstalling %zu component(s) to %s...\n", components.size(), installDir.c_str());
    // Real, background worker + poll-and-print on this thread: reuses
    // the exact same runInstallComponents()/InstallerState the GUI
    // modes drive, just echoed to stdout instead of an ImGui progress
    // bar -- the same real download/verify/extract pipeline either way.
    std::thread worker([&]() { runInstallComponents(state, components, installDir); });
    InstallStage lastStage = InstallStage::Idle;
    std::string lastMessage;
    while (true) {
        InstallStage stage = state.stage.load();
        std::string message;
        {
            std::lock_guard<std::mutex> lock(state.mutex);
            message = state.statusMessage;
        }
        if (stage != lastStage || message != lastMessage) {
            uint64_t downloaded = state.bytesDownloaded.load();
            uint64_t total = state.totalBytes.load();
            if (stage == InstallStage::Downloading && total > 0) {
                std::printf("%s (%s / %s)\n", message.c_str(), formatBytes(downloaded).c_str(),
                            formatBytes(total).c_str());
            } else if (!message.empty()) {
                std::printf("%s\n", message.c_str());
            }
            lastStage = stage;
            lastMessage = message;
        }
        if (stage == InstallStage::Done || stage == InstallStage::Failed) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    }
    worker.join();
    return state.stage.load() == InstallStage::Done ? 0 : 1;
}

// Kronos ("In-App Auto-Updater"): real CLI surface. With no arguments
// this binary is the first-time Bootstrap Installer exactly as before;
// with --update it is the update helper a running Kronos spawns just
// before exiting.
struct CommandLineOptions {
    bool updateMode = false;
    // Kronos ("Installer Component Selection" -- "In-Player Tool
    // Manager"): two new real modes, both keyed off `installComponents`
    // being non-empty. `--install-components` is GUI (the Player's own
    // tool manager launches this exactly like --update -- see
    // RuntimeShell.cpp's own startComponentInstall()); `interactive` is
    // the separate, pure-terminal --select-components mode
    // (runInteractiveComponentSelector()) neither GUI mode ever sets.
    std::vector<const ComponentSpec*> installComponents;
    bool interactive = false;
    std::string installDir;  // the real directory to replace
    std::string relaunchExe; // real executable name to start afterwards
    int64_t waitPid = 0;     // the real pid to wait for before touching anything
    bool valid = true;
    std::string error;
};

CommandLineOptions parseCommandLine(int argc, char** argv) {
    CommandLineOptions options;
    auto needsValue = [&](int& i, const char* flag) -> std::string {
        if (i + 1 >= argc) {
            options.valid = false;
            options.error = std::string(flag) + " requires a value";
            return {};
        }
        return argv[++i];
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--update") {
            options.updateMode = true;
        } else if (arg == "--install-dir") {
            options.installDir = needsValue(i, "--install-dir");
        } else if (arg == "--relaunch") {
            options.relaunchExe = needsValue(i, "--relaunch");
        } else if (arg == "--wait-pid") {
            std::string value = needsValue(i, "--wait-pid");
            if (options.valid) {
                try {
                    options.waitPid = std::stoll(value);
                } catch (const std::exception&) {
                    options.valid = false;
                    options.error = "--wait-pid expects a real numeric process id, got \"" + value + "\"";
                }
            }
        } else if (arg == "--install-components") {
            std::string value = needsValue(i, "--install-components");
            if (options.valid && !parseComponentList(value, options.installComponents, options.error)) {
                options.valid = false;
            }
        } else if (arg == "--select-components") {
            options.interactive = true;
        } else {
            options.valid = false;
            options.error = "unrecognized argument \"" + arg + "\"";
        }
        if (!options.valid) break;
    }

    if (options.valid && options.updateMode && options.installDir.empty()) {
        options.valid = false;
        options.error = "--update requires --install-dir";
    }
    if (options.valid && !options.installComponents.empty() && options.installDir.empty()) {
        options.valid = false;
        options.error = "--install-components requires --install-dir";
    }
    if (options.valid && options.updateMode && !options.installComponents.empty()) {
        options.valid = false;
        options.error = "--update and --install-components are mutually exclusive";
    }
    if (options.updateMode && options.relaunchExe.empty()) options.relaunchExe = currentPlatformRuntimeExe();
    return options;
}

} // namespace

int main(int argc, char** argv) {
    CommandLineOptions options = parseCommandLine(argc, argv);
    if (!options.valid) {
        std::fprintf(stderr,
                      "kronos_installer: %s\n"
                      "usage: kronos_installer                       (first-time install, GUI)\n"
                      "       kronos_installer --update --install-dir <dir>\n"
                      "                        [--relaunch <exe>] [--wait-pid <pid>]\n"
                      "       kronos_installer --install-components <ids|all> --install-dir <dir>\n"
                      "                        (GUI -- the Player's own in-app tool manager uses this)\n"
                      "       kronos_installer --select-components [--install-dir <dir>]\n"
                      "                        (pure-terminal component picker, no display needed)\n",
                      options.error.c_str());
        return 2;
    }

    // Kronos ("Installer Component Selection" -- Linux interactive
    // TUI): dispatched BEFORE SDL_Init() specifically so this real mode
    // works with no display server at all (a plain SSH session, a
    // minimal server install) -- the whole point of offering a terminal
    // alternative to the GUI installer in the first place.
    if (options.interactive) {
        std::string installDir = options.installDir;
        if (installDir.empty()) {
#if defined(_WIN32)
            const char* localAppData = std::getenv("LOCALAPPDATA");
            installDir = localAppData != nullptr ? (std::filesystem::path(localAppData) / "Kronos").string()
                                                  : "C:\\Kronos";
#else
            const char* home = std::getenv("HOME");
            installDir = home != nullptr ? (std::filesystem::path(home) / ".local" / "share" / "Kronos").string()
                                          : "./Kronos";
#endif
        }
        return runInteractiveComponentSelector(installDir);
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "kronos_installer: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    bool installingComponents = !options.installComponents.empty();
    const char* windowTitle =
        options.updateMode ? "Kronos Updater" : installingComponents ? "Kronos Tool Manager" : "Kronos Installer";
    SDL_Window* window = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 520,
                                           (options.updateMode || installingComponents) ? 220 : 320, SDL_WINDOW_SHOWN);
    if (window == nullptr) {
        std::fprintf(stderr, "kronos_installer: SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) {
        std::fprintf(stderr, "kronos_installer: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // real, no imgui.ini for a one-shot installer window
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);

    InstallerState state;
    char installDirBuffer[512];
    std::string defaultInstallDir = (std::filesystem::path(SDL_GetPrefPath("Kronos", "Kronos")).parent_path()).string();
#if defined(_WIN32)
    const char* localAppData = std::getenv("LOCALAPPDATA");
    if (localAppData != nullptr) defaultInstallDir = (std::filesystem::path(localAppData) / "Kronos").string();
#else
    const char* home = std::getenv("HOME");
    if (home != nullptr) defaultInstallDir = (std::filesystem::path(home) / ".local" / "share" / "Kronos").string();
#endif
    std::snprintf(installDirBuffer, sizeof(installDirBuffer), "%s", defaultInstallDir.c_str());

    // Update mode starts working immediately -- there is nothing for the
    // user to choose here; the running Kronos already asked them.
    if (options.updateMode) {
        state.worker = std::thread([&state, &options]() {
            runUpdate(state, options.installDir, options.relaunchExe, options.waitPid);
        });
    }
    // Kronos ("In-Player Tool Manager"): same real "start immediately"
    // reasoning -- the Player's own tool-manager panel already showed
    // its own confirmation before launching this helper (see
    // RuntimeShell.cpp's startComponentInstall()), so there's nothing
    // left to ask here either.
    if (installingComponents) {
        state.worker = std::thread([&state, &options]() {
            runInstallComponents(state, options.installComponents, options.installDir);
        });
    }

    // Real auto-close: once a real update has fully succeeded, this
    // helper's own window has nothing left to say and the relaunched app
    // is already coming up in front of it. Failures deliberately stay on
    // screen so the user can actually read what went wrong.
    bool autoCloseArmed = false;
    uint32_t autoCloseAtTicks = 0;

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
        }

        ImGui_ImplSDLRenderer2_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Kronos Installer", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::SetWindowFontScale(1.4f);
        ImGui::TextUnformatted(options.updateMode      ? "Updating Kronos"
                                : installingComponents  ? "Kronos Tool Manager"
                                                        : "Kronos Installer");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled(options.updateMode ? "Downloading and installing the latest release. This only takes a "
                                                  "moment."
                            : installingComponents
                                ? "Installing the selected Kronos tools. This only takes a moment."
                                : "Downloads and installs the latest Kronos release -- no compiler required.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        InstallStage stage = state.stage.load();
        bool busy = stage != InstallStage::Idle && stage != InstallStage::Done && stage != InstallStage::Failed;

        if (!options.updateMode && !installingComponents) {
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::BeginDisabled(busy);
            ImGui::InputText("##install_dir", installDirBuffer, sizeof(installDirBuffer));
            ImGui::EndDisabled();
            ImGui::TextDisabled("Install directory");
            ImGui::Dummy(ImVec2(0.0f, 10.0f));

            ImGui::BeginDisabled(busy);
            ImVec2 buttonSize(ImGui::GetContentRegionAvail().x * 0.48f, 44.0f);
            if (ImGui::Button("Install for Windows", buttonSize)) {
                if (state.worker.joinable()) state.worker.join();
                std::string installDir = installDirBuffer;
                state.worker = std::thread([&state, installDir]() {
                    runInstall(state, "windows-x64.zip", true, "engine_runtime.exe", installDir);
                });
            }
            ImGui::SameLine();
            if (ImGui::Button("Install for Linux", buttonSize)) {
                if (state.worker.joinable()) state.worker.join();
                std::string installDir = installDirBuffer;
                state.worker = std::thread([&state, installDir]() {
                    runInstall(state, "linux-x64.tar.gz", false, "engine_runtime", installDir);
                });
            }
            ImGui::EndDisabled();

            ImGui::Dummy(ImVec2(0.0f, 14.0f));
        }

        // Kronos ("A progress bar showing the download status"): real
        // byte counts from the real in-flight libcurl transfer (see
        // Downloader.cpp's own progress callback) -- 0 while idle/
        // before a real Content-Length is known yet.
        uint64_t downloaded = state.bytesDownloaded.load();
        uint64_t total = state.totalBytes.load();
        float fraction = (stage == InstallStage::Done) ? 1.0f : (total > 0 ? static_cast<float>(downloaded) / static_cast<float>(total) : 0.0f);
        char overlay[64];
        if (stage == InstallStage::Downloading && total > 0) {
            std::snprintf(overlay, sizeof(overlay), "%s / %s", formatBytes(downloaded).c_str(), formatBytes(total).c_str());
        } else {
            overlay[0] = '\0';
        }
        ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), overlay[0] != '\0' ? overlay : nullptr);

        {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (!state.statusMessage.empty()) {
                ImVec4 color = stage == InstallStage::Failed  ? ImVec4(0.8f, 0.25f, 0.2f, 1.0f)
                                : stage == InstallStage::Done  ? ImVec4(0.13f, 0.55f, 0.25f, 1.0f)
                                                                : ImVec4(0.176f, 0.216f, 0.282f, 1.0f);
                ImGui::TextColored(color, "%s", state.statusMessage.c_str());
            }
        }

        if ((options.updateMode || installingComponents) && stage == InstallStage::Done && !autoCloseArmed) {
            autoCloseArmed = true;
            autoCloseAtTicks = SDL_GetTicks() + 1500; // real, brief "N of N installed" confirmation
        }
        if (autoCloseArmed && SDL_TICKS_PASSED(SDL_GetTicks(), autoCloseAtTicks)) running = false;

        ImGui::End();

        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 250, 250, 248, 255); // matches Kronos's own "Warm Ivory" window background
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);
    }

    if (state.worker.joinable()) state.worker.join();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
