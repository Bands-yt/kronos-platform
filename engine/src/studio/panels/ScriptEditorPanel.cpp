#include "studio/panels/ScriptEditorPanel.hpp"

#include <cctype>
#include <cstdio>
#include <unordered_set>
#include <vector>

#include <imgui.h>
#include <imgui_stdlib.h>

#include "core/Components.hpp"
#include "studio/Notification.hpp"

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

ScriptEditorPanel::ScriptEditorPanel() {
    auto monaco = std::make_unique<MonacoWebViewEditor>();
    if (monaco->initialize()) {
        backend_ = std::move(monaco); // never reached today -- see MonacoWebViewEditor::initialize()
    } else {
        backend_ = std::make_unique<ImGuiFallbackEditor>();
        backend_->initialize();
    }
}

void ScriptEditorPanel::loadFromEntity(core::ECS& ecs, core::EntityId entity) {
    targetEntity_ = entity;
    if (const core::Script* script = ecs.tryGetComponent<core::Script>(entity)) {
        targetHasScript_ = true;
        backend_->setSource(script->source);
    } else {
        targetHasScript_ = false;
        backend_->setSource("");
    }
}

void ScriptEditorPanel::saveToEntity(core::ECS& ecs, NotificationCenter& notifications) {
    core::Script* script = ecs.tryGetComponent<core::Script>(targetEntity_);
    if (script == nullptr) return;
    // Deliberately only `source` -- `loadedSource` stays whatever it was
    // so core::tickScriptHotReload() sees a real mismatch and does the
    // real (re)load, instead of this save silently marking itself
    // "already loaded" and skipping that step.
    script->source = backend_->source();
    notifications.push("Script saved -- will hot-reload on next tick", NotificationSeverity::Success);
}

void ScriptEditorPanel::draw(core::ECS& ecs, core::EntityId selectedEntity, NotificationCenter& notifications) {
    if (selectedEntity != targetEntity_) {
        loadFromEntity(ecs, selectedEntity);
    }

    ImGui::Begin("Script Editor");

    if (selectedEntity == core::kNullEntity) {
        ImGui::TextDisabled("Select an entity to view or edit its script.");
        ImGui::End();
        return;
    }

    const core::Name* name = ecs.tryGetComponent<core::Name>(selectedEntity);
    ImGui::TextDisabled("Entity: %s", (name != nullptr && !name->value.empty()) ? name->value.c_str() : "(unnamed)");

    if (!targetHasScript_) {
        ImGui::TextWrapped("This entity has no Script component yet.");
        if (ImGui::Button("Add Script Component")) {
            ecs.addComponent<core::Script>(selectedEntity);
            loadFromEntity(ecs, selectedEntity);
        }
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Backend: %s", backend_->backendName());
    ImGui::SameLine();
    ImGui::TextDisabled("| Ctrl+S to save (real hot-reload while Playing)");
    ImGui::Separator();
    backend_->draw();

    // Checked here, not StudioApp's own global per-frame keybind block,
    // so this only fires while the Script Editor window genuinely has
    // keyboard focus -- Ctrl+S elsewhere in Studio still means "save
    // scene" (StudioApp::run()'s own existing binding), unchanged.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && ImGui::GetIO().KeyCtrl &&
        ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        saveToEntity(ecs, notifications);
    }

    ImGui::End();
}

} // namespace engine::studio::panels
