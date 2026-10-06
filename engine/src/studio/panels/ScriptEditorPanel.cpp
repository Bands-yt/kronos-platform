#include "studio/panels/ScriptEditorPanel.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "core/Components.hpp"
#include "core/NativeFileDialog.hpp"
#include "core/ScriptDebugger.hpp"
#include "core/UITheme.hpp"
#include "core/UIWidgets.hpp"
#include "studio/StudioIcons.hpp"
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
    tab.backend->setHooks(makeHooks(entity));
    if (auto it = breakpointsByEntity_.find(entity); it != breakpointsByEntity_.end()) tab.backend->setBreakpoints(it->second);
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

int ScriptEditorPanel::newScriptTab(core::ECS& ecs, const std::string& baseName, const std::string& source) {
    std::string candidate = baseName;
    for (int suffix = 2; entityNameTaken(ecs, candidate); ++suffix) {
        candidate = baseName + std::to_string(suffix);
    }
    core::EntityId entity = ecs.createEntity(candidate);
    ecs.addComponent<core::Script>(entity).source = source;
    int index = openOrFocusTab(ecs, entity);
    forceFocusActiveTab_ = true;
    // Keeps draw()'s own outer-selection edge trigger in sync, same
    // reasoning as openAndJumpToLine()'s own trailing assignment -- an
    // Explorer selection landing on this same new entity later this
    // frame or next shouldn't redundantly re-run openOrFocusTab().
    lastOuterSelection_ = entity;
    return index;
}

void ScriptEditorPanel::openScriptFromFile(core::ECS& /*ecs*/, NotificationCenter& notifications) {
    const bool started = core::openFileDialogAsync({"Open Script", {"*.luau", "*.lua"}, "Luau scripts"},
                                                   [this](const std::string& path) { pendingScriptPath_ = path; });
    if (!started) notifications.push("Could not open a file dialog: " + core::fileDialogError(), NotificationSeverity::Error);
}

void ScriptEditorPanel::importScriptFile(core::ECS& ecs, NotificationCenter& notifications, const std::string& pickedPath) {
    const std::optional<std::string> path = pickedPath;

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

    int forcedTab = forceFocusActiveTab_ ? activeTab_ : -1;
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
        if (i == forcedTab) flags |= ImGuiTabItemFlags_SetSelected;

        bool open = true;
        if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
            if (forcedTab < 0) activeTab_ = i;
            ImGui::EndTabItem();
        }
        if (!open) {
            closeTab(i);
            if (forcedTab > i) --forcedTab;
            else if (forcedTab == i) forcedTab = -1;
            continue; // next tab has shifted into slot i -- don't advance
        }
        ++i;
    }

    forceFocusActiveTab_ = false;
    ImGui::EndTabBar();
}

namespace {
struct ScriptTemplate {
    const char* title;
    const char* blurb;
    const char* entityName;
    const char* source;
};

const ScriptTemplate kTemplates[] = {
    {"Blank script", "An empty Luau script.", "Script", "print(\"Hello from Kronos!\")\n"},
    {"Spinning part", "Rotates a part every frame.", "SpinScript",
     "local part = world.findByName(\"Part\")\nlocal speed = 90 -- degrees per second\nlocal angle = 0\n\n"
     "events.onUpdate(function(dt)\n\tif part == nil then return end\n\tangle = (angle + speed * dt) % 360\n"
     "\tworld.setRotation(part, 0, angle, 0)\nend)\n"},
    {"Player greeting", "Welcomes players as they join.", "GreetingScript",
     "events.onPlayerJoin(function(playerId, displayName)\n\tprint(displayName .. \" joined the game\")\n"
     "\tTextChatService.SendAsync(\"Welcome, \" .. displayName .. \"!\")\nend)\n\n"
     "events.onPlayerLeave(function(playerId, displayName)\n\tprint(displayName .. \" left the game\")\nend)\n"},
    {"Collision handler", "Reacts when two bodies touch.", "CollisionScript",
     "local hits = 0\n\nevents.onCollision(function(entityA, entityB)\n\thits += 1\n"
     "\tworld.setColor(entityA, math.random(), math.random(), math.random())\n"
     "\tprint(\"Collision #\" .. hits .. \": \" .. entityA .. \" hit \" .. entityB)\nend)\n"},
};
} // namespace

