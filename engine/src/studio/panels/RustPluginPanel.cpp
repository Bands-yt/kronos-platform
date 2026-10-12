#include "studio/panels/RustPluginPanel.hpp"

#include <fstream>
#include <sstream>

#include <imgui.h>

#include "studio/panels/ColorTextEditBackend.hpp"

namespace engine::studio::panels {

namespace fs = std::filesystem;

RustPluginPanel::RustPluginPanel() = default;
RustPluginPanel::~RustPluginPanel() = default;

void RustPluginPanel::refresh(const Paths& paths) {
    std::error_code ec;
    fs::create_directories(paths.projects, ec);
    const std::string current = selected_ >= 0 && selected_ < static_cast<int>(projects_.size())
                                    ? projects_[static_cast<size_t>(selected_)].name
                                    : std::string();
    projects_ = RustPluginProject::scan(paths.projects);
    selected_ = -1;
    for (size_t i = 0; i < projects_.size(); ++i) {
        if (projects_[i].name == current) selected_ = static_cast<int>(i);
    }
    listed_ = true;
}

void RustPluginPanel::select(size_t index) {
    if (editor_ != nullptr && selected_ >= 0) save();
    selected_ = static_cast<int>(index);
    if (editor_ == nullptr) {
        editor_ = std::make_unique<ColorTextEditBackend>();
        (void)editor_->initialize();
        editor_->setLanguage(ColorTextEditBackend::Language::Rust);
    }
    std::ifstream in(projects_[index].dir / "src" / "lib.rs", std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    savedText_ = text.str();
    editor_->setSource(savedText_);
}

bool RustPluginPanel::save() {
    if (editor_ == nullptr || selected_ < 0 || selected_ >= static_cast<int>(projects_.size())) return true;
    const std::string& text = editor_->source();
    if (text == savedText_) return true;
    std::ofstream out(projects_[static_cast<size_t>(selected_)].dir / "src" / "lib.rs", std::ios::binary);
    out << text;
    if (!out) {
        status_ = "Couldn't save lib.rs";
        return false;
    }
    savedText_ = text;
    return true;
}

void RustPluginPanel::startBuild(bool install) {
    if (!save()) return;
    std::string error;
    installAfterBuild_ = install;
    if (build_.start(projects_[static_cast<size_t>(selected_)].dir, error)) {
        status_ = "Building...";
    } else {
        status_ = error;
    }
}

void RustPluginPanel::draw(const Paths& paths) {
    if (!open_) return;
    ImGui::SetNextWindowSize(ImVec2(920.0f, 600.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Rust Plugins", &open_)) {
        ImGui::End();
        return;
    }
    if (!listed_) refresh(paths);
    if (cargo_ < 0) cargo_ = RustPluginProject::cargoAvailable() ? 1 : 0;

    const RustBuild::State before = build_.state();
    build_.update();
    if (before == RustBuild::State::Running) {
        if (build_.state() == RustBuild::State::Succeeded) {
            status_ = "Build finished.";
            if (installAfterBuild_ && selected_ >= 0) {
                std::string error;
                const RustProjectInfo& project = projects_[static_cast<size_t>(selected_)];
                const fs::path library = RustPluginProject::builtLibrary(project.dir, project.crate);
                status_ = RustPluginProject::install(library, paths.pluginDir, error)
                              ? "Installed " + library.filename().string() +
                                    ". A plugin Studio already runs reloads by itself; a new one shows up after Studio restarts."
                              : error;
            }
        } else if (build_.state() == RustBuild::State::Failed) {
            status_ = "Build failed. See the log below.";
        }
    }

    if (cargo_ == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "Rust (cargo) was not found.");
        ImGui::TextDisabled("Install it from rustup.rs, then press Check again.");
        if (ImGui::Button("Check again")) cargo_ = -1;
        ImGui::Separator();
    }

    ImGui::SetNextItemWidth(180.0f);
    ImGui::InputTextWithHint("##newname", "New plugin name", newName_, sizeof(newName_));
    ImGui::SameLine();
    if (ImGui::Button("Create")) {
        fs::path created;
        std::string error;
        std::error_code ec;
        fs::create_directories(paths.projects, ec);
        if (RustPluginProject::create(paths.projects, newName_, paths.templateDir, paths.sdkDir, created, error)) {
            newName_[0] = '\0';
            refresh(paths);
            for (size_t i = 0; i < projects_.size(); ++i) {
                if (projects_[i].dir == created) select(i);
            }
            status_ = "Created " + created.string();
        } else {
            status_ = error;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) refresh(paths);
    ImGui::TextDisabled("Plugins live in %s", paths.projects.string().c_str());
    ImGui::Separator();

    ImGui::BeginChild("##rustlist", ImVec2(170.0f, 0.0f), ImGuiChildFlags_Borders);
    for (size_t i = 0; i < projects_.size(); ++i) {
        if (ImGui::Selectable(projects_[i].name.c_str(), static_cast<int>(i) == selected_) &&
            static_cast<int>(i) != selected_) {
            select(i);
        }
    }
    if (projects_.empty()) ImGui::TextDisabled("No plugins yet.\nName one above\nand press Create.");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginGroup();
    if (selected_ >= 0 && editor_ != nullptr) {
        const bool running = build_.state() == RustBuild::State::Running;
        const bool dirty = editor_->source() != savedText_;
        if (ImGui::Button(dirty ? "Save *" : "Save")) (void)save();
        ImGui::SameLine();
        ImGui::BeginDisabled(running || cargo_ != 1);
        if (ImGui::Button("Build")) startBuild(false);
        ImGui::SameLine();
        if (ImGui::Button("Build & Install")) startBuild(true);
        ImGui::EndDisabled();
        if (running) {
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                build_.cancel();
                status_ = "Build cancelled.";
            }
        }
        if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
        const float logHeight = build_.state() == RustBuild::State::Idle ? 0.0f : 120.0f;
        ImGui::BeginChild("##rustedit", ImVec2(0.0f, -logHeight - (logHeight > 0.0f ? 8.0f : 0.0f)));
        editor_->draw();
        ImGui::EndChild();
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl &&
            ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            (void)save();
        }
        if (logHeight > 0.0f) {
            ImGui::BeginChild("##rustlog", ImVec2(0.0f, logHeight), ImGuiChildFlags_Borders);
            for (const std::string& line : build_.logTail(60)) ImGui::TextUnformatted(line.c_str());
            if (running) ImGui::SetScrollHereY(1.0f);
            ImGui::EndChild();
        }
    } else {
        ImGui::TextDisabled("Pick a plugin on the left, or create one.");
        if (!status_.empty()) ImGui::TextWrapped("%s", status_.c_str());
    }
    ImGui::EndGroup();
    ImGui::End();
}

} // namespace engine::studio::panels
