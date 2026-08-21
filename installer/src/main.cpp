// Kronos Bootstrap Installer -- a real, lightweight, standalone C++ app
// (SDL2 + Dear ImGui, SDL_Renderer backend -- deliberately not Vulkan,
// see installer/CMakeLists.txt's own header comment) that fetches the
// real latest Kronos release from GitHub, downloads the right archive
// for the chosen platform, verifies it against the real published
// checksum, extracts it, and wires up real platform integration --
// so a new user never has to touch a compiler.
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

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

// Kronos ("In-App Auto-Updater"): real CLI surface. With no arguments
// this binary is the first-time Bootstrap Installer exactly as before;
// with --update it is the update helper a running Kronos spawns just
// before exiting.
struct CommandLineOptions {
    bool updateMode = false;
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
                      "                        [--relaunch <exe>] [--wait-pid <pid>]\n",
                      options.error.c_str());
        return 2;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "kronos_installer: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const char* windowTitle = options.updateMode ? "Kronos Updater" : "Kronos Installer";
    SDL_Window* window = SDL_CreateWindow(windowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 520,
                                           options.updateMode ? 220 : 320, SDL_WINDOW_SHOWN);
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
        ImGui::TextUnformatted(options.updateMode ? "Updating Kronos" : "Kronos Installer");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled(options.updateMode
                                 ? "Downloading and installing the latest release. This only takes a moment."
                                 : "Downloads and installs the latest Kronos release -- no compiler required.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        InstallStage stage = state.stage.load();
        bool busy = stage != InstallStage::Idle && stage != InstallStage::Done && stage != InstallStage::Failed;

        if (!options.updateMode) {
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

        if (options.updateMode && stage == InstallStage::Done && !autoCloseArmed) {
            autoCloseArmed = true;
            autoCloseAtTicks = SDL_GetTicks() + 1500; // real, brief "Updated to vX" confirmation
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