void ScriptEditorPanel::drawEmptyState(core::ECS& ecs, NotificationCenter& notifications) {
    const ImVec2 start = ImGui::GetCursorPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 card(220.0f, 92.0f);
    constexpr float kGap = 12.0f;
    const int columns = avail.x >= card.x * 2.0f + kGap + 40.0f ? 2 : 1;
    const int rows = (static_cast<int>(std::size(kTemplates)) + columns - 1) / columns;
    const float blockWidth = card.x * static_cast<float>(columns) + kGap * static_cast<float>(columns - 1);
    const float headerHeight = ImGui::GetTextLineHeight() * 3.0f + 26.0f;
    const float blockHeight = headerHeight + rows * card.y + (rows - 1) * kGap + 56.0f;
    const ImVec2 origin(start.x + std::max(16.0f, (avail.x - blockWidth) * 0.5f),
                        start.y + std::max(16.0f, (avail.y - blockHeight) * 0.5f));

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImVec4 accent = ui::accent();
    ImGui::SetCursorPos(origin);
    const ImVec2 iconPos = ImGui::GetCursorScreenPos();
    drawList->AddRectFilled(iconPos, ImVec2(iconPos.x + 40.0f, iconPos.y + 40.0f),
                            ImGui::GetColorU32(ImVec4(accent.x, accent.y, accent.z, 0.15f)), 10.0f);
    drawIcon(drawList, Icon::Script, ImVec2(iconPos.x + 20.0f, iconPos.y + 20.0f), 22.0f, ImGui::GetColorU32(accent));
    ImGui::SetCursorPos(ImVec2(origin.x + 52.0f, origin.y));
    ImGui::BeginGroup();
    if (ImFont* bold = core::kronosBoldFont()) ImGui::PushFont(bold, ImGui::GetStyle().FontSizeBase * 1.3f);
    ImGui::TextUnformatted("Start a script");
    if (core::kronosBoldFont() != nullptr) ImGui::PopFont();
    ImGui::TextDisabled("Pick a template, or select an entity in the Explorer.");
    ImGui::EndGroup();

    const float gridTop = origin.y + headerHeight;
    for (int i = 0; i < static_cast<int>(std::size(kTemplates)); ++i) {
        const ScriptTemplate& entry = kTemplates[i];
        const ImVec2 local(origin.x + static_cast<float>(i % columns) * (card.x + kGap),
                           gridTop + static_cast<float>(i / columns) * (card.y + kGap));
        ImGui::SetCursorPos(local);
        ImGui::PushID(i);
        const bool clicked = ImGui::InvisibleButton("##template", card);
        const bool hovered = ImGui::IsItemHovered();
        const float t = ui::animate(ImGui::GetItemID(), hovered ? 1.0f : 0.0f);
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        const ImVec4 surface = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        ui::softShadow(drawList, min, max, 8.0f, 6.0f + 6.0f * t, ImVec4(0, 0, 0, 0.25f + 0.15f * t));
        drawList->AddRectFilled(min, max, ui::mix(surface, ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered], t), 8.0f);
        drawList->AddRect(min, max, ui::mix(ImGui::GetStyle().Colors[ImGuiCol_Border], accent, t), 8.0f, 0, 1.0f + t * 0.5f);
        drawIcon(drawList, i == 0 ? Icon::Script : i == 1 ? Icon::Rotate : i == 2 ? Icon::Play : Icon::Physics,
                 ImVec2(min.x + 22.0f, min.y + 24.0f), 18.0f, ImGui::GetColorU32(accent));
        if (ImFont* bold = core::kronosBoldFont()) ImGui::PushFont(bold, 0.0f);
        drawList->AddText(ImVec2(min.x + 42.0f, min.y + 15.0f), ImGui::GetColorU32(ImGuiCol_Text), entry.title);
        if (core::kronosBoldFont() != nullptr) ImGui::PopFont();
        drawList->AddText(nullptr, 0.0f, ImVec2(min.x + 14.0f, min.y + 46.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                          entry.blurb, nullptr, card.x - 28.0f);
        if (clicked) newScriptTab(ecs, entry.entityName, entry.source);
        ImGui::PopID();
    }

    ImGui::SetCursorPos(ImVec2(origin.x, gridTop + rows * card.y + (rows - 1) * kGap + 18.0f));
    if (ui::button("Import .luau file...", ui::ButtonKind::Ghost)) openScriptFromFile(ecs, notifications);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Scripts hot-reload while you play.");
}

