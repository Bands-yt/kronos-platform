#include "studio/FileBrowse.hpp"

#include <cstdio>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>

namespace engine::studio {

namespace {

std::unordered_map<ImGuiID, std::string>& results() {
    static std::unordered_map<ImGuiID, std::string> map;
    return map;
}

std::unordered_set<ImGuiID>& waiting() {
    static std::unordered_set<ImGuiID> set;
    return set;
}

bool browse(const char* id, const core::FileDialogOptions& options, std::string& picked) {
    ImGui::PushID(id);
    const ImGuiID key = ImGui::GetID("##browse");
    const bool pending = waiting().count(key) != 0 && core::fileDialogOpen();
    if (!core::fileDialogOpen()) waiting().erase(key);
    ImGui::BeginDisabled(pending);
    if (ImGui::Button(pending ? "Choosing..." : "Browse...")) {
        if (core::openFileDialogAsync(options, [key](const std::string& path) { results()[key] = path; })) {
            waiting().insert(key);
        }
    }
    ImGui::EndDisabled();
    ImGui::PopID();

    auto it = results().find(key);
    if (it == results().end()) return false;
    picked = std::move(it->second);
    results().erase(it);
    waiting().erase(key);
    return true;
}

} // namespace

bool browseButton(const char* id, char* buffer, size_t bufferSize, const core::FileDialogOptions& options) {
    std::string picked;
    if (!browse(id, options, picked)) return false;
    std::snprintf(buffer, bufferSize, "%s", picked.c_str());
    return true;
}

bool browseButton(const char* id, std::string& value, const core::FileDialogOptions& options) {
    std::string picked;
    if (!browse(id, options, picked)) return false;
    value = std::move(picked);
    return true;
}

} // namespace engine::studio
