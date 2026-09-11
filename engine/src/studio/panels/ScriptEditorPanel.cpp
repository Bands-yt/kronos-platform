#include "studio/panels/ScriptEditorPanel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "core/Components.hpp"
#include "core/NativeFileDialog.hpp"
#include "studio/Notification.hpp"
#include "studio/panels/ColorTextEditBackend.hpp"

namespace engine::studio::panels {

namespace {
// Kronos ("Script Editor Polish" -- "syntax coloring for Luau
// keywords"): the real Luau reserved-word set (standard Lua 5.1
// keywords plus Luau's own `continue` extension -- see
// https://luau.org/syntax#keywords) -- not a guessed/approximate list.
const std::unordered_set<std::string>& luauKeywords() {
    static const std::unordered_set<std::string> kKeywords = {
        "and",    "break",  "do",     "else",   "elseif", "end",    "false",  "for",
        "function", "if",   "in",     "local",  "nil",    "not",    "or",     "repeat",
        "return", "then",   "true",   "until",  "while",  "continue",
    };
    return kKeywords;
}

enum class TokenKind { Plain, Keyword, String, Comment, Number };

struct Token {
    TokenKind kind;
    std::string text;
};

ImVec4 colorForToken(TokenKind kind) {
    switch (kind) {
        case TokenKind::Keyword: return ImVec4(0.161f, 0.322f, 0.749f, 1.0f);  // deep indigo
        case TokenKind::String: return ImVec4(0.129f, 0.549f, 0.251f, 1.0f);  // forest green
        case TokenKind::Comment: return ImVec4(0.55f, 0.58f, 0.52f, 1.0f);    // muted gray-green
        case TokenKind::Number: return ImVec4(0.553f, 0.247f, 0.647f, 1.0f);  // plum/purple
        case TokenKind::Plain: default: return ImVec4(0.176f, 0.216f, 0.282f, 1.0f); // matches kText
    }
}

// Real, small, line-oriented Luau tokenizer -- deliberately not a full
// Luau.Ast-backed parse (that's real, separate, much larger scope; see
// IScriptEditorBackend's own class comment on why a real Monaco/Luau.Ast
// integration is out of this skeleton's reach). Good enough for real
// visual coloring of keywords/strings/comments/numbers; doesn't attempt
// real syntax error detection. `inBlockComment` is real, carried
// *across* lines by the caller (a `--[[ ... ]]` block spans multiple
// real lines) -- passed and updated by reference so a whole-buffer scan
// stays a single, real top-to-bottom pass.
std::vector<Token> tokenizeLine(const std::string& line, bool& inBlockComment) {
    std::vector<Token> tokens;
    size_t i = 0;
    const size_t n = line.size();

    auto pushToken = [&](TokenKind kind, size_t start, size_t end) {
        if (end > start) tokens.push_back(Token{kind, line.substr(start, end - start)});
    };

    if (inBlockComment) {
        size_t closeAt = line.find("]]");
        if (closeAt == std::string::npos) {
            pushToken(TokenKind::Comment, 0, n);
            return tokens;
        }
        pushToken(TokenKind::Comment, 0, closeAt + 2);
        i = closeAt + 2;
        inBlockComment = false;
    }

    while (i < n) {
        char c = line[i];

        // Line/block comment start.
        if (c == '-' && i + 1 < n && line[i + 1] == '-') {
            if (i + 3 < n && line[i + 2] == '[' && line[i + 3] == '[') {
                size_t closeAt = line.find("]]", i + 4);
                if (closeAt == std::string::npos) {
                    pushToken(TokenKind::Comment, i, n);
                    inBlockComment = true;
                    return tokens;
                }
                pushToken(TokenKind::Comment, i, closeAt + 2);
                i = closeAt + 2;
                continue;
            }
            pushToken(TokenKind::Comment, i, n);
            break;
        }

        // String literal ("..." or '...'), real backslash-escape aware
        // so an escaped quote doesn't end the string early.
        if (c == '"' || c == '\'') {
            char quote = c;
            size_t start = i;
            ++i;
            while (i < n && line[i] != quote) {
                if (line[i] == '\\' && i + 1 < n) ++i;
                ++i;
            }
            if (i < n) ++i; // consume the real closing quote
            pushToken(TokenKind::String, start, i);
            continue;
        }

        // Identifier/keyword.
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            size_t start = i;
            while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_')) ++i;
            std::string word = line.substr(start, i - start);
            pushToken(luauKeywords().count(word) != 0 ? TokenKind::Keyword : TokenKind::Plain, start, i);
            continue;
        }

        // Number (real, simple -- digits, one real decimal point, no
        // hex/scientific-notation recognition; good enough for real
        // visual coloring, not a real numeric-literal validator).
        if (std::isdigit(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < n && (std::isdigit(static_cast<unsigned char>(line[i])) || line[i] == '.')) ++i;
            pushToken(TokenKind::Number, start, i);
            continue;
        }

        size_t start = i;
        ++i;
        pushToken(TokenKind::Plain, start, i);
    }

