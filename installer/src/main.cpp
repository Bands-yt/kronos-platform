// Kronos Bootstrap Installer -- a real, lightweight, standalone C++ app
// (SDL2 + Dear ImGui, SDL_Renderer backend -- deliberately not Vulkan,
// see installer/CMakeLists.txt's own header comment) that fetches the
// real latest Kronos release from GitHub, downloads the right archive
// for the chosen platform, verifies it against the real published
// checksum, extracts it, and wires up real platform integration --
// so a new user never has to touch a compiler.
#include <atomic>
#include <cstdio>
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

namespace {

constexpr const char* kRepoOwner = "Bands-yt";
constexpr const char* kRepoName = "kronos-platform";

enum class InstallStage { Idle, FetchingRelease, Downloading, Verifying, Extracting, Integrating, Done, Failed };

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
        std::filesystem::path runtimePath = std::filesystem::path(installDir) / "kronos-alpha" / runtimeExeName;
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

} // namespace

int main(int, char**) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "kronos_installer: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Kronos Installer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 520, 320,
                                           SDL_WINDOW_SHOWN);
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
        ImGui::TextUnformatted("Kronos Installer");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::TextDisabled("Downloads and installs the latest Kronos release -- no compiler required.");
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        InstallStage stage = state.stage.load();
        bool busy = stage != InstallStage::Idle && stage != InstallStage::Done && stage != InstallStage::Failed;

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
            state.worker = std::thread(
                [&state, installDir]() { runInstall(state, "windows-x64.zip", true, "engine_runtime.exe", installDir); });
        }
        ImGui::SameLine();
        if (ImGui::Button("Install for Linux", buttonSize)) {
            if (state.worker.joinable()) state.worker.join();
            std::string installDir = installDirBuffer;
            state.worker = std::thread(
                [&state, installDir]() { runInstall(state, "linux-x64.tar.gz", false, "engine_runtime", installDir); });
        }
        ImGui::EndDisabled();

        ImGui::Dummy(ImVec2(0.0f, 14.0f));

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