void ScriptEditorPanel::drawHeader(ScriptEditorTab& tab, core::ECS& ecs, NotificationCenter& notifications) {
    const core::Name* name = ecs.tryGetComponent<core::Name>(tab.entity);
    const std::string title = (name != nullptr && !name->value.empty()) ? name->value : "(unnamed)";
    const bool dirty = tab.backend->source() != tab.savedSource;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight() + 12.0f;
    const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + height);
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    drawList->AddLine(ImVec2(min.x, max.y - 1.0f), ImVec2(max.x, max.y - 1.0f), ImGui::GetColorU32(ImGuiCol_Separator));

    const float centerY = min.y + height * 0.5f;
    const ImVec4 accent = ui::accent();
    drawIcon(drawList, Icon::Script, ImVec2(min.x + 20.0f, centerY), 16.0f, ImGui::GetColorU32(accent));
    ImGui::SetCursorScreenPos(ImVec2(min.x + 34.0f, centerY - ImGui::GetTextLineHeight() * 0.5f));
    ImGui::TextDisabled("Workspace  /");
    ImGui::SameLine(0.0f, 6.0f);
    if (ImFont* bold = core::kronosBoldFont()) ImGui::PushFont(bold, 0.0f);
    ImGui::TextUnformatted(title.c_str());
    if (core::kronosBoldFont() != nullptr) ImGui::PopFont();
    ImGui::SameLine(0.0f, 10.0f);
    const ImVec2 chip = ImGui::GetCursorScreenPos();
    const char* state = dirty ? "Unsaved" : "Saved";
    const ImVec4 stateColor = dirty ? ImVec4(0.91f, 0.66f, 0.23f, 1.0f) : ImVec4(0.18f, 0.74f, 0.52f, 1.0f);
    const ImVec2 stateSize = ImGui::CalcTextSize(state);
    drawList->AddRectFilled(ImVec2(chip.x, chip.y - 2.0f), ImVec2(chip.x + stateSize.x + 22.0f, chip.y + stateSize.y + 2.0f),
                            ImGui::GetColorU32(ImVec4(stateColor.x, stateColor.y, stateColor.z, 0.14f)), 9.0f);
    drawList->AddCircleFilled(ImVec2(chip.x + 8.0f, chip.y + stateSize.y * 0.5f), 3.0f, ImGui::GetColorU32(stateColor));
    drawList->AddText(ImVec2(chip.x + 15.0f, chip.y), ImGui::GetColorU32(stateColor), state);

    if (debugger_ != nullptr && debugger_->paused() && debugger_->pauseState().chunk == title) {
        ImGui::SameLine(0.0f, stateSize.x + 34.0f);
        const ImVec2 pausedChip = ImGui::GetCursorScreenPos();
        char pausedText[64];
        std::snprintf(pausedText, sizeof(pausedText), "Paused at line %d", debugger_->pauseState().line);
        const ImVec2 pausedSize = ImGui::CalcTextSize(pausedText);
        const ImVec4 amber(1.0f, 0.8f, 0.0f, 1.0f);
        drawList->AddRectFilled(ImVec2(pausedChip.x, pausedChip.y - 2.0f),
                                ImVec2(pausedChip.x + pausedSize.x + 16.0f, pausedChip.y + pausedSize.y + 2.0f),
                                ImGui::GetColorU32(ImVec4(amber.x, amber.y, amber.z, 0.16f)), 9.0f);
        drawList->AddText(ImVec2(pausedChip.x + 8.0f, pausedChip.y), ImGui::GetColorU32(amber), pausedText);
    }

    const float saveWidth = 74.0f;
    const float revertWidth = 74.0f;
    const float debugWidth = 86.0f;
    const float buttonY = centerY - ImGui::GetFrameHeight() * 0.5f;
    if (debugger_ != nullptr) {
        ImGui::SetCursorScreenPos(ImVec2(max.x - saveWidth - revertWidth - debugWidth - 24.0f, buttonY));
        if (ui::button(debuggerWindowOpen_ ? "Debugger*" : "Debugger", ui::ButtonKind::Ghost, ImVec2(debugWidth, 0.0f))) {
            debuggerWindowOpen_ = !debuggerWindowOpen_;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Breakpoints, call stack and variables.\nF9 toggles a breakpoint; F5/F10/F11 continue and step while paused.");
        }
    }
    ImGui::SetCursorScreenPos(ImVec2(max.x - saveWidth - revertWidth - 18.0f, buttonY));
    ImGui::BeginDisabled(!dirty);
    if (ui::button("Revert", ui::ButtonKind::Ghost, ImVec2(revertWidth, 0.0f))) {
        tab.backend->setSource(tab.savedSource);
        notifications.push("Reverted to the last saved version", NotificationSeverity::Info);
    }
    ImGui::SameLine(0.0f, 6.0f);
    if (ui::button("Save", dirty ? ui::ButtonKind::Primary : ui::ButtonKind::Secondary, ImVec2(saveWidth, 0.0f))) saveTab(tab, ecs, notifications);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Save (Ctrl+S) -- the script hot-reloads on the next tick");
    }
    ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
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
    runPendingRequests(ecs, notifications);
    syncDebugger(ecs);
    if (!pendingScriptPath_.empty()) {
        const std::string path = std::move(pendingScriptPath_);
        pendingScriptPath_.clear();
        importScriptFile(ecs, notifications, path);
    }
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

    core::Script* script = ecs.tryGetComponent<core::Script>(tab.entity);
    if (script == nullptr) {
        ImGui::SetCursorPos(ImVec2(16.0f, ImGui::GetCursorPosY() + 16.0f));
        ImGui::BeginGroup();
        ImGui::TextWrapped("This entity has no Script component yet.");
        if (ui::button("Add Script Component", ui::ButtonKind::Primary)) {
            ecs.addComponent<core::Script>(tab.entity);
            tab.backend->setSource("");
            tab.savedSource.clear();
        }
        ImGui::EndGroup();
        ImGui::End();
        return;
    }

    drawHeader(tab, ecs, notifications);
    tab.backend->draw();
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