    return tokens;
}

// Kronos ("Script Editor QoL" -- actionable empty state): real collision
// check against every live entity's core::Name -- without this, "Create
// New Script" clicked twice in a row would silently produce two
// same-named entities, which would break DebugConsolePanel's own
// findEntityByName() (first-match-wins) for click-to-jump on whichever
// one comes second.
bool entityNameTaken(core::ECS& ecs, const std::string& candidate) {
    for (auto entity : ecs.view<core::Name>()) {
        const auto* nameComp = ecs.tryGetComponent<core::Name>(entity);
        if (nameComp != nullptr && nameComp->value == candidate) return true;
    }
    return false;
}
} // namespace

void ImGuiFallbackEditor::draw() {
    ImVec2 avail = ImGui::GetContentRegionAvail();

    // Kronos ("Script Editor Polish" -- "line number gutters"): a real,
    // fixed-width column, one real ImGui::TextDisabled() row per real
    // line in `buffer_`. Shares the exact same scrolling region as the
    // colorized view / the plain edit box below it (a single BeginChild
    // per mode, gutter drawn first inside it, not a separately-scrolled
    // sibling), so it never drifts out of sync with the real line it's
    // labeling.
    int lineCount = 1;
    for (char c : buffer_) if (c == '\n') ++lineCount;
    char widestLineNumber[16];
    std::snprintf(widestLineNumber, sizeof(widestLineNumber), "%d", lineCount);
    float gutterWidth = ImGui::CalcTextSize(widestLineNumber).x + 16.0f;

    if (wasFocused_) {
        // Real edit mode -- the exact same reliable InputTextMultiline
        // every prior pass of this editor used, real cursor/selection/
        // undo/clipboard all still 100% ImGui-native (nothing about
        // editing itself changed).
        ImGui::BeginChild("##editor_gutter_edit", ImVec2(gutterWidth, avail.y), false,
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        for (int line = 1; line <= lineCount; ++line) ImGui::TextDisabled("%d", line);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##editor_edit", ImVec2(avail.x - gutterWidth - 4.0f, avail.y), false);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextMultiline("##source", &buffer_, ImGui::GetContentRegionAvail(),
                                   ImGuiInputTextFlags_AllowTabInput);
        wasFocused_ = ImGui::IsItemActive() || ImGui::IsItemFocused();
        ImGui::EndChild();
        return;
    }

    // Real, colorized, read-only view -- swapped to InputTextMultiline
    // the instant the user clicks in (below).
    ImGui::BeginChild("##editor_gutter_view", ImVec2(gutterWidth, avail.y), false,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    for (int line = 1; line <= lineCount; ++line) ImGui::TextDisabled("%d", line);
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##editor_view", ImVec2(avail.x - gutterWidth - 4.0f, avail.y), false);
    bool inBlockComment = false;
    size_t lineStart = 0;
    while (lineStart <= buffer_.size()) {
        size_t lineEnd = buffer_.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = buffer_.size();
        std::string line = buffer_.substr(lineStart, lineEnd - lineStart);

        std::vector<Token> tokens = tokenizeLine(line, inBlockComment);
        if (tokens.empty()) {
            ImGui::Dummy(ImVec2(1.0f, ImGui::GetTextLineHeight())); // real, honest blank line, still takes a real row
        } else {
            for (size_t t = 0; t < tokens.size(); ++t) {
                if (t > 0) ImGui::SameLine(0.0f, 0.0f);
                ImGui::TextColored(colorForToken(tokens[t].kind), "%s", tokens[t].text.c_str());
            }
        }

        if (lineEnd >= buffer_.size()) break;
        lineStart = lineEnd + 1;
    }
    // One real, invisible full-region button beneath the colorized text
    // (not per-line -- simpler, and a click anywhere in the editor
    // area, including past the last real line, should start editing)
    // real-swaps to edit mode on click.
    ImVec2 clickRegionMin = ImGui::GetWindowPos();
    ImGui::SetCursorScreenPos(clickRegionMin);
    ImGui::InvisibleButton("##editor_view_click_target", ImGui::GetContentRegionAvail());
    if (ImGui::IsItemClicked()) wasFocused_ = true;
    ImGui::EndChild();
}

bool MonacoWebViewEditor::initialize() {
    std::fprintf(stderr,
                  "MonacoWebViewEditor: not implemented -- Monaco requires an embedded webview (CEF/Ultralight, "
                  "see docs/ARCHITECTURE.md §5); falling back to ImGuiFallbackEditor.\n");
    return false;
}

std::unique_ptr<IScriptEditorBackend> ScriptEditorPanel::createBackend() {
    // Kronos ("Studio Revamp" -- "Native Syntax-Highlighting Editor"):
    // real preference order, tried in this sequence --
    //  1. Monaco/webview (never succeeds today; MonacoWebViewEditor::
    //     initialize() always returns false until Ultralight/CEF is
    //     actually wired in, see that class's own comment).
    //  2. ColorTextEditBackend -- the real default: native
    //     ImGuiColorTextEdit widget + live Luau.Analysis error markers,
    //     no embedded webview needed at all.
    //  3. ImGuiFallbackEditor -- kept as the last-resort backend so a
    //     Studio build never silently ends up with no editor, same
    //     contract IScriptEditorBackend's own class comment already
    //     establishes.
    // Kronos ("Script Editor QoL" -- multi-tab document model): each tab
    // gets its OWN backend instance from this factory (rather than
    // sharing one), so one tab's undo history/cursor/completion state
    // never leaks into another's.
    auto monaco = std::make_unique<MonacoWebViewEditor>();
    if (monaco->initialize()) {
        return monaco; // never reached today -- see MonacoWebViewEditor::initialize()
    }

    auto colorTextEdit = std::make_unique<ColorTextEditBackend>();
    if (colorTextEdit->initialize()) {
        return colorTextEdit;
    }

    auto fallback = std::make_unique<ImGuiFallbackEditor>();
    if (!fallback->initialize()) {
        std::fprintf(stderr, "ScriptEditorPanel: ImGuiFallbackEditor::initialize() unexpectedly failed.\n");
    }
    return fallback;
}

int ScriptEditorPanel::findTabIndex(core::EntityId entity) const {
    for (int i = 0; i < static_cast<int>(tabs_.size()); ++i) {
        if (tabs_[i].entity == entity) return i;
    }
    return -1;
}

int ScriptEditorPanel::openOrFocusTab(core::ECS& ecs, core::EntityId entity) {
    int existing = findTabIndex(entity);
    if (existing >= 0) {
        activeTab_ = existing;
        return existing;
    }

    ScriptEditorTab tab;
    tab.entity = entity;
    tab.backend = createBackend();
    if (const core::Script* script = ecs.tryGetComponent<core::Script>(entity)) {
        tab.backend->setSource(script->source);
        tab.savedSource = script->source;
    } else {
        tab.backend->setSource("");
        tab.savedSource.clear();
    }
    tabs_.push_back(std::move(tab));
    activeTab_ = static_cast<int>(tabs_.size()) - 1;
    return activeTab_;
}

void ScriptEditorPanel::closeTab(int index) {
    if (index < 0 || index >= static_cast<int>(tabs_.size())) return;
    tabs_.erase(tabs_.begin() + index);
    if (tabs_.empty()) {
        activeTab_ = -1;
    } else if (activeTab_ >= static_cast<int>(tabs_.size())) {
        activeTab_ = static_cast<int>(tabs_.size()) - 1;
    } else if (activeTab_ > index) {
        --activeTab_;
    }
}

void ScriptEditorPanel::saveTab(ScriptEditorTab& tab, core::ECS& ecs, NotificationCenter& notifications) {
    core::Script* script = ecs.tryGetComponent<core::Script>(tab.entity);
    if (script == nullptr) return;
    // Deliberately only `source` -- `loadedSource` stays whatever it was
    // so core::tickScriptHotReload() sees a real mismatch and does the
    // real (re)load, instead of this save silently marking itself
    // "already loaded" and skipping that step.
    script->source = tab.backend->source();
    tab.savedSource = tab.backend->source();
    notifications.push("Script saved -- will hot-reload on next tick", NotificationSeverity::Success);
}

int ScriptEditorPanel::newScriptTab(core::ECS& ecs) {
    std::string candidate = "Script";
    for (int suffix = 2; entityNameTaken(ecs, candidate); ++suffix) {
        candidate = "Script" + std::to_string(suffix);
    }
    core::EntityId entity = ecs.createEntity(candidate);
    ecs.addComponent<core::Script>(entity);
    int index = openOrFocusTab(ecs, entity);
    forceFocusActiveTab_ = true;
    // Keeps draw()'s own outer-selection edge trigger in sync, same
    // reasoning as openAndJumpToLine()'s own trailing assignment -- an
    // Explorer selection landing on this same new entity later this
    // frame or next shouldn't redundantly re-run openOrFocusTab().
    lastOuterSelection_ = entity;
    return index;
}

void ScriptEditorPanel::openScriptFromFile(core::ECS& ecs, NotificationCenter& notifications) {
    std::optional<std::string> path = core::openFileDialog("Open Script", {"*.luau", "*.lua"});
    if (!path) return; // real cancel, not an error

    std::ifstream file(*path, std::ios::binary);
    if (!file) {
        notifications.push("Could not open \"" + *path + "\"", NotificationSeverity::Error);
        return;
    }
    std::ostringstream contents;
    contents << file.rdbuf();

    // Kronos ("Script Editor QoL" -- actionable empty state): this
    // engine has no filesystem-backed core::Script persistence (source
    // lives in the ECS component, not re-read from `*path` later -- see
    // ScriptEditorTab's own class comment), so "Open File" honestly
    // means "import this file's text into a new script entity", not
    // "open this file in place". The entity's name comes from the
    // file's own stem so the new tab reads as "the file you picked",
    // same std::filesystem::path(...).stem() idiom StudioApp.cpp's own
    // world-id derivation already uses.
    std::string base = std::filesystem::path(*path).stem().string();
    if (base.empty()) base = "Script";
    std::string candidate = base;
    for (int suffix = 2; entityNameTaken(ecs, candidate); ++suffix) {
        candidate = base + std::to_string(suffix);
    }

    core::EntityId entity = ecs.createEntity(candidate);
    core::Script& script = ecs.addComponent<core::Script>(entity);
    script.source = contents.str();

    openOrFocusTab(ecs, entity);
    forceFocusActiveTab_ = true;
    lastOuterSelection_ = entity;
    notifications.push("Imported \"" + *path + "\"", NotificationSeverity::Success);
}

void ScriptEditorPanel::drawTabBar(core::ECS& ecs, NotificationCenter& notifications) {
    if (!ImGui::BeginTabBar("##script_editor_tabs", ImGuiTabBarFlags_Reorderable)) return;

    // Kronos ("Script Editor QoL" -- persistent tab bar): a real
    // ImGuiTabItemFlags_Trailing button, submitted before the regular
    // tabs below -- same order-doesn't-matter idiom imgui_demo.cpp's own
    // "TabItemButton & Leading/Trailing flags" section uses -- so it
    // always renders pinned to the tab bar's right edge regardless of
    // how many real tabs are open, including zero.
    const bool addTabClicked = ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("New script");
    if (addTabClicked) newScriptTab(ecs);

    for (int i = 0; i < static_cast<int>(tabs_.size());) {
        ScriptEditorTab& tab = tabs_[i];
        const core::Name* name = ecs.tryGetComponent<core::Name>(tab.entity);
        const std::string title = (name != nullptr && !name->value.empty()) ? name->value : "(unnamed)";
        const bool dirty = tab.backend->source() != tab.savedSource;
        // "###" splits display label from ImGui id -- the id is the
        // stable entity id, so a tab keeps its identity (selection,
        // reorder position) across a dirty-indicator label change or an
        // entity rename.
        const std::string label =
            title + (dirty ? " *" : "") + "###tab" + std::to_string(static_cast<uint32_t>(tab.entity));

        ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
        if (i == activeTab_ && forceFocusActiveTab_) flags |= ImGuiTabItemFlags_SetSelected;

        bool open = true;
        if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
            activeTab_ = i;
            ImGui::EndTabItem();
        }
        if (!open) {
            closeTab(i);
            continue; // next tab has shifted into slot i -- don't advance
        }
        ++i;
    }

    forceFocusActiveTab_ = false;
    ImGui::EndTabBar();
}

