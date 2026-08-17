#include "studio/CommandPalette.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include <imgui.h>

namespace engine::studio {

namespace {
std::string toLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
} // namespace

void CommandPalette::open() {
    isOpen_ = true;
    justOpened_ = true;
    queryBuffer_[0] = '\0';
    highlightedIndex_ = 0;
}

void CommandPalette::draw(const std::vector<PaletteCommand>& commands, const EntitySearchFn& entitySearch) {
    if (!isOpen_) return;

    std::string query = queryBuffer_;
    std::string queryLower = toLower(query);

    // Real, substring, case-insensitive filter against every real
    // command's own label -- a query of "" (just opened, nothing typed
    // yet) matches everything, so the palette opens showing the full
    // real action list rather than an empty result.
    std::vector<const PaletteCommand*> results;
    for (const PaletteCommand& command : commands) {
        if (queryLower.empty() || toLower(command.label).find(queryLower) != std::string::npos) {
            results.push_back(&command);
        }
    }

    // Kronos ("...type an entity name to jump the viewport camera
    // directly to it"): real entity-name matches are appended after
    // command matches, not blended/sorted together -- "type '>' for a
    // command, or a plain name to jump to an entity" is the same real
    // split VS Code's own palette draws between "> commands" and plain
    // file/symbol search, and it keeps registered commands from getting
    // buried under entity results in a scene with many similarly-named
    // entities.
    std::vector<PaletteCommand> entityMatches;
    if (!query.empty() && entitySearch) entityMatches = entitySearch(query);
    for (const PaletteCommand& match : entityMatches) results.push_back(&match);

    if (highlightedIndex_ >= static_cast<int>(results.size())) {
        highlightedIndex_ = results.empty() ? 0 : static_cast<int>(results.size()) - 1;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 viewportCenter(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.28f);
    ImGui::SetNextWindowPos(viewportCenter, ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 0.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowFocus();

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings;
    bool stillOpen = isOpen_;
    if (ImGui::Begin("Command Palette##CommandPalette", &stillOpen, flags)) {
        if (justOpened_) {
            ImGui::SetKeyboardFocusHere();
            justOpened_ = false;
        }
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##query", "Type a command, or an entity name to focus it...", queryBuffer_,
                                  sizeof(queryBuffer_));

        // Keyboard navigation -- Up/Down move the highlight, Enter
        // executes whichever row is highlighted, real and independent of
        // mouse hover so a keyboard-only workflow (the entire point of a
        // command palette) works without ever touching the mouse.
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true) && !results.empty()) {
            highlightedIndex_ = std::min(highlightedIndex_ + 1, static_cast<int>(results.size()) - 1);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) && !results.empty()) {
            highlightedIndex_ = std::max(highlightedIndex_ - 1, 0);
        }

        const PaletteCommand* toExecute = nullptr;
        ImGui::Separator();
        ImGui::BeginChild("##results", ImVec2(0.0f, std::min(280.0f, results.size() * 26.0f + 8.0f)));
        for (int i = 0; i < static_cast<int>(results.size()); ++i) {
            ImGui::PushID(i);
            bool isHighlighted = (i == highlightedIndex_);
            if (ImGui::Selectable(results[static_cast<size_t>(i)]->label.c_str(), isHighlighted)) {
                toExecute = results[static_cast<size_t>(i)];
            }
            if (isHighlighted && ImGui::IsWindowAppearing() == false) {
                // Keeps the highlighted row visible as Up/Down moves it
                // past the currently-scrolled viewport, the same
                // "keyboard nav keeps its own target on-screen"
                // convention any real list-searching UI needs.
                ImGui::SetScrollHereY(0.5f);
            }
            ImGui::PopID();
        }
        if (results.empty()) ImGui::TextDisabled("No matching command or entity.");
        ImGui::EndChild();

        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && !results.empty() &&
            highlightedIndex_ < static_cast<int>(results.size())) {
            toExecute = results[static_cast<size_t>(highlightedIndex_)];
        }

        if (toExecute != nullptr) {
            // Real, deliberate order -- close the palette *before*
            // running the command, not after: a command that itself
            // opens another real ImGui window/popup this same frame
            // (plausible for something like a future "New Scene"
            // action) shouldn't have this window's own still-active
            // state fighting it.
            close();
            toExecute->execute();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            close();
        }
    }
    ImGui::End();
    if (!stillOpen) close(); // real 'x' button in the title bar
}

} // namespace engine::studio