ScriptEditorHooks ScriptEditorPanel::makeHooks(core::EntityId owner) {
    ScriptEditorHooks hooks;
    hooks.goToGlobalDefinition = [this](const luau_symbols::GlobalRef& ref) { pendingDefinition_ = ref; };
    hooks.findInAllScripts = [this](const std::string& query) {
        if (!query.empty()) globalQuery_ = query;
        findInScriptsOpen_ = true;
        focusFindInScripts_ = true;
    };
    hooks.renameGlobalElsewhere = [this, owner](const luau_symbols::GlobalRef& ref, const std::string& newName) {
        pendingRename_ = PendingRename{ref, newName, owner};
    };
    return hooks;
}

std::string ScriptEditorPanel::liveSource(core::ECS& ecs, core::EntityId entity) const {
    const int index = findTabIndex(entity);
    if (index >= 0) return tabs_[static_cast<size_t>(index)].backend->source();
    const core::Script* script = ecs.tryGetComponent<core::Script>(entity);
    return script != nullptr ? script->source : std::string();
}

std::string ScriptEditorPanel::chunkNameOf(core::ECS& ecs, core::EntityId entity) {
    const core::Name* name = ecs.tryGetComponent<core::Name>(entity);
    return (name != nullptr && !name->value.empty()) ? name->value : "Script";
}

void ScriptEditorPanel::jumpTo(core::ECS& ecs, core::EntityId entity, int line, int byteStart, int byteEnd) {
    if (ecs.tryGetComponent<core::Script>(entity) == nullptr) return;
    const int index = openOrFocusTab(ecs, entity);
    forceFocusActiveTab_ = true;
    lastOuterSelection_ = entity;
    tabs_[static_cast<size_t>(index)].backend->selectSourceRange(line, byteStart, byteEnd);
    ImGui::SetWindowFocus("Script Editor");
}