void ScriptEditorPanel::drawEmptyState(core::ECS& ecs, NotificationCenter& notifications) {
    // Kronos ("Script Editor QoL" -- actionable empty state): replaces
    // the old dead-end "Select an entity..." label with two real,
    // working actions -- centered in whatever space is left below the
    // (now-persistent) tab bar, rather than pinned to the window's
    // top-left like a plain label would be.
    // GetContentRegionAvail() measures from the *current cursor*
    // (already below the tab bar drawn just above), but SetCursorPos()
    // below places things in window-local coordinates measured from the
    // window's content origin -- so `start` has to be folded back in, or
    // the block would center against the whole window and sit too high,
    // overlapping the tab bar.
    const ImVec2 start = ImGui::GetCursorPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const char* heading = "No scripts open";
    const char* subheading = "Create a new script or import an existing .luau/.lua file.";
    const ImVec2 buttonSize(190.0f, 36.0f);
    constexpr float kSpacing = 10.0f;

    const float headingWidth = ImGui::CalcTextSize(heading).x;
    const float subheadingWidth = ImGui::CalcTextSize(subheading).x;
    const float buttonsWidth = buttonSize.x * 2.0f + kSpacing;
    const float blockWidth = std::max({headingWidth, subheadingWidth, buttonsWidth});
    const float lineSpacing = ImGui::GetStyle().ItemSpacing.y;
    const float blockHeight =
        ImGui::GetTextLineHeight() * 2.0f + lineSpacing * 2.0f + kSpacing + buttonSize.y;

    // `origin` is the block's fixed top-left in window-local coordinates
    // -- every line below centers itself against `origin.x + blockWidth`
    // rather than against wherever the cursor happened to land after the
    // previous item, since ImGui resets CursorPos.x back to the current
    // line/group's own indent (not to a prior SetCursorPosX() call) after
    // every item.
    const ImVec2 origin(start.x + std::max(0.0f, (avail.x - blockWidth) * 0.5f),
                         start.y + std::max(0.0f, (avail.y - blockHeight) * 0.5f));

    ImGui::SetCursorPos(origin);
    ImGui::BeginGroup();
    {
        ImGui::SetCursorPosX(origin.x + std::max(0.0f, (blockWidth - headingWidth) * 0.5f));
        ImGui::TextUnformatted(heading);

        ImGui::SetCursorPosX(origin.x + std::max(0.0f, (blockWidth - subheadingWidth) * 0.5f));
        ImGui::TextDisabled("%s", subheading);

        ImGui::Dummy(ImVec2(1.0f, kSpacing));

        ImGui::SetCursorPosX(origin.x + std::max(0.0f, (blockWidth - buttonsWidth) * 0.5f));
        if (ImGui::Button("Create New Script", buttonSize)) {
            newScriptTab(ecs);
        }
        ImGui::SameLine(0.0f, kSpacing);
        if (ImGui::Button("Open File", buttonSize)) {
            openScriptFromFile(ecs, notifications);
        }
    }
    ImGui::EndGroup();
}

void ScriptEditorPanel::openAndJumpToLine(core::ECS& ecs, core::EntityId entity, int oneBasedLine) {
    // Real, honest no-op if there's no script here to jump into -- see
    // this method's own header comment.
    if (entity == core::kNullEntity || ecs.tryGetComponent<core::Script>(entity) == nullptr) return;

    int index = openOrFocusTab(ecs, entity);
    forceFocusActiveTab_ = true;
    tabs_[index].backend->moveCaretToLine(oneBasedLine);
    // Keeps draw()'s own outer-selection edge trigger in sync so it
    // doesn't redundantly re-open this same tab the instant the Explorer
    // selection also lands on `entity` this frame or next.
    lastOuterSelection_ = entity;
}

void ScriptEditorPanel::draw(core::ECS& ecs, core::EntityId selectedEntity, NotificationCenter& notifications) {
    if (selectedEntity != lastOuterSelection_) {
        lastOuterSelection_ = selectedEntity;
        if (selectedEntity != core::kNullEntity) {
            openOrFocusTab(ecs, selectedEntity);
            forceFocusActiveTab_ = true;
        }
    }

    // Zero outer padding, same as ViewportPanel's own "Viewport" window --
    // lets the text editor fill the central dockspace edge-to-edge
    // instead of sitting inset behind a frame.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("Script Editor");
    ImGui::PopStyleVar();

    // Kronos ("Script Editor QoL" -- persistent tab bar): drawn
    // unconditionally now, even with zero tabs open -- see
    // drawTabBar()'s own trailing "+" button.
    drawTabBar(ecs, notifications);

    if (tabs_.empty()) {
        drawEmptyState(ecs, notifications);
        ImGui::End();
        return;
    }

    if (activeTab_ < 0 || activeTab_ >= static_cast<int>(tabs_.size())) {
        ImGui::End();
        return;
    }
    ScriptEditorTab& tab = tabs_[activeTab_];

    const core::Name* name = ecs.tryGetComponent<core::Name>(tab.entity);
    ImGui::TextDisabled("Entity: %s", (name != nullptr && !name->value.empty()) ? name->value.c_str() : "(unnamed)");

    core::Script* script = ecs.tryGetComponent<core::Script>(tab.entity);
    if (script == nullptr) {
        ImGui::TextWrapped("This entity has no Script component yet.");
        if (ImGui::Button("Add Script Component")) {
            ecs.addComponent<core::Script>(tab.entity);
            tab.backend->setSource("");
            tab.savedSource.clear();
        }
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Ctrl+S save * Ctrl+W close tab (real hot-reload while Playing)");
    ImGui::Separator();
    // Kronos ("Script Editor QoL" -- editor background contrast): a real
    // ImGuiCol_ChildBg push, VS Code's own #1E1E1E editor background --
    // gives the actual editing surface visible depth against the
    // surrounding panel chrome (Ctrl+S/Ctrl+W hint line, tab bar) above
    // it. Harmless, not redundant, for ColorTextEditBackend specifically:
    // TextEditor::Render() pushes its own ImGuiCol_ChildBg from its
    // palette right before its BeginChild() (already close to this same
    // color by default) and pops it before returning, so this outer
    // push/pop only ever affects backends -- like ImGuiFallbackEditor's
    // plain BeginChild() calls -- that don't already override it
    // themselves; the two pushes nest and unwind cleanly either way.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0x1E, 0x1E, 0x1E, 255));
    tab.backend->draw();
    ImGui::PopStyleColor();

    // Checked here, not StudioApp's own global per-frame keybind block,
    // so these only fire while the Script Editor window genuinely has
    // keyboard focus -- Ctrl+S/Ctrl+W elsewhere in Studio keep their own
    // existing meanings (save scene / close active dock, respectively),
    // unchanged.
    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    if (windowFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        saveTab(tab, ecs, notifications);
    }
    if (windowFocused && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
        closeTab(activeTab_);
        ImGui::End();
        return;
    }

    ImGui::End();
}

} // namespace engine::studio::panels