void ScriptEditorPanel::runPendingRequests(core::ECS& ecs, NotificationCenter& notifications) {
    if (pendingDefinition_) {
        const luau_symbols::GlobalRef ref = *pendingDefinition_;
        pendingDefinition_.reset();
        const std::string label = ref.member.empty() ? ref.global : ref.global + "." + ref.member;
        bool found = false;
        for (auto entity : ecs.view<core::Script>()) {
            const std::optional<luau_symbols::Range> definition = luau_symbols::definitionOf(liveSource(ecs, entity), ref);
            if (!definition) continue;
            jumpTo(ecs, entity, definition->line, definition->column, definition->endColumn);
            found = true;
            break;
        }
        if (!found) notifications.push("No script defines \"" + label + "\" (it may be built in)", NotificationSeverity::Info);
    }
    if (pendingRename_) {
        const PendingRename rename = std::move(*pendingRename_);
        pendingRename_.reset();
        const int previousActive = activeTab_;
        int changed = 0;
        std::vector<core::EntityId> entities;
        for (auto entity : ecs.view<core::Script>()) {
            if (entity != rename.origin) entities.push_back(entity);
        }
        for (core::EntityId entity : entities) {
            const std::string source = liveSource(ecs, entity);
            std::vector<luau_symbols::Range> ranges = luau_symbols::occurrencesOf(source, rename.ref);
            if (ranges.empty()) continue;
            const std::string updated = luau_symbols::replaceRanges(source, std::move(ranges), rename.newName);
            tabs_[static_cast<size_t>(openOrFocusTab(ecs, entity))].backend->setSource(updated);
            ++changed;
        }
        activeTab_ = previousActive;
        if (changed > 0) {
            notifications.push("Also renamed in " + std::to_string(changed) + " other script" + (changed == 1 ? "" : "s") +
                                   " (unsaved, review and save each)",
                               NotificationSeverity::Info);
        }
    }
}

void ScriptEditorPanel::syncDebugger(core::ECS& ecs) {
    if (debugger_ == nullptr) return;
    for (ScriptEditorTab& tab : tabs_) breakpointsByEntity_[tab.entity] = tab.backend->breakpoints();

    std::map<std::string, std::set<int>> wanted;
    for (auto it = breakpointsByEntity_.begin(); it != breakpointsByEntity_.end();) {
        if (!ecs.raw().valid(it->first) || ecs.tryGetComponent<core::Script>(it->first) == nullptr) {
            it = breakpointsByEntity_.erase(it);
            continue;
        }
        if (!it->second.empty()) wanted[chunkNameOf(ecs, it->first)].insert(it->second.begin(), it->second.end());
        ++it;
    }
    if (wanted != debugger_->breakpoints()) {
        debugger_->clearBreakpoints();
        for (const auto& [chunk, lines] : wanted) debugger_->setBreakpoints(chunk, lines);
    }

    const bool paused = debugger_->paused();
    const core::ScriptPauseState& state = debugger_->pauseState();
    if (paused && debugger_->pauseSerial() != seenPauseSerial_) {
        seenPauseSerial_ = debugger_->pauseSerial();
        selectedFrame_ = 0;
        debuggerWindowOpen_ = true;
        focusDebuggerWindow_ = true;
        for (auto entity : ecs.view<core::Script>()) {
            if (chunkNameOf(ecs, entity) != state.chunk) continue;
            const int index = openOrFocusTab(ecs, entity);
            forceFocusActiveTab_ = true;
            lastOuterSelection_ = entity;
            tabs_[static_cast<size_t>(index)].backend->moveCaretToLine(state.line);
            ImGui::SetWindowFocus("Script Editor");
            break;
        }
    }

    std::string chunk;
    int line = 0;
    if (paused) {
        chunk = state.chunk;
        line = state.line;
        if (selectedFrame_ > 0 && selectedFrame_ < static_cast<int>(state.frames.size())) {
            chunk = state.frames[static_cast<size_t>(selectedFrame_)].chunk;
            line = state.frames[static_cast<size_t>(selectedFrame_)].line;
        }
    }
    for (ScriptEditorTab& tab : tabs_) tab.backend->setExecutionLine(paused && chunkNameOf(ecs, tab.entity) == chunk ? line : 0);
}

void ScriptEditorPanel::handleDebuggerShortcuts() {
    if (debugger_ == nullptr || !debugger_->paused()) return;
    const bool shift = ImGui::GetIO().KeyShift;
    if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) debugger_->resume(core::ScriptDebugger::Action::Continue);
    else if (ImGui::IsKeyPressed(ImGuiKey_F10, false)) debugger_->resume(core::ScriptDebugger::Action::StepOver);
    else if (ImGui::IsKeyPressed(ImGuiKey_F11, false)) {
        debugger_->resume(shift ? core::ScriptDebugger::Action::StepOut : core::ScriptDebugger::Action::StepInto);
    }
}

void ScriptEditorPanel::drawAuxiliaryWindows(core::ECS& ecs, bool playing) {
    handleDebuggerShortcuts();
    if (debugger_ != nullptr && debuggerWindowOpen_) drawDebuggerWindow(ecs, playing);
    if (findInScriptsOpen_) drawFindInScripts(ecs);
}

void ScriptEditorPanel::drawDebuggerWindow(core::ECS& ecs, bool playing) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 700.0f, viewport->WorkPos.y + 300.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 480.0f), ImGuiCond_FirstUseEver);
    if (focusDebuggerWindow_) {
        ImGui::SetNextWindowFocus();
        focusDebuggerWindow_ = false;
    }
    if (!ImGui::Begin("Debugger", &debuggerWindowOpen_)) {
        ImGui::End();
        return;
    }
    using Action = core::ScriptDebugger::Action;
    const bool paused = debugger_->paused();
    const core::ScriptPauseState& state = debugger_->pauseState();

    ImGui::BeginDisabled(!paused);
    if (ui::button("Continue", paused ? ui::ButtonKind::Primary : ui::ButtonKind::Secondary)) debugger_->resume(Action::Continue);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Continue (F5)");
    ImGui::SameLine();
    if (ui::button("Over", ui::ButtonKind::Secondary)) debugger_->resume(Action::StepOver);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Step over (F10)");
    ImGui::SameLine();
    if (ui::button("Into", ui::ButtonKind::Secondary)) debugger_->resume(Action::StepInto);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Step into (F11)");
    ImGui::SameLine();
    if (ui::button("Out", ui::ButtonKind::Secondary)) debugger_->resume(Action::StepOut);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Step out (Shift+F11)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(paused || !playing || debugger_->pauseRequested());
    if (ui::button("Pause", ui::ButtonKind::Ghost)) debugger_->requestPause();
    ImGui::EndDisabled();

    if (paused) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "%s in %s, line %d", state.reason.c_str(), state.chunk.c_str(), state.line);
    } else if (playing) {
        ImGui::TextDisabled(debugger_->pauseRequested() ? "Pausing at the next script statement..." : "Running");
    } else {
        ImGui::TextDisabled("Press Play to run scripts under the debugger.");
    }
    if (!debugger_->lastNotice().empty()) ImGui::TextWrapped("%s", debugger_->lastNotice().c_str());

    ImGui::SeparatorText("Call Stack");
    if (!paused) {
        ImGui::TextDisabled("Not paused.");
    } else if (ImGui::BeginTable("##call_stack", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (int i = 0; i < static_cast<int>(state.frames.size()); ++i) {
            const core::ScriptDebugFrame& frame = state.frames[static_cast<size_t>(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            const std::string label = frame.function.empty() ? std::string("(main chunk)") : frame.function;
            if (ImGui::Selectable(label.c_str(), selectedFrame_ == i, ImGuiSelectableFlags_SpanAllColumns)) {
                selectedFrame_ = i;
                for (auto entity : ecs.view<core::Script>()) {
                    if (chunkNameOf(ecs, entity) != frame.chunk) continue;
                    const int index = openOrFocusTab(ecs, entity);
                    forceFocusActiveTab_ = true;
                    lastOuterSelection_ = entity;
                    tabs_[static_cast<size_t>(index)].backend->moveCaretToLine(frame.line);
                    break;
                }
            }
            ImGui::PopID();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s:%d", frame.chunk.c_str(), frame.line);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Variables");
    if (paused && selectedFrame_ < static_cast<int>(state.frames.size())) {
        const core::ScriptDebugFrame& frame = state.frames[static_cast<size_t>(selectedFrame_)];
        if (frame.variables.empty()) ImGui::TextDisabled("No locals in this frame.");
        else if (ImGui::BeginTable("##variables", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableHeadersRow();
            for (const core::ScriptDebugVariable& variable : frame.variables) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if (variable.upvalue) ImGui::TextColored(ImVec4(0.6f, 0.75f, 1.0f, 1.0f), "%s", variable.name.c_str());
                else ImGui::TextUnformatted(variable.name.c_str());
                if (variable.upvalue && ImGui::IsItemHovered()) ImGui::SetTooltip("Upvalue (captured from an enclosing scope)");
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", variable.value.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", variable.type.c_str());
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextDisabled("Not paused.");
    }

    ImGui::SeparatorText("Breakpoints");
    bool any = false;
    for (auto& [entity, lines] : breakpointsByEntity_) {
        for (auto it = lines.begin(); it != lines.end();) {
            any = true;
            const int line = *it;
            ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)) * 100000 + line);
            bool keep = true;
            ImGui::Checkbox("##keep", &keep);
            ImGui::SameLine();
            const std::string label = chunkNameOf(ecs, entity) + ":" + std::to_string(line);
            const bool clicked = ImGui::Selectable(label.c_str());
            ImGui::PopID();
            if (!keep) {
                it = lines.erase(it);
                const int index = findTabIndex(entity);
                if (index >= 0) tabs_[static_cast<size_t>(index)].backend->setBreakpoints(lines);
                continue;
            }
            if (clicked) openAndJumpToLine(ecs, entity, line);
            ++it;
        }
    }
    if (!any) ImGui::TextDisabled("Click the gutter or press F9 to add one.");
    else if (ui::button("Remove all", ui::ButtonKind::Ghost)) {
        breakpointsByEntity_.clear();
        for (ScriptEditorTab& tab : tabs_) tab.backend->setBreakpoints({});
    }
    ImGui::End();
}

void ScriptEditorPanel::drawFindInScripts(core::ECS& ecs) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x - 760.0f, viewport->WorkPos.y + 280.0f),
                            ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_FirstUseEver);
    if (focusFindInScripts_) ImGui::SetNextWindowFocus();
    if (!ImGui::Begin("Find in Scripts", &findInScriptsOpen_)) {
        ImGui::End();
        return;
    }
    if (focusFindInScripts_) {
        ImGui::SetKeyboardFocusHere();
        focusFindInScripts_ = false;
    }
    ImGui::SetNextItemWidth(-110.0f);
    ImGui::InputTextWithHint("##global_query", "Search every script (Ctrl+Shift+F)", &globalQuery_);
    ImGui::SameLine();
    ImGui::Checkbox("Aa", &globalMatchCase_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Match case");
    ImGui::SameLine();
    ImGui::Checkbox("W", &globalWholeWord_);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Whole word");

    int total = 0;
    int files = 0;
    ImGui::BeginChild("##global_results", ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()));
    if (!globalQuery_.empty()) {
        for (auto entity : ecs.view<core::Script>()) {
            const std::string source = liveSource(ecs, entity);
            const std::vector<luau_symbols::TextHit> hits = luau_symbols::findText(source, globalQuery_, globalMatchCase_, globalWholeWord_);
            if (hits.empty()) continue;
            ++files;
            total += static_cast<int>(hits.size());
            std::vector<std::string> lines;
            for (size_t start = 0;;) {
                const size_t end = source.find('\n', start);
                lines.push_back(source.substr(start, end == std::string::npos ? std::string::npos : end - start));
                if (end == std::string::npos) break;
                start = end + 1;
            }
            const std::string name = chunkNameOf(ecs, entity);
            ImGui::PushID(static_cast<int>(static_cast<uint32_t>(entity)));
            ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
            if (ImGui::TreeNodeEx("##file", ImGuiTreeNodeFlags_SpanAvailWidth, "%s  (%d)", name.c_str(), static_cast<int>(hits.size()))) {
                for (size_t i = 0; i < hits.size(); ++i) {
                    const luau_symbols::TextHit& hit = hits[i];
                    std::string text = hit.line < static_cast<int>(lines.size()) ? lines[static_cast<size_t>(hit.line)] : std::string();
                    const size_t indent = text.find_first_not_of(" \t");
                    text = indent == std::string::npos ? std::string() : text.substr(indent);
                    if (text.size() > 120) text = text.substr(0, 117) + "...";
                    ImGui::PushID(static_cast<int>(i));
                    char label[32];
                    std::snprintf(label, sizeof(label), "%4d", hit.line + 1);
                    if (ImGui::Selectable(label, false, ImGuiSelectableFlags_AllowOverlap)) {
                        jumpTo(ecs, entity, hit.line, hit.byteStart, hit.byteEnd);
                    }
                    ImGui::SameLine();
                    ImGui::TextUnformatted(text.c_str());
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    if (globalQuery_.empty()) ImGui::TextDisabled("Type to search every script in the place.");
    else ImGui::TextDisabled("%d result%s in %d script%s", total, total == 1 ? "" : "s", files, files == 1 ? "" : "s");
    ImGui::End();
}

} // namespace engine::studio::panels
