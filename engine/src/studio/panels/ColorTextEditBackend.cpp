#include "studio/panels/ColorTextEditBackend.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <TextEditor.h>

#include "Luau/Autocomplete.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Frontend.h"
#include "Luau/ToString.h"
#include "Luau/TypeArena.h"

#include "core/ResourcePaths.hpp"
#include "core/UITheme.hpp"
#include "core/UIWidgets.hpp"
#include "studio/StudioIcons.hpp"

namespace engine::studio::panels {

namespace {

constexpr const char* kBufferModuleName = "=script";
constexpr int kTabSize = 4;

struct EditorPrefs {
    int theme = 0;
    float zoom = 1.0f;
    bool whitespace = false;
    bool problemsOpen = true;
    bool autoSuggest = true;
};
EditorPrefs g_prefs;

struct BufferFileResolver final : Luau::FileResolver {
    std::string source;

    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override {
        if (name != kBufferModuleName) return std::nullopt;
        return Luau::SourceCode{source, Luau::SourceCode::Script};
    }
};

constexpr ImU32 rgb(uint32_t hex, uint32_t alpha = 0xFF) {
    return (alpha << 24) | ((hex & 0xFF) << 16) | (hex & 0xFF00) | ((hex >> 16) & 0xFF);
}

struct ThemeInfo {
    const char* name;
    TextEditor::Palette palette;
};

const std::array<ThemeInfo, 3>& themes() {
    static const std::array<ThemeInfo, 3> kThemes = {{
        {"Kronos Night",
         {{rgb(0xC9D1D9), rgb(0xC678DD), rgb(0xE5A15B), rgb(0x98C379), rgb(0x98C379), rgb(0x8A93A3), rgb(0xE06C75),
           rgb(0xD7DCE2), rgb(0x4EA8DE), rgb(0xE06C75), rgb(0x5C6773), rgb(0x5C6773), rgb(0x0F1218), rgb(0x4EA8DE),
           rgb(0x2A4A6E, 0xC0), rgb(0xE5534B, 0x50), rgb(0xE5534B, 0x40), rgb(0x434B58), rgb(0xFFFFFF, 0x0A),
           rgb(0xFFFFFF, 0x05), rgb(0xFFFFFF, 0x12)}}},
        {"Midnight",
         {{rgb(0xA9B1D6), rgb(0xBB9AF7), rgb(0xFF9E64), rgb(0x9ECE6A), rgb(0x9ECE6A), rgb(0x89DDFF), rgb(0xF7768E),
           rgb(0xC0CAF5), rgb(0x7DCFFF), rgb(0xF7768E), rgb(0x565F89), rgb(0x565F89), rgb(0x1A1B26), rgb(0xC0CAF5),
           rgb(0x33467C, 0xC0), rgb(0xF7768E, 0x50), rgb(0xF7768E, 0x40), rgb(0x3B4261), rgb(0x292E42, 0xA0),
           rgb(0x292E42, 0x60), rgb(0x3B4261, 0x80)}}},
        {"Daylight",
         {{rgb(0x24292F), rgb(0xCF222E), rgb(0x0550AE), rgb(0x0A3069), rgb(0x0A3069), rgb(0x57606A), rgb(0x953800),
           rgb(0x24292F), rgb(0x8250DF), rgb(0x953800), rgb(0x6E7781), rgb(0x6E7781), rgb(0xFAFBFC), rgb(0x24292F),
           rgb(0x0969DA, 0x40), rgb(0xCF222E, 0x40), rgb(0xCF222E, 0x30), rgb(0x8C959F), rgb(0x000000, 0x08),
           rgb(0x000000, 0x04), rgb(0x000000, 0x10)}}},
    }};
    return kThemes;
}

std::string kronosGlobalDefinitionsPath() {
    return core::resolveResourceDir(core::executableDirectory(), "assets", ENGINE_ASSET_DIR) + "/luau/kronos_globals.d.lua";
}

std::optional<std::string> readFileToString(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

const std::unordered_map<std::string, std::string>& apiDocs() {
    static const std::unordered_map<std::string, std::string> kDocs = [] {
        std::optional<std::string> source = readFileToString(kronosGlobalDefinitionsPath());
        return source ? ColorTextEditBackend::parseApiDocs(*source) : std::unordered_map<std::string, std::string>{};
    }();
    return kDocs;
}

TextEditor::LanguageDefinition luauLanguageDefinition() {
    static bool inited = false;
    static TextEditor::LanguageDefinition langDef;
    if (!inited) {
        static const char* const keywords[] = {
            "and", "break", "do",       "else",  "elseif", "end",    "false", "for",     "function", "if",   "in",
            "local", "nil", "not",      "or",    "repeat", "return", "then",  "true",    "until",    "while", "continue",
        };
        for (const char* keyword : keywords) langDef.mKeywords.insert(keyword);

        static const std::pair<const char*, const char*> globals[] = {
            {"world", "world -- entities, transforms, physics and animation"},
            {"events", "events -- update, collision, interaction and player callbacks"},
            {"task", "task -- wait, spawn and defer coroutines"},
            {"ui", "ui -- immediate-mode on-screen drawing"},
            {"network", "network -- client/server remote events"},
            {"avatar", "avatar -- player avatar emotes"},
            {"audio", "audio -- sounds, mixer buses and snapshots"},
            {"engine", "engine -- logging"},
            {"TextChatService", "TextChatService -- send and receive chat"},
            {"print", "print(...) -- writes to the Engine Console"},
            {"require", "require(path) -- loads a module script"},
            {"math", "math -- Luau math library"},
            {"string", "string -- Luau string library"},
            {"table", "table -- Luau table library"},
            {"coroutine", "coroutine -- Luau coroutine library"},
            {"pairs", "pairs(t) -- iterates every key/value of a table"},
            {"ipairs", "ipairs(t) -- iterates the array part of a table"},
            {"tostring", "tostring(value) -> string"},
            {"tonumber", "tonumber(value) -> number?"},
            {"typeof", "typeof(value) -> string"},
            {"type", "type(value) -> string"},
            {"pcall", "pcall(fn, ...) -> (boolean, ...) -- calls fn, catching errors"},
            {"error", "error(message) -- raises an error"},
            {"assert", "assert(value, message?) -- errors when value is falsy"},
            {"select", "select(n, ...) -- picks from a variadic list"},
            {"setmetatable", "setmetatable(t, mt) -> t"},
            {"getmetatable", "getmetatable(t) -> table?"},
        };
        for (const auto& [name, declaration] : globals) {
            TextEditor::Identifier identifier;
            identifier.mDeclaration = declaration;
            langDef.mIdentifiers.insert(std::make_pair(std::string(name), identifier));
        }

        langDef.mTokenRegexStrings.emplace_back("\"(\\\\.|[^\"])*\"", TextEditor::PaletteIndex::String);
        langDef.mTokenRegexStrings.emplace_back("\'(\\\\.|[^\'])*\'", TextEditor::PaletteIndex::String);
        langDef.mTokenRegexStrings.emplace_back("0[xX][0-9a-fA-F_]+", TextEditor::PaletteIndex::Number);
        langDef.mTokenRegexStrings.emplace_back("[+-]?([0-9][0-9_]*([.][0-9_]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?",
                                                  TextEditor::PaletteIndex::Number);
        langDef.mTokenRegexStrings.emplace_back("[a-zA-Z_][a-zA-Z0-9_]*", TextEditor::PaletteIndex::Identifier);
        langDef.mTokenRegexStrings.emplace_back("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.\\:\\#]",
                                                  TextEditor::PaletteIndex::Punctuation);

        langDef.mCommentStart = "--[[";
        langDef.mCommentEnd = "]]";
        langDef.mSingleLineComment = "--";
        langDef.mCaseSensitive = true;
        langDef.mAutoIndentation = true;
        langDef.mName = "Luau";
        inited = true;
    }
    return langDef;
}

struct SnippetDef {
    const char* label;
    const char* filter;
    const char* body;
};

const SnippetDef kSnippets[] = {
    {"function name() ... end", "function", "function ${1:name}()\n\t\nend"},
    {"local function name() ... end", "local function", "local function ${1:name}()\n\t\nend"},
    {"if ... then ... end", "if", "if ${1:condition} then\n\t\nend"},
    {"if ... then ... else ... end", "ifelse", "if ${1:condition} then\n\t\nelse\n\t\nend"},
    {"for i = 1, n do ... end", "for", "for ${1:i} = 1, 10 do\n\t\nend"},
    {"for k, v in pairs(t) do ... end", "forpairs", "for key, value in pairs(${1:t}) do\n\t\nend"},
    {"for i, v in ipairs(t) do ... end", "foripairs", "for index, value in ipairs(${1:t}) do\n\t\nend"},
    {"while ... do ... end", "while", "while ${1:condition} do\n\t\nend"},
    {"repeat ... until ...", "repeat", "repeat\n\t\nuntil ${1:condition}"},
    {"events.onUpdate(function(dt) ... end)", "onUpdate", "events.onUpdate(function(dt)\n\t$0\nend)"},
    {"events.onCollision(function(a, b) ... end)", "onCollision",
     "events.onCollision(function(entityA, entityB)\n\t$0\nend)"},
    {"events.onInteract(function(entity, player) ... end)", "onInteract",
     "events.onInteract(function(entity, interactor)\n\t$0\nend)"},
    {"events.onPlayerJoin(function(id, name) ... end)", "onPlayerJoin",
     "events.onPlayerJoin(function(playerId, displayName)\n\t$0\nend)"},
    {"task.spawn(function() ... end)", "spawn", "task.spawn(function()\n\t$0\nend)"},
};

Luau::ToStringOptions namedArguments() {
    Luau::ToStringOptions options;
    options.functionTypeArguments = true;
    return options;
}

bool isIdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

ImVec4 kindColor(ColorTextEditBackend::CompletionKind kind) {
    using K = ColorTextEditBackend::CompletionKind;
    switch (kind) {
        case K::Keyword: return ImVec4(0.91f, 0.66f, 0.23f, 1.0f);
        case K::Function:
        case K::Method: return ImVec4(0.73f, 0.52f, 0.92f, 1.0f);
        case K::Property: return ImVec4(0.30f, 0.76f, 0.78f, 1.0f);
        case K::Table: return ImVec4(0.93f, 0.78f, 0.35f, 1.0f);
        case K::Type: return ImVec4(0.45f, 0.80f, 0.55f, 1.0f);
        case K::Snippet: return ImVec4(0.18f, 0.74f, 0.52f, 1.0f);
        case K::Module: return ImVec4(0.61f, 0.63f, 0.67f, 1.0f);
        case K::Variable:
        default: return ImVec4(0.31f, 0.66f, 0.87f, 1.0f);
    }
}

const char* kindGlyph(ColorTextEditBackend::CompletionKind kind) {
    using K = ColorTextEditBackend::CompletionKind;
    switch (kind) {
        case K::Keyword: return "K";
        case K::Function: return "F";
        case K::Method: return "M";
        case K::Property: return "P";
        case K::Table: return "T";
        case K::Type: return "Y";
        case K::Snippet: return "S";
        case K::Module: return "R";
        case K::Variable:
        default: return "V";
    }
}

const char* kindName(ColorTextEditBackend::CompletionKind kind) {
    using K = ColorTextEditBackend::CompletionKind;
    switch (kind) {
        case K::Keyword: return "keyword";
        case K::Function: return "function";
        case K::Method: return "method";
        case K::Property: return "property";
        case K::Table: return "table";
        case K::Type: return "type";
        case K::Snippet: return "snippet";
        case K::Module: return "module";
        case K::Variable:
        default: return "variable";
    }
}

void drawBadge(ImDrawList* drawList, ImVec2 min, float size, const ImVec4& color, const char* glyph) {
    const ImVec2 max(min.x + size, min.y + size);
    drawList->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.18f)), 3.0f);
    drawList->AddRect(min, max, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.55f)), 3.0f);
    const ImVec2 textSize = ImGui::CalcTextSize(glyph);
    drawList->AddText(ImVec2(min.x + (size - textSize.x) * 0.5f, min.y + (size - textSize.y) * 0.5f),
                      ImGui::GetColorU32(color), glyph);
}

void verticalSeparator() {
    ImGui::SameLine(0.0f, 6.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + 4.0f), ImVec2(pos.x, pos.y + height - 4.0f),
                                        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2(1.0f, height));
    ImGui::SameLine(0.0f, 6.0f);
}

bool toggleChip(const char* label, bool active, const char* tooltip) {
    const ImVec4 accent = ui::accent();
    ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(accent.x, accent.y, accent.z, 0.30f) : ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, active ? accent : ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    const bool clicked = ImGui::Button(label);
    ImGui::PopStyleColor(2);
    if (tooltip != nullptr && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

} // namespace

struct LuauLiveAnalyzer {
    BufferFileResolver fileResolver;
    Luau::NullConfigResolver configResolver;
    Luau::Frontend frontend{&fileResolver, &configResolver};

    LuauLiveAnalyzer() {
        // Autocomplete type-checks against its own global scope, so both need the builtins and the Kronos API.
        Luau::registerBuiltinGlobals(frontend, frontend.globals);
        Luau::registerBuiltinGlobals(frontend, frontend.globalsForAutocomplete, /*typeCheckForAutocomplete*/ true);
        loadKronosGlobalDefinitions(frontend.globals);
        loadKronosGlobalDefinitions(frontend.globalsForAutocomplete);
        // Must follow loadDefinitionFile(), which allocates into globalTypes.
        Luau::freeze(frontend.globals.globalTypes);
        Luau::freeze(frontend.globalsForAutocomplete.globalTypes);
    }

    void loadKronosGlobalDefinitions(Luau::GlobalTypes& globals) {
        const std::string path = kronosGlobalDefinitionsPath();
        std::optional<std::string> source = readFileToString(path);
        if (!source) {
            std::fprintf(stderr, "ColorTextEditBackend: could not read Kronos global definitions at \"%s\".\n", path.c_str());
            return;
        }
        Luau::LoadDefinitionFileResult result = frontend.loadDefinitionFile(
            globals, globals.globalScope, *source, "kronos_globals", /*captureComments*/ false, /*typeCheckForAutocomplete*/ &globals == &frontend.globalsForAutocomplete);
        if (!result.success) {
            std::fprintf(stderr, "ColorTextEditBackend: \"%s\" failed to typecheck (%zu error(s)).\n", path.c_str(),
                         result.parseResult.errors.size());
        }
    }

    std::vector<ColorTextEditBackend::Diagnostic> analyze(const std::string& source) {
        fileResolver.source = source;
        frontend.markDirty(kBufferModuleName);

        Luau::FrontendOptions options;
        options.runLintChecks = true;
        Luau::LintOptions lint;
        lint.setDefaults();
        lint.enableWarning(Luau::LintWarning::Code_LocalUnused);
        lint.enableWarning(Luau::LintWarning::Code_FunctionUnused);
        lint.disableWarning(Luau::LintWarning::Code_UnknownGlobal);
        options.enabledLintWarnings = lint;
        Luau::CheckResult result = frontend.check(kBufferModuleName, options);

        std::vector<ColorTextEditBackend::Diagnostic> diagnostics;
        auto add = [&](const Luau::Location& location, std::string message, bool warning) {
            diagnostics.push_back({static_cast<int>(location.begin.line), static_cast<int>(location.begin.column),
                                   static_cast<int>(location.end.line), static_cast<int>(location.end.column),
                                   std::move(message), warning});
        };
        for (const Luau::TypeError& error : result.errors) add(error.location, Luau::toString(error), false);
        for (const Luau::LintWarning& error : result.lintResult.errors) add(error.location, error.text, false);
        for (const Luau::LintWarning& warning : result.lintResult.warnings) add(warning.location, warning.text, true);
        std::sort(diagnostics.begin(), diagnostics.end(), [](const auto& a, const auto& b) {
            return a.line != b.line ? a.line < b.line : a.warning < b.warning;
        });
        return diagnostics;
    }

    // line/column are 0-based; column is a byte offset, as Luau counts it.
    Luau::AutocompleteResult complete(const std::string& source, int line, int column) {
        fileResolver.source = source;
        frontend.markDirty(kBufferModuleName);
        Luau::FrontendOptions options;
        options.forAutocomplete = true;
        options.retainFullTypeGraphs = true;
        frontend.check(kBufferModuleName, options);
        auto noStringCompletions = [](const std::string&, std::optional<const Luau::ExternType*>,
                                       std::optional<std::string>) -> std::optional<Luau::AutocompleteEntryMap> {
            return std::nullopt;
        };
        return Luau::autocomplete(frontend, kBufferModuleName,
                                   Luau::Position{static_cast<unsigned>(line), static_cast<unsigned>(column)},
                                   noStringCompletions);
    }
};

ColorTextEditBackend::ColorTextEditBackend() = default;
ColorTextEditBackend::~ColorTextEditBackend() = default;

bool ColorTextEditBackend::initialize() {
    editor_ = std::make_unique<TextEditor>();
    editor_->SetLanguageDefinition(luauLanguageDefinition());
    editor_->SetImGuiChildIgnored(true);
    editor_->SetTabSize(kTabSize);
    analyzer_ = std::make_unique<LuauLiveAnalyzer>();
    applyThemeIfChanged();
    return true;
}

void ColorTextEditBackend::shutdown() {
    analyzer_.reset();
    editor_.reset();
}

void ColorTextEditBackend::setSource(const std::string& source) {
    editor_->SetText(source);
    reanalyze();
}

const std::string& ColorTextEditBackend::source() const {
    sourceCache_ = editor_->GetText();
    // GetText() terminates the last line with an extra '\n'; dropping it makes setSource/source round-trip.
    if (!sourceCache_.empty() && sourceCache_.back() == '\n') sourceCache_.pop_back();
    return sourceCache_;
}

void ColorTextEditBackend::moveCaretToLine(int oneBasedLine) {
    const int zeroBasedLine = std::clamp(oneBasedLine - 1, 0, std::max(0, editor_->GetTotalLines() - 1));
    const TextEditor::Coordinates target(zeroBasedLine, 0);
    editor_->SetCursorPosition(target);
    editor_->SetSelection(target, target);
    refocusEditor_ = true;
}

void ColorTextEditBackend::reanalyze() {
    ++textVersion_;
    lines_ = editor_->GetTextLines();
    diagnostics_ = analyzer_->analyze(editor_->GetText());
    symbols_ = findSymbols(lines_);
}

void ColorTextEditBackend::applyThemeIfChanged() {
    g_prefs.theme = std::clamp(g_prefs.theme, 0, static_cast<int>(themes().size()) - 1);
    if (appliedTheme_ == g_prefs.theme) return;
    editor_->SetPalette(themes()[static_cast<size_t>(g_prefs.theme)].palette);
    appliedTheme_ = g_prefs.theme;
}

ImVec2 ColorTextEditBackend::caretScreenPos() const {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    return ImVec2(editorOrigin_.x + textStart_ + static_cast<float>(cursor.mColumn) * charAdvance_.x,
                  editorOrigin_.y + static_cast<float>(cursor.mLine) * charAdvance_.y);
}

bool ColorTextEditBackend::caretVisible() const {
    const ImVec2 caret = caretScreenPos();
    return caret.y >= editorClipMin_.y - 1.0f && caret.y + charAdvance_.y <= editorClipMax_.y + 1.0f;
}

void ColorTextEditBackend::draw() {
    applyThemeIfChanged();
    editor_->SetShowWhitespaces(g_prefs.whitespace);

    const bool panelFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    const ImGuiWindow* navWindow = ImGui::GetCurrentContext()->NavWindow;
    const bool editorFocused = panelFocused && navWindow != nullptr && navWindow->ID == editorWindowId_;
    // ImGui's nav moves focus from a child to its parent on Escape before we
    // get here, so treat Escape as still belonging to the editor.
    const bool escapeFromEditor = editorWasFocused_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (escapeFromEditor) refocusEditor_ = true;
    editorWasFocused_ = editorFocused || escapeFromEditor;
    handleShortcuts(editorFocused || escapeFromEditor || (panelFocused && findOpen_));

    drawToolbar();
    if (findOpen_) drawFindBar();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float statusHeight = ImGui::GetFrameHeight();
    const float problemsHeight =
        (g_prefs.problemsOpen && !diagnostics_.empty()) ? std::min(150.0f, std::max(70.0f, avail.y * 0.28f)) : 0.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.y * (problemsHeight > 0.0f ? 2.0f : 1.0f);
    const float editorHeight = std::max(40.0f, avail.y - statusHeight - problemsHeight - spacing);
    drawEditor(ImVec2(avail.x, editorHeight));
    if (problemsHeight > 0.0f) drawProblems(problemsHeight);
    drawStatusBar();

    drawCompletionPopup();
    drawSignaturePopup();
    drawRenamePopup();
}

void ColorTextEditBackend::handleShortcuts(bool focused) {
    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = io.KeyCtrl;
    const bool popupVisible = showCompletions_ && !completionEntries_.empty();

    bool consumed = false;
    auto pressed = [&](ImGuiKey key, bool repeat = false) { return ImGui::IsKeyPressed(key, repeat); };

    if (focused && popupVisible) {
        const CompletionItem& selected = completionEntries_[static_cast<size_t>(completionSelected_)];
        const std::string& line = lines_.empty() ? std::string() : lines_[static_cast<size_t>(
                                                                         std::min<int>(completionAnchorLine_, static_cast<int>(lines_.size()) - 1))];
        const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
        const int cursorByte = columnToByte(line, cursor.mColumn, kTabSize);
        const std::string prefix =
            cursorByte > completionAnchorColumn_ ? line.substr(completionAnchorColumn_, cursorByte - completionAnchorColumn_) : "";
        const bool alreadyTyped = selected.kind != CompletionKind::Snippet && selected.insertText == prefix;
        if ((pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) && alreadyTyped) {
            showCompletions_ = false;
        } else if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter) || pressed(ImGuiKey_Tab)) {
            CompletionItem item = selected;
            editor_->SetHandleKeyboardInputs(false);
            insertCompletion(item);
            consumed = true;
        } else if (pressed(ImGuiKey_Escape)) {
            showCompletions_ = false;
            consumed = true;
        } else if (pressed(ImGuiKey_UpArrow, true)) {
            completionSelected_ = completionSelected_ == 0 ? static_cast<int>(completionEntries_.size()) - 1 : completionSelected_ - 1;
            consumed = true;
        } else if (pressed(ImGuiKey_DownArrow, true)) {
            completionSelected_ = (completionSelected_ + 1) % static_cast<int>(completionEntries_.size());
            consumed = true;
        } else if (pressed(ImGuiKey_PageDown)) {
            completionSelected_ = std::min(static_cast<int>(completionEntries_.size()) - 1, completionSelected_ + 8);
            consumed = true;
        } else if (pressed(ImGuiKey_PageUp)) {
            completionSelected_ = std::max(0, completionSelected_ - 8);
            consumed = true;
        }
    }

    if (focused && !consumed) {
        const TextEditor::Coordinates caret = editor_->GetCursorPosition();
        if (ctrl && io.KeyAlt && (pressed(ImGuiKey_UpArrow, true) || pressed(ImGuiKey_DownArrow, true))) {
            addCaretVertically(ImGui::IsKeyPressed(ImGuiKey_UpArrow, true) ? -1 : 1);
            consumed = true;
        } else if (ctrl && io.KeyShift && pressed(ImGuiKey_D)) {
            extraCarets_.clear();
            duplicateLine();
            consumed = true;
        } else if (ctrl && pressed(ImGuiKey_D)) {
            addNextOccurrence();
            consumed = true;
        } else if (ctrl && io.KeyShift && pressed(ImGuiKey_F)) {
            std::string query = editor_->HasSelection() ? editor_->GetSelectedText() : std::string();
            if (query.empty() || query.find('\n') != std::string::npos) {
                const std::string& line = lines_.empty() ? std::string() : lines_[static_cast<size_t>(caret.mLine)];
                const int byte = columnToByte(line, caret.mColumn, kTabSize);
                const int start = identifierPrefixStart(line, byte);
                int finish = byte;
                while (finish < static_cast<int>(line.size()) && isIdentifierChar(line[static_cast<size_t>(finish)])) ++finish;
                query = line.substr(static_cast<size_t>(start), static_cast<size_t>(finish - start));
            }
            if (hooks_.findInAllScripts) hooks_.findInAllScripts(query);
            consumed = true;
        } else if (pressed(ImGuiKey_F12) && !lines_.empty()) {
            goToDefinitionAt(caret.mLine, columnToByte(lines_[static_cast<size_t>(caret.mLine)], caret.mColumn, kTabSize));
            consumed = true;
        } else if (pressed(ImGuiKey_F2)) {
            beginRename();
            consumed = true;
        } else if (pressed(ImGuiKey_F9)) {
            toggleBreakpoint(caret.mLine + 1);
            consumed = true;
        } else if (!extraCarets_.empty()) {
            consumed = handleMultiCursorKeys();
        }
    }

    if (focused && !consumed) {
        if (ctrl && pressed(ImGuiKey_Space)) {
            updateCompletions(true);
            consumed = true;
        } else if (ctrl && pressed(ImGuiKey_F)) {
            openFind(false);
            consumed = true;
        } else if (ctrl && pressed(ImGuiKey_H)) {
            openFind(true);
            consumed = true;
        } else if (ctrl && pressed(ImGuiKey_Slash)) {
            toggleCommentOnSelection();
            consumed = true;
        } else if (ctrl && io.KeyShift && pressed(ImGuiKey_O)) {
            openSymbolPicker_ = true;
            consumed = true;
        } else if (ctrl && (pressed(ImGuiKey_Equal) || pressed(ImGuiKey_KeypadAdd))) {
            g_prefs.zoom = std::min(2.5f, g_prefs.zoom + 0.1f);
            consumed = true;
        } else if (ctrl && (pressed(ImGuiKey_Minus) || pressed(ImGuiKey_KeypadSubtract))) {
            g_prefs.zoom = std::max(0.6f, g_prefs.zoom - 0.1f);
            consumed = true;
        } else if (ctrl && pressed(ImGuiKey_0)) {
            g_prefs.zoom = 1.0f;
            consumed = true;
        } else if (pressed(ImGuiKey_F3) && !matches_.empty()) {
            goToMatch(io.KeyShift ? (currentMatch_ - 1 + static_cast<int>(matches_.size())) % static_cast<int>(matches_.size())
                                  : (currentMatch_ + 1) % static_cast<int>(matches_.size()));
            consumed = true;
        } else if (pressed(ImGuiKey_Escape) && (callContext_.active || findOpen_)) {
            if (callContext_.active) callContext_ = {};
            else findOpen_ = false;
            refocusEditor_ = true;
            consumed = true;
        }
    }
    editor_->SetHandleKeyboardInputs(!consumed);
}

void ColorTextEditBackend::drawToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 6.0f);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 3.0f);
    const float buttonSide = ImGui::GetFrameHeight();
    const ImVec2 button(buttonSide, buttonSide);

    ImGui::BeginDisabled(!editor_->CanUndo());
    if (iconButton("undo", Icon::Undo, button, false, "Undo (Ctrl+Z)")) {
        editor_->Undo();
        reanalyze();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!editor_->CanRedo());
    if (iconButton("redo", Icon::Redo, button, false, "Redo (Ctrl+Y)")) {
        editor_->Redo();
        reanalyze();
    }
    ImGui::EndDisabled();
    verticalSeparator();
    if (iconButton("find", Icon::Search, button, findOpen_, "Find and replace (Ctrl+F / Ctrl+H)")) {
        if (findOpen_) findOpen_ = false;
        else openFind(false);
    }
    ImGui::SameLine();
    if (iconButton("comment", Icon::Comment, button, false, "Toggle line comment (Ctrl+/)")) toggleCommentOnSelection();
    ImGui::SameLine();
    if (iconButton("whitespace", Icon::Whitespace, button, g_prefs.whitespace, "Show whitespace")) {
        g_prefs.whitespace = !g_prefs.whitespace;
    }
    ImGui::SameLine();
    if (iconButton("problems", Icon::Warning, button, g_prefs.problemsOpen, "Problems panel")) {
        g_prefs.problemsOpen = !g_prefs.problemsOpen;
    }
    verticalSeparator();

    const int cursorLine = editor_->GetCursorPosition().mLine;
    const Symbol* current = nullptr;
    for (const Symbol& symbol : symbols_) {
        if (symbol.line <= cursorLine) current = &symbol;
    }
    const ImVec2 outlinePos = ImGui::GetCursorScreenPos();
    if (iconButton("outline", Icon::Outline, button, false, "Go to symbol (Ctrl+Shift+O)")) openSymbolPicker_ = true;
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (current != nullptr) ImGui::TextUnformatted(current->name.c_str());
    else ImGui::TextDisabled(symbols_.empty() ? "No functions yet" : "(top level)");

    static std::string symbolFilter;
    if (openSymbolPicker_) {
        ImGui::OpenPopup("##symbol_picker");
        symbolFilter.clear();
        openSymbolPicker_ = false;
    }
    ImGui::SetNextWindowPos(ImVec2(outlinePos.x, outlinePos.y + buttonSide + 4.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(300.0f, 0.0f), ImVec2(480.0f, 360.0f));
    if (ImGui::BeginPopup("##symbol_picker")) {
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1.0f);
        const bool submitted =
            ImGui::InputTextWithHint("##symbol_filter", "Go to function...", &symbolFilter, ImGuiInputTextFlags_EnterReturnsTrue);
        int shown = 0;
        int firstMatch = -1;
        for (int i = 0; i < static_cast<int>(symbols_.size()); ++i) {
            const Symbol& symbol = symbols_[static_cast<size_t>(i)];
            if (fuzzyScore(symbol.name, symbolFilter) < 0) continue;
            if (firstMatch < 0) firstMatch = i;
            ++shown;
            ImGui::PushID(i);
            if (ImGui::Selectable(symbol.name.c_str(), false, ImGuiSelectableFlags_AllowOverlap)) {
                moveCaretToLine(symbol.line + 1);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 60.0f);
            ImGui::TextDisabled("Ln %d", symbol.line + 1);
            ImGui::PopID();
        }
        if (shown == 0) ImGui::TextDisabled(symbols_.empty() ? "This script declares no functions." : "No match.");
        if (submitted && firstMatch >= 0) {
            moveCaretToLine(symbols_[static_cast<size_t>(firstMatch)].line + 1);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    const float rightWidth = 250.0f;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 8.0f, ImGui::GetContentRegionMax().x - rightWidth));
    if (ImGui::SmallButton("-")) g_prefs.zoom = std::max(0.6f, g_prefs.zoom - 0.1f);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%d%%", static_cast<int>(std::lround(g_prefs.zoom * 100.0f)));
    if (ImGui::IsItemClicked()) g_prefs.zoom = 1.0f;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Zoom (Ctrl +/-, Ctrl+wheel, click to reset)");
    ImGui::SameLine();
    if (ImGui::SmallButton("+")) g_prefs.zoom = std::min(2.5f, g_prefs.zoom + 0.1f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130.0f);
    if (ImGui::BeginCombo("##editor_theme", themes()[static_cast<size_t>(g_prefs.theme)].name)) {
        for (int i = 0; i < static_cast<int>(themes().size()); ++i) {
            if (ImGui::Selectable(themes()[static_cast<size_t>(i)].name, i == g_prefs.theme)) g_prefs.theme = i;
        }
        ImGui::EndCombo();
    }
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
}

void ColorTextEditBackend::openFind(bool withReplace) {
    findOpen_ = true;
    replaceOpen_ = replaceOpen_ || withReplace;
    focusFindInput_ = true;
    if (editor_->HasSelection()) {
        std::string selected = editor_->GetSelectedText();
        if (!selected.empty() && selected.find('\n') == std::string::npos) findQuery_ = selected;
    }
    matchedVersion_ = ~0ull;
}

void ColorTextEditBackend::drawFindBar() {
    recomputeMatches();
    const ImVec4 surface = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(surface.x, surface.y, surface.z, 0.6f));
    const float rows = replaceOpen_ ? 2.0f : 1.0f;
    const float height = rows * (ImGui::GetFrameHeight() + 4.0f) + 8.0f;
    ImGui::BeginChild("##find_bar", ImVec2(-1.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(6.0f, 4.0f));

    if (ImGui::ArrowButton("##toggle_replace", replaceOpen_ ? ImGuiDir_Down : ImGuiDir_Right)) replaceOpen_ = !replaceOpen_;
    ImGui::SameLine();
    if (focusFindInput_) {
        ImGui::SetKeyboardFocusHere();
        focusFindInput_ = false;
    }
    ImGui::SetNextItemWidth(240.0f);
    const bool enter = ImGui::InputTextWithHint("##find_query", "Find", &findQuery_, ImGuiInputTextFlags_EnterReturnsTrue);
    const bool findActive = ImGui::IsItemActive();
    if (enter && !matches_.empty()) {
        const int count = static_cast<int>(matches_.size());
        goToMatch(ImGui::GetIO().KeyShift ? (currentMatch_ - 1 + count) % count : (currentMatch_ + 1) % count);
        ImGui::SetKeyboardFocusHere(-1);
    }
    if (findActive && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        findOpen_ = false;
        refocusEditor_ = true;
    }
    ImGui::SameLine();
    if (toggleChip("Aa", matchCase_, "Match case")) {
        matchCase_ = !matchCase_;
        matchedVersion_ = ~0ull;
    }
    ImGui::SameLine();
    if (toggleChip("ab", wholeWord_, "Whole word")) {
        wholeWord_ = !wholeWord_;
        matchedVersion_ = ~0ull;
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    if (findQuery_.empty()) ImGui::TextDisabled("Type to search");
    else if (matches_.empty()) ImGui::TextColored(ImVec4(0.90f, 0.40f, 0.36f, 1.0f), "No results");
    else ImGui::Text("%d of %d", currentMatch_ < 0 ? 0 : currentMatch_ + 1, static_cast<int>(matches_.size()));
    ImGui::SameLine();
    ImGui::BeginDisabled(matches_.empty());
    if (ImGui::ArrowButton("##prev_match", ImGuiDir_Up)) {
        const int count = static_cast<int>(matches_.size());
        goToMatch((currentMatch_ - 1 + count) % count);
    }
    ImGui::SameLine();
    if (ImGui::ArrowButton("##next_match", ImGuiDir_Down)) goToMatch((currentMatch_ + 1) % static_cast<int>(matches_.size()));
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::SmallButton("x")) {
        findOpen_ = false;
        refocusEditor_ = true;
    }

    if (replaceOpen_) {
        ImGui::SetCursorPosX(6.0f + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth(240.0f);
        const bool replaceEnter =
            ImGui::InputTextWithHint("##replace_text", "Replace", &replaceText_, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::BeginDisabled(matches_.empty());
        if (ImGui::Button("Replace") || replaceEnter) replaceCurrentMatch();
        ImGui::SameLine();
        if (ImGui::Button("Replace All")) replaceAllMatches();
        ImGui::EndDisabled();
    }
    ImGui::EndChild();
}

void ColorTextEditBackend::recomputeMatches() {
    if (matchedVersion_ == textVersion_ && matchedQuery_ == findQuery_) return;
    matchedVersion_ = textVersion_;
    matchedQuery_ = findQuery_;
    matches_.clear();
    if (findQuery_.empty()) {
        currentMatch_ = -1;
        return;
    }
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    const std::string needle = matchCase_ ? findQuery_ : lower(findQuery_);
    for (int lineIndex = 0; lineIndex < static_cast<int>(lines_.size()); ++lineIndex) {
        const std::string hay = matchCase_ ? lines_[static_cast<size_t>(lineIndex)] : lower(lines_[static_cast<size_t>(lineIndex)]);
        for (size_t at = hay.find(needle); at != std::string::npos; at = hay.find(needle, at + needle.size())) {
            const size_t end = at + needle.size();
            if (wholeWord_ && ((at > 0 && isIdentifierChar(hay[at - 1])) || (end < hay.size() && isIdentifierChar(hay[end])))) {
                continue;
            }
            matches_.push_back({lineIndex, static_cast<int>(at), static_cast<int>(end)});
        }
    }
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    currentMatch_ = matches_.empty() ? -1 : 0;
    for (int i = 0; i < static_cast<int>(matches_.size()); ++i) {
        const FindMatch& match = matches_[static_cast<size_t>(i)];
        if (match.line > cursor.mLine ||
            (match.line == cursor.mLine &&
             byteToColumn(lines_[static_cast<size_t>(match.line)], match.byteEnd, kTabSize) >= cursor.mColumn)) {
            currentMatch_ = i;
            break;
        }
    }
}

void ColorTextEditBackend::selectRange(int line, int byteStart, int byteEnd) {
    const std::string& text = lines_[static_cast<size_t>(line)];
    const TextEditor::Coordinates start(line, byteToColumn(text, byteStart, kTabSize));
    const TextEditor::Coordinates end(line, byteToColumn(text, byteEnd, kTabSize));
    editor_->SetCursorPosition(end);
    editor_->SetSelection(start, end);
}

void ColorTextEditBackend::goToMatch(int index) {
    if (index < 0 || index >= static_cast<int>(matches_.size())) return;
    currentMatch_ = index;
    const FindMatch& match = matches_[static_cast<size_t>(index)];
    selectRange(match.line, match.byteStart, match.byteEnd);
}

void ColorTextEditBackend::replaceCurrentMatch() {
    if (currentMatch_ < 0 || currentMatch_ >= static_cast<int>(matches_.size())) return;
    const int replaced = currentMatch_;
    const FindMatch match = matches_[static_cast<size_t>(replaced)];
    selectRange(match.line, match.byteStart, match.byteEnd);
    editor_->Delete();
    editor_->InsertText(replaceText_);
    reanalyze();
    recomputeMatches();
    if (!matches_.empty()) goToMatch(std::min(replaced, static_cast<int>(matches_.size()) - 1));
}

void ColorTextEditBackend::replaceAllMatches() {
    for (auto it = matches_.rbegin(); it != matches_.rend(); ++it) {
        selectRange(it->line, it->byteStart, it->byteEnd);
        editor_->Delete();
        editor_->InsertText(replaceText_);
        lines_ = editor_->GetTextLines();
    }
    reanalyze();
    recomputeMatches();
}

bool ColorTextEditBackend::selectionLines(int& firstLine, int& lastLine) const {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    firstLine = lastLine = cursor.mLine;
    if (!editor_->HasSelection()) return false;
    const std::string selected = editor_->GetSelectedText();
    const int newlines = static_cast<int>(std::count(selected.begin(), selected.end(), '\n'));
    // The caret sits at one end of the selection; work out which.
    const std::string& line = lines_[static_cast<size_t>(cursor.mLine)];
    const int cursorByte = columnToByte(line, cursor.mColumn, kTabSize);
    const size_t lastNewline = selected.rfind('\n');
    const std::string tail = lastNewline == std::string::npos ? selected : selected.substr(lastNewline + 1);
    const bool caretAtEnd = cursorByte >= static_cast<int>(tail.size()) &&
                            line.compare(static_cast<size_t>(cursorByte) - tail.size(), tail.size(), tail) == 0;
    if (caretAtEnd) {
        firstLine = cursor.mLine - newlines;
        if (newlines > 0 && tail.empty()) lastLine = cursor.mLine - 1;
    } else {
        lastLine = cursor.mLine + newlines;
    }
    firstLine = std::max(0, firstLine);
    lastLine = std::clamp(lastLine, firstLine, static_cast<int>(lines_.size()) - 1);
    return true;
}

void ColorTextEditBackend::toggleCommentOnSelection() {
    if (lines_.empty()) return;
    int first = 0;
    int last = 0;
    const bool hadSelection = selectionLines(first, last);
    std::vector<std::string> updated = lines_;
    toggleLineComments(updated, first, last);

    const TextEditor::Coordinates blockStart(first, 0);
    const TextEditor::Coordinates blockEnd(last, byteToColumn(lines_[static_cast<size_t>(last)],
                                                              static_cast<int>(lines_[static_cast<size_t>(last)].size()), kTabSize));
    std::string block;
    for (int i = first; i <= last; ++i) {
        if (i > first) block += '\n';
        block += updated[static_cast<size_t>(i)];
    }
    editor_->SetCursorPosition(blockEnd);
    editor_->SetSelection(blockStart, blockEnd);
    editor_->Delete();
    editor_->InsertText(block);
    reanalyze();
    const TextEditor::Coordinates newEnd(last, byteToColumn(lines_[static_cast<size_t>(last)],
                                                            static_cast<int>(lines_[static_cast<size_t>(last)].size()), kTabSize));
    editor_->SetCursorPosition(newEnd);
    if (hadSelection) editor_->SetSelection(blockStart, newEnd);
}

void ColorTextEditBackend::duplicateLine() {
    if (lines_.empty()) return;
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    const std::string line = lines_[static_cast<size_t>(cursor.mLine)];
    const TextEditor::Coordinates lineEnd(cursor.mLine, byteToColumn(line, static_cast<int>(line.size()), kTabSize));
    editor_->SetCursorPosition(lineEnd);
    editor_->SetSelection(lineEnd, lineEnd);
    editor_->InsertText("\n" + line);
    const TextEditor::Coordinates target(cursor.mLine + 1, cursor.mColumn);
    editor_->SetCursorPosition(target);
    editor_->SetSelection(target, target);
    reanalyze();
}

void ColorTextEditBackend::drawEditor(ImVec2 size) {
    ImGuiIO& io = ImGui::GetIO();
    const auto& palette = editor_->GetPalette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          ImGui::ColorConvertU32ToFloat4(palette[static_cast<int>(TextEditor::PaletteIndex::Background)]));
    if (refocusEditor_) {
        ImGui::SetNextWindowFocus();
        refocusEditor_ = false;
    }
    ImGuiWindowFlags flags = ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav;
    if (io.KeyCtrl) flags |= ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::BeginChild("##luau_source", size, ImGuiChildFlags_None, flags);
    editorWindowId_ = ImGui::GetCurrentWindow()->ID;
    ImGui::PushFont(core::kronosCodeFont(), ImGui::GetStyle().FontSizeBase * g_prefs.zoom);

    if (io.KeyCtrl && io.MouseWheel != 0.0f && ImGui::IsWindowHovered()) {
        g_prefs.zoom = std::clamp(g_prefs.zoom + io.MouseWheel * 0.1f, 0.6f, 2.5f);
    }

    editorOrigin_ = ImGui::GetCursorScreenPos();
    charAdvance_ = ImVec2(ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, "#").x,
                          ImGui::GetTextLineHeight());
    char gutter[16];
    std::snprintf(gutter, sizeof(gutter), " %d ", editor_->GetTotalLines());
    textStart_ = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, gutter).x + 10.0f;

    const TextEditor::Coordinates cursorBefore = editor_->GetCursorPosition();
    editor_->Render("##luau_source");
    editor_->SetHandleKeyboardInputs(true);
    editorClipMin_ = ImGui::GetWindowPos();
    editorClipMax_ = ImVec2(editorClipMin_.x + ImGui::GetWindowSize().x, editorClipMin_.y + ImGui::GetWindowSize().y);

    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !lines_.empty()) {
        const ImVec2 mouse = io.MousePos;
        const auto [line, byte] = mouseTextPosition();
        const bool belowText = mouse.y >= editorOrigin_.y + static_cast<float>(lines_.size()) * charAdvance_.y;
        if (mouse.x < editorOrigin_.x + textStart_ - 4.0f) {
            if (!belowText) toggleBreakpoint(line + 1);
        } else if (io.KeyAlt && !io.KeyCtrl) {
            const int offset = offsetOf(line, byteToColumn(lines_[static_cast<size_t>(line)], byte, kTabSize));
            std::vector<Caret> carets = allCarets();
            const auto existing = std::find_if(carets.begin() + 1, carets.end(), [&](const Caret& c) { return c.position == offset; });
            if (existing != carets.end()) carets.erase(existing);
            else carets.push_back({offset, offset});
            setCarets(carets);
        } else if (io.KeyCtrl && !io.KeyAlt && !belowText) {
            extraCarets_.clear();
            goToDefinitionAt(line, byte);
        } else {
            extraCarets_.clear();
        }
    }
    drawOverlays(ImGui::GetWindowDrawList(), editorOrigin_, charAdvance_, textStart_);

    if (editor_->IsTextChanged()) {
        lines_ = editor_->GetTextLines();
        const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
        const std::string line = cursor.mLine < static_cast<int>(lines_.size()) ? lines_[static_cast<size_t>(cursor.mLine)] : "";
        const int cursorByte = columnToByte(line, cursor.mColumn, kTabSize);
        const bool singleCharTyped = io.InputQueueCharacters.Size == 1 || (cursor.mLine == cursorBefore.mLine &&
                                                                           cursor.mColumn == cursorBefore.mColumn + 1);
        if (singleCharTyped && cursorByte > 0 && cursorByte <= static_cast<int>(line.size())) {
            const char lastTyped = line[static_cast<size_t>(cursorByte - 1)];
            const char nextChar = cursorByte < static_cast<int>(line.size()) ? line[static_cast<size_t>(cursorByte)] : '\0';
            const char prevChar = cursorByte > 1 ? line[static_cast<size_t>(cursorByte - 2)] : '\0';
            const bool quote = lastTyped == '"' || lastTyped == '\'';
            const bool closer = lastTyped == ')' || lastTyped == ']' || lastTyped == '}';
            const bool inString = positionInCommentOrString(line, cursorByte - 1);
            if ((quote || closer) && nextChar == lastTyped) {
                const TextEditor::Coordinates typedStart(cursor.mLine, cursor.mColumn - 1);
                editor_->SetSelection(typedStart, cursor);
                editor_->Delete();
                editor_->SetCursorPosition(cursor);
                editor_->SetSelection(cursor, cursor);
            } else if (!inString && (quote || lastTyped == '(' || lastTyped == '[' || lastTyped == '{') &&
                       (nextChar == '\0' || nextChar == ' ' || nextChar == ')' || nextChar == ']' || nextChar == '}' ||
                        nextChar == ',')) {
                const char closing = lastTyped == '(' ? ')' : lastTyped == '[' ? ']' : lastTyped == '{' ? '}' : lastTyped;
                editor_->InsertText(std::string(1, closing));
                editor_->SetCursorPosition(cursor);
                editor_->SetSelection(cursor, cursor);
            }
            reanalyze();
            const std::string& current = lines_[static_cast<size_t>(cursor.mLine)];
            if (!positionInCommentOrString(current, cursorByte)) {
                if ((lastTyped == '.' && prevChar != '.' && (isIdentifierChar(prevChar) || prevChar == ')' || prevChar == ']') &&
                     !std::isdigit(static_cast<unsigned char>(prevChar))) ||
                    lastTyped == ':') {
                    updateCompletions(true);
                } else if (g_prefs.autoSuggest && isIdentifierChar(lastTyped) && !showCompletions_ &&
                           identifierPrefixStart(current, cursorByte) == cursorByte - 1 &&
                           !std::isdigit(static_cast<unsigned char>(lastTyped))) {
                    updateCompletions(false);
                }
            }
        } else {
            reanalyze();
        }
        updateSignatureHelp();
    } else if (editor_->IsCursorPositionChanged()) {
        updateSignatureHelp();
    }
    if (showCompletions_) refreshCompletionFilter();

    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void ColorTextEditBackend::drawOverlays(ImDrawList* drawList, ImVec2 origin, ImVec2 advance, float textStart) {
    if (lines_.empty()) return;
    const int firstVisible = std::max(0, static_cast<int>((editorClipMin_.y - origin.y) / advance.y) - 1);
    const int lastVisible =
        std::min(static_cast<int>(lines_.size()) - 1, static_cast<int>((editorClipMax_.y - origin.y) / advance.y) + 1);
    auto lineY = [&](int line) { return origin.y + static_cast<float>(line) * advance.y; };
    auto colX = [&](int column) { return origin.x + textStart + static_cast<float>(column) * advance.x; };

    const ImVec4 accent = ui::accent();
    for (int i = 0; i < static_cast<int>(matches_.size()) && findOpen_; ++i) {
        const FindMatch& match = matches_[static_cast<size_t>(i)];
        if (match.line < firstVisible || match.line > lastVisible) continue;
        const std::string& text = lines_[static_cast<size_t>(match.line)];
        const ImVec2 min(colX(byteToColumn(text, match.byteStart, kTabSize)), lineY(match.line));
        const ImVec2 max(colX(byteToColumn(text, match.byteEnd, kTabSize)), lineY(match.line) + advance.y);
        drawList->AddRectFilled(min, max, IM_COL32(255, 196, 0, i == currentMatch_ ? 70 : 38), 2.0f);
        if (i == currentMatch_) drawList->AddRect(min, max, ImGui::GetColorU32(accent), 2.0f);
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool hovered = ImGui::IsWindowHovered();
    const float rowRight = editorClipMax_.x;

    if (executionLine_ > 0 && executionLine_ - 1 >= firstVisible && executionLine_ - 1 <= lastVisible) {
        const float y = lineY(executionLine_ - 1);
        drawList->AddRectFilled(ImVec2(origin.x + textStart - 2.0f, y), ImVec2(rowRight, y + advance.y), IM_COL32(255, 204, 0, 30));
        drawList->AddRectFilled(ImVec2(origin.x + textStart - 2.0f, y), ImVec2(origin.x + textStart, y + advance.y),
                                IM_COL32(255, 204, 0, 255));
    }
    const float dotRadius = std::max(3.0f, std::min(advance.y * 0.22f, advance.x * 0.45f));
    const float dotX = origin.x + dotRadius + 1.0f;
    for (int line : breakpoints_) {
        if (line - 1 < firstVisible || line - 1 > lastVisible) continue;
        drawList->AddCircleFilled(ImVec2(dotX, lineY(line - 1) + advance.y * 0.5f), dotRadius, IM_COL32(229, 57, 53, 255));
    }
    if (hovered && mouse.x < origin.x + textStart - 4.0f) {
        const int line = static_cast<int>((mouse.y - origin.y) / advance.y);
        if (line >= 0 && line < static_cast<int>(lines_.size()) && breakpoints_.count(line + 1) == 0) {
            drawList->AddCircleFilled(ImVec2(dotX, lineY(line) + advance.y * 0.5f), dotRadius, IM_COL32(229, 57, 53, 90));
        }
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    if (executionLine_ > 0 && executionLine_ - 1 >= firstVisible && executionLine_ - 1 <= lastVisible) {
        const float cy = lineY(executionLine_ - 1) + advance.y * 0.5f;
        const float half = dotRadius;
        drawList->AddTriangleFilled(ImVec2(dotX - half, cy - half), ImVec2(dotX - half, cy + half), ImVec2(dotX + half + 1.0f, cy),
                                    IM_COL32(255, 204, 0, 255));
    }

    const ImU32 caretColor = editor_->GetPalette()[static_cast<int>(TextEditor::PaletteIndex::Cursor)];
    const ImU32 selectionColor = editor_->GetPalette()[static_cast<int>(TextEditor::PaletteIndex::Selection)];
    for (const Caret& caret : extraCarets_) {
        const int from = std::min(caret.anchor, caret.position);
        const int to = std::max(caret.anchor, caret.position);
        if (from != to) {
            const auto [startLine, startColumn] = positionOf(from);
            const auto [endLine, endColumn] = positionOf(to);
            for (int line = startLine; line <= endLine; ++line) {
                if (line < firstVisible || line > lastVisible) continue;
                const int c0 = line == startLine ? startColumn : 0;
                const int c1 = line == endLine ? endColumn
                                               : byteToColumn(lines_[static_cast<size_t>(line)],
                                                              static_cast<int>(lines_[static_cast<size_t>(line)].size()), kTabSize) + 1;
                drawList->AddRectFilled(ImVec2(colX(c0), lineY(line)), ImVec2(colX(c1), lineY(line) + advance.y), selectionColor);
            }
        }
        const auto [line, column] = positionOf(caret.position);
        if (line < firstVisible || line > lastVisible) continue;
        drawList->AddRectFilled(ImVec2(colX(column), lineY(line)), ImVec2(colX(column) + 2.0f, lineY(line) + advance.y), caretColor);
    }

    if (hovered && ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt && mouse.x >= origin.x + textStart) {
        const auto [line, byte] = mouseTextPosition();
        if (line >= firstVisible && line <= lastVisible && mouse.y < lineY(static_cast<int>(lines_.size()))) {
            const std::string& text = lines_[static_cast<size_t>(line)];
            int start = std::min(byte, static_cast<int>(text.size()));
            int finish = start;
            while (start > 0 && isIdentifierChar(text[static_cast<size_t>(start - 1)])) --start;
            while (finish < static_cast<int>(text.size()) && isIdentifierChar(text[static_cast<size_t>(finish)])) ++finish;
            if (finish > start && !std::isdigit(static_cast<unsigned char>(text[static_cast<size_t>(start)]))) {
                const float y = lineY(line) + advance.y - 1.0f;
                drawList->AddLine(ImVec2(colX(byteToColumn(text, start, kTabSize)), y),
                                  ImVec2(colX(byteToColumn(text, finish, kTabSize)), y), ImGui::GetColorU32(accent), 1.0f);
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            }
        }
    }

    const ImU32 errorColor = IM_COL32(229, 83, 75, 255);
    const ImU32 warningColor = IM_COL32(232, 169, 59, 255);
    int lastAnnotatedLine = -1;
    for (const Diagnostic& diagnostic : diagnostics_) {
        if (diagnostic.line < firstVisible || diagnostic.line > lastVisible ||
            diagnostic.line >= static_cast<int>(lines_.size())) {
            continue;
        }
        const std::string& text = lines_[static_cast<size_t>(diagnostic.line)];
        const ImU32 color = diagnostic.warning ? warningColor : errorColor;
        const int startColumn = byteToColumn(text, diagnostic.column, kTabSize);
        int endColumn = diagnostic.endLine == diagnostic.line ? byteToColumn(text, diagnostic.endColumn, kTabSize)
                                                              : byteToColumn(text, static_cast<int>(text.size()), kTabSize);
        endColumn = std::max(endColumn, startColumn + 1);
        const float y = lineY(diagnostic.line) + advance.y - 2.0f;
        const float x0 = colX(startColumn);
        const float x1 = colX(endColumn);
        std::vector<ImVec2> zigzag;
        for (float x = x0; x <= x1 + 0.5f; x += 2.5f) {
            zigzag.emplace_back(x, y + ((static_cast<int>((x - x0) / 2.5f) % 2 == 0) ? 0.0f : -2.0f));
        }
        drawList->AddPolyline(zigzag.data(), static_cast<int>(zigzag.size()), color, ImDrawFlags_None, 1.2f);

        if (diagnostic.line != lastAnnotatedLine) {
            lastAnnotatedLine = diagnostic.line;
            drawList->AddCircleFilled(ImVec2(origin.x + 6.0f, lineY(diagnostic.line) + advance.y * 0.5f), 3.0f, color);
            std::string message = diagnostic.message.substr(0, diagnostic.message.find('\n'));
            if (message.size() > 110) message = message.substr(0, 107) + "...";
            const int lineEndColumn = byteToColumn(text, static_cast<int>(text.size()), kTabSize);
            const ImU32 faded = (color & 0x00FFFFFFu) | (0xB0u << 24);
            drawList->AddText(ImVec2(colX(lineEndColumn + 3), lineY(diagnostic.line)), faded, message.c_str());
        }
        if (hovered && mouse.y >= lineY(diagnostic.line) && mouse.y < lineY(diagnostic.line) + advance.y && mouse.x >= x0 - 2.0f &&
            mouse.x <= x1 + 2.0f) {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase);
            ImGui::BeginTooltip();
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(color), diagnostic.warning ? "Warning" : "Error");
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 32.0f);
            ImGui::TextUnformatted(diagnostic.message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
            ImGui::PopFont();
        }
    }
}

std::string ColorTextEditBackend::memberContainerBefore(int line, int anchorByte) const {
    if (line < 0 || line >= static_cast<int>(lines_.size()) || anchorByte <= 0) return {};
    const std::string& text = lines_[static_cast<size_t>(line)];
    if (anchorByte > static_cast<int>(text.size())) return {};
    const char separator = text[static_cast<size_t>(anchorByte - 1)];
    if (separator != '.' && separator != ':') return {};
    int start = anchorByte - 1;
    while (start > 0 && (isIdentifierChar(text[static_cast<size_t>(start - 1)]) || text[static_cast<size_t>(start - 1)] == '.')) {
        --start;
    }
    return text.substr(static_cast<size_t>(start), static_cast<size_t>(anchorByte - 1 - start));
}

void ColorTextEditBackend::updateCompletions(bool explicitRequest) {
    if (lines_.empty()) lines_ = editor_->GetTextLines();
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    const std::string& line = lines_[static_cast<size_t>(cursor.mLine)];
    const int cursorByte = columnToByte(line, cursor.mColumn, kTabSize);
    const Luau::AutocompleteResult result = analyzer_->complete(editor_->GetText(), cursor.mLine, cursorByte);

    completionAnchorLine_ = cursor.mLine;
    completionAnchorColumn_ = identifierPrefixStart(line, cursorByte);
    const std::string container = memberContainerBefore(cursor.mLine, completionAnchorColumn_);
    const bool methodAccess = completionAnchorColumn_ > 0 && line[static_cast<size_t>(completionAnchorColumn_ - 1)] == ':';

    completionRawEntries_.clear();
    for (const auto& [name, entry] : result.entryMap) {
        if (entry.deprecated || entry.kind == Luau::AutocompleteEntryKind::HotComment ||
            entry.kind == Luau::AutocompleteEntryKind::String || entry.kind == Luau::AutocompleteEntryKind::RequirePath) {
            continue;
        }
        CompletionItem item;
        item.label = name;
        item.insertText = entry.insertText.value_or(name);
        item.filterText = name;
        item.typeCorrect = entry.typeCorrect != Luau::TypeCorrectKind::None;
        bool isFunction = false;
        bool isTable = false;
        if (entry.type) {
            Luau::TypeId type = Luau::follow(*entry.type);
            isFunction = Luau::get<Luau::FunctionType>(type) != nullptr;
            isTable = Luau::get<Luau::TableType>(type) != nullptr;
            item.detail = Luau::toString(*entry.type, namedArguments());
            if (item.detail.size() > 140) item.detail = item.detail.substr(0, 137) + "...";
        }
        switch (entry.kind) {
            case Luau::AutocompleteEntryKind::Keyword: item.kind = CompletionKind::Keyword; break;
            case Luau::AutocompleteEntryKind::Type: item.kind = CompletionKind::Type; break;
            case Luau::AutocompleteEntryKind::Module: item.kind = CompletionKind::Module; break;
            case Luau::AutocompleteEntryKind::Property:
                item.kind = isFunction ? (methodAccess ? CompletionKind::Method : CompletionKind::Function)
                                       : isTable ? CompletionKind::Table : CompletionKind::Property;
                break;
            default:
                item.kind = isFunction ? CompletionKind::Function : isTable ? CompletionKind::Table : CompletionKind::Variable;
                break;
        }
        item.callable = isFunction && entry.parens != Luau::ParenthesesRecommendation::None;
        item.cursorInsideParens = entry.parens == Luau::ParenthesesRecommendation::CursorInside;
        auto doc = apiDocs().find(container.empty() ? name : container + "." + name);
        if (doc != apiDocs().end()) item.doc = doc->second;
        completionRawEntries_.push_back(std::move(item));
    }

    const bool statementContext = container.empty() && (result.context == Luau::AutocompleteContext::Statement ||
                                                         result.context == Luau::AutocompleteContext::Unknown);
    if (statementContext) {
        for (const SnippetDef& snippet : kSnippets) {
            CompletionItem item;
            item.label = snippet.label;
            item.filterText = snippet.filter;
            item.insertText = snippet.body;
            item.kind = CompletionKind::Snippet;
            item.detail = "Insert a code template";
            completionRawEntries_.push_back(std::move(item));
        }
    }

    completionExplicit_ = explicitRequest;
    completionSelected_ = 0;
    showCompletions_ = true;
    refreshCompletionFilter();
}

void ColorTextEditBackend::refreshCompletionFilter() {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    if (cursor.mLine != completionAnchorLine_ || cursor.mLine >= static_cast<int>(lines_.size())) {
        showCompletions_ = false;
        return;
    }
    const std::string& line = lines_[static_cast<size_t>(cursor.mLine)];
    const int cursorByte = columnToByte(line, cursor.mColumn, kTabSize);
    if (!completionAnchorValid(completionAnchorLine_, completionAnchorColumn_, cursor.mLine, cursorByte, line)) {
        showCompletions_ = false;
        return;
    }
    const std::string prefix = line.substr(static_cast<size_t>(completionAnchorColumn_),
                                           static_cast<size_t>(cursorByte - completionAnchorColumn_));
    if (prefix.empty() && !completionExplicit_) {
        showCompletions_ = false;
        return;
    }

    const std::string previouslySelected =
        completionSelected_ < static_cast<int>(completionEntries_.size()) ? completionEntries_[static_cast<size_t>(completionSelected_)].label
                                                                          : std::string();
    completionEntries_.clear();
    for (CompletionItem item : completionRawEntries_) {
        item.score = fuzzyScore(item.filterText, prefix);
        if (item.score < 0) continue;
        if (item.kind == CompletionKind::Snippet && prefix.empty()) continue;
        completionEntries_.push_back(std::move(item));
    }
    std::stable_sort(completionEntries_.begin(), completionEntries_.end(), [](const CompletionItem& a, const CompletionItem& b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.typeCorrect != b.typeCorrect) return a.typeCorrect;
        if ((a.kind == CompletionKind::Snippet) != (b.kind == CompletionKind::Snippet)) return b.kind == CompletionKind::Snippet;
        if (a.label.size() != b.label.size()) return a.label.size() < b.label.size();
        return a.label < b.label;
    });
    constexpr size_t kMaxEntries = 60;
    if (completionEntries_.size() > kMaxEntries) completionEntries_.resize(kMaxEntries);

    completionSelected_ = 0;
    for (int i = 0; i < static_cast<int>(completionEntries_.size()); ++i) {
        if (completionEntries_[static_cast<size_t>(i)].label == previouslySelected && !prefix.empty()) {
            completionSelected_ = i;
            break;
        }
    }
    showCompletions_ = !completionEntries_.empty();
}

void ColorTextEditBackend::insertCompletion(const CompletionItem& item) {
    showCompletions_ = false;
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    const std::string& line = lines_[static_cast<size_t>(cursor.mLine)];
    const int cursorByte = columnToByte(line, cursor.mColumn, kTabSize);
    if (!completionAnchorValid(completionAnchorLine_, completionAnchorColumn_, cursor.mLine, cursorByte, line)) return;

    if (completionAnchorColumn_ != cursorByte) {
        const TextEditor::Coordinates anchor(completionAnchorLine_, byteToColumn(line, completionAnchorColumn_, kTabSize));
        editor_->SetSelection(anchor, cursor);
        editor_->Delete();
    }
    if (item.kind == CompletionKind::Snippet) {
        insertSnippet(item.insertText);
        return;
    }

    const char nextChar = cursorByte < static_cast<int>(line.size()) ? line[static_cast<size_t>(cursorByte)] : '\0';
    std::string text = item.insertText;
    const bool addParens = item.callable && nextChar != '(';
    if (addParens) text += "()";
    editor_->InsertText(text);
    if (addParens && item.cursorInsideParens) {
        TextEditor::Coordinates inside = editor_->GetCursorPosition();
        inside.mColumn -= 1;
        editor_->SetCursorPosition(inside);
        editor_->SetSelection(inside, inside);
    }
    reanalyze();
    updateSignatureHelp();
}

void ColorTextEditBackend::insertSnippet(const std::string& body) {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    const std::string line = lines_[static_cast<size_t>(cursor.mLine)];
    const int startByte = columnToByte(line, cursor.mColumn, kTabSize);
    const std::string indentation = line.substr(0, line.find_first_not_of(" \t") == std::string::npos
                                                       ? line.size()
                                                       : line.find_first_not_of(" \t"));

    std::string expanded;
    for (char c : body) {
        expanded += c;
        if (c == '\n') expanded += indentation;
    }
    int placeholderStart = -1;
    int placeholderLength = 0;
    const size_t tabStop = expanded.find("${1:");
    if (tabStop != std::string::npos) {
        const size_t close = expanded.find('}', tabStop);
        const std::string placeholder = expanded.substr(tabStop + 4, close - tabStop - 4);
        expanded.replace(tabStop, close - tabStop + 1, placeholder);
        placeholderStart = static_cast<int>(tabStop);
        placeholderLength = static_cast<int>(placeholder.size());
    } else if (size_t caret = expanded.find("$0"); caret != std::string::npos) {
        expanded.erase(caret, 2);
        placeholderStart = static_cast<int>(caret);
    } else {
        placeholderStart = static_cast<int>(expanded.size());
    }

    editor_->InsertText(expanded);
    reanalyze();

    const std::string before = expanded.substr(0, static_cast<size_t>(placeholderStart));
    const int lineOffset = static_cast<int>(std::count(before.begin(), before.end(), '\n'));
    const int targetLine = cursor.mLine + lineOffset;
    const size_t lastNewline = before.rfind('\n');
    const int byteInLine =
        lastNewline == std::string::npos ? startByte + placeholderStart : static_cast<int>(before.size() - lastNewline - 1);
    if (targetLine < static_cast<int>(lines_.size())) selectRange(targetLine, byteInLine, byteInLine + placeholderLength);
}

void ColorTextEditBackend::updateSignatureHelp() {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    if (cursor.mLine >= static_cast<int>(lines_.size())) {
        callContext_ = {};
        return;
    }
    const std::string& line = lines_[static_cast<size_t>(cursor.mLine)];
    CallContext context = findCallContext(line, columnToByte(line, cursor.mColumn, kTabSize));
    callContext_ = context;
    if (!context.active) return;

    const std::string key = context.callee + "@" + std::to_string(cursor.mLine) + ":" + std::to_string(context.calleeEnd);
    if (key == signatureKey_) return;
    signatureKey_ = key;
    signatureName_.clear();
    signatureParams_.clear();
    signatureDoc_.clear();

    const size_t separator = context.callee.find_last_of(".:");
    const std::string name = separator == std::string::npos ? context.callee : context.callee.substr(separator + 1);
    const Luau::AutocompleteResult result = analyzer_->complete(editor_->GetText(), cursor.mLine, context.calleeEnd);
    auto entry = result.entryMap.find(name);
    if (entry == result.entryMap.end() || !entry->second.type) return;
    if (Luau::get<Luau::FunctionType>(Luau::follow(*entry->second.type)) == nullptr) return;
    signatureName_ = context.callee;
    signatureParams_ = splitSignatureParameters(Luau::toString(*entry->second.type, namedArguments()), signatureReturn_);
    if (separator != std::string::npos && context.callee[separator] == ':' && !signatureParams_.empty() &&
        signatureParams_.front().rfind("self", 0) == 0) {
        signatureParams_.erase(signatureParams_.begin());
    }
    auto doc = apiDocs().find(separator == std::string::npos ? name : context.callee.substr(0, separator) + "." + name);
    if (doc != apiDocs().end()) signatureDoc_ = doc->second;
}

void ColorTextEditBackend::drawCompletionPopup() {
    if (!showCompletions_ || completionEntries_.empty() || !caretVisible()) return;
    const float rowHeight = ImGui::GetTextLineHeight() + 6.0f;
    const int visibleRows = std::min(9, static_cast<int>(completionEntries_.size()));
    const float width = 440.0f;
    const ImVec2 caret = caretScreenPos();
    const float prefixWidth = static_cast<float>(editor_->GetCursorPosition().mColumn -
                                                 byteToColumn(lines_[static_cast<size_t>(completionAnchorLine_)],
                                                              completionAnchorColumn_, kTabSize)) *
                              charAdvance_.x;
    ImVec2 pos(caret.x - prefixWidth - 30.0f, caret.y + charAdvance_.y + 3.0f);
    const ImGuiViewport* viewport = ImGui::GetWindowViewport();
    const float estimatedHeight = rowHeight * static_cast<float>(visibleRows) + 90.0f;
    if (pos.y + estimatedHeight > viewport->Pos.y + viewport->Size.y) pos.y = caret.y - estimatedHeight - 3.0f;
    pos.x = std::clamp(pos.x, viewport->Pos.x, viewport->Pos.x + viewport->Size.x - width);

    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize;
    if (ImGui::Begin("##kronos_completion_popup", nullptr, flags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::BeginChild("##completion_rows", ImVec2(width - 8.0f, rowHeight * static_cast<float>(visibleRows)), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoNav);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        static int lastScrolledTo = -1;
        for (int i = 0; i < static_cast<int>(completionEntries_.size()); ++i) {
            const CompletionItem& item = completionEntries_[static_cast<size_t>(i)];
            const bool selected = i == completionSelected_;
            ImGui::PushID(i);
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_None, ImVec2(0.0f, rowHeight))) {
                completionSelected_ = i;
                CompletionItem chosen = item;
                insertCompletion(chosen);
                refocusEditor_ = true;
                ImGui::PopID();
                break;
            }
            if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDelta.x != 0.0f) completionSelected_ = i;
            if (selected && lastScrolledTo != i) {
                ImGui::SetScrollHereY();
                lastScrolledTo = i;
            }
            const float badge = rowHeight - 8.0f;
            drawBadge(drawList, ImVec2(rowMin.x + 4.0f, rowMin.y + 4.0f), badge, kindColor(item.kind), kindGlyph(item.kind));
            const ImVec2 labelPos(rowMin.x + badge + 12.0f, rowMin.y + 3.0f);
            drawList->AddText(labelPos, ImGui::GetColorU32(selected ? ImGuiCol_Text : ImGuiCol_Text), item.label.c_str());
            if (!item.detail.empty() && item.kind != CompletionKind::Snippet) {
                const float labelWidth = ImGui::CalcTextSize(item.label.c_str()).x;
                const float available = width - 40.0f - badge - labelWidth - 24.0f;
                std::string detail = item.detail;
                while (!detail.empty() && ImGui::CalcTextSize(detail.c_str()).x > available) {
                    detail.resize(detail.size() > 4 ? detail.size() - 4 : 0);
                    if (!detail.empty()) detail += "...";
                    if (detail.size() <= 3) detail.clear();
                }
                if (!detail.empty()) {
                    const float detailWidth = ImGui::CalcTextSize(detail.c_str()).x;
                    drawList->AddText(ImVec2(rowMin.x + width - 20.0f - detailWidth, labelPos.y),
                                      ImGui::GetColorU32(ImGuiCol_TextDisabled), detail.c_str());
                }
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (completionSelected_ < static_cast<int>(completionEntries_.size())) {
            const CompletionItem& item = completionEntries_[static_cast<size_t>(completionSelected_)];
            ImGui::Separator();
            ImGui::PushTextWrapPos(width - 12.0f);
            ImGui::TextColored(kindColor(item.kind), "%s", kindName(item.kind));
            if (item.kind == CompletionKind::Snippet) {
                ImGui::PushFont(core::kronosCodeFont(), ImGui::GetStyle().FontSizeBase * 0.92f);
                std::string preview = item.insertText;
                for (const char* marker : {"${1:", "$0"}) {
                    for (size_t at = preview.find(marker); at != std::string::npos; at = preview.find(marker)) {
                        preview.erase(at, std::string(marker).size());
                        if (marker[1] == '{') {
                            const size_t close = preview.find('}', at);
                            if (close != std::string::npos) preview.erase(close, 1);
                        }
                    }
                }
                for (size_t at = preview.find('\t'); at != std::string::npos; at = preview.find('\t')) preview.replace(at, 1, "    ");
                ImGui::TextDisabled("%s", preview.c_str());
                ImGui::PopFont();
            } else if (!item.detail.empty()) {
                ImGui::PushFont(core::kronosCodeFont(), ImGui::GetStyle().FontSizeBase * 0.92f);
                ImGui::TextDisabled("%s", item.detail.c_str());
                ImGui::PopFont();
            }
            if (!item.doc.empty()) ImGui::TextUnformatted(item.doc.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::TextDisabled("Enter/Tab insert   Up/Down choose   Esc close");
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void ColorTextEditBackend::drawSignaturePopup() {
    if (!callContext_.active || signatureName_.empty() || !caretVisible()) return;
    const ImVec2 caret = caretScreenPos();
    ImGui::SetNextWindowPos(ImVec2(caret.x - 20.0f, caret.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 2.0f));
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##kronos_signature_popup", nullptr, flags)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImGui::PushFont(core::kronosCodeFont(), ImGui::GetStyle().FontSizeBase * 0.95f);
        const ImVec4 dim = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
        const ImVec4 accent = ui::accent();
        ImGui::TextUnformatted(signatureName_.c_str());
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(dim, "(");
        for (size_t i = 0; i < signatureParams_.size(); ++i) {
            ImGui::SameLine(0.0f, 0.0f);
            if (i > 0) {
                ImGui::TextColored(dim, ", ");
                ImGui::SameLine(0.0f, 0.0f);
            }
            const bool active = static_cast<int>(i) == callContext_.argumentIndex ||
                                (i + 1 == signatureParams_.size() && signatureParams_[i].rfind("...", 0) == 0 &&
                                 callContext_.argumentIndex > static_cast<int>(i));
            if (active) ImGui::TextColored(accent, "%s", signatureParams_[i].c_str());
            else ImGui::TextUnformatted(signatureParams_[i].c_str());
            if (active) {
                const ImVec2 min = ImGui::GetItemRectMin();
                const ImVec2 max = ImGui::GetItemRectMax();
                ImGui::GetWindowDrawList()->AddLine(ImVec2(min.x, max.y), max, ImGui::GetColorU32(accent), 1.5f);
            }
        }
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextColored(dim, ")%s%s", signatureReturn_.empty() ? "" : " ", signatureReturn_.c_str());
        ImGui::PopFont();
        if (!signatureDoc_.empty()) ImGui::TextDisabled("%s", signatureDoc_.c_str());
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
}

void ColorTextEditBackend::drawProblems(float height) {
    int errors = 0;
    int warnings = 0;
    for (const Diagnostic& diagnostic : diagnostics_) (diagnostic.warning ? warnings : errors)++;

    ImGui::BeginChild("##problems", ImVec2(-1.0f, height), ImGuiChildFlags_Borders);
    if (ImFont* bold = core::kronosBoldFont()) ImGui::PushFont(bold, 0.0f);
    ImGui::TextUnformatted("PROBLEMS");
    if (core::kronosBoldFont() != nullptr) ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.90f, 0.33f, 0.29f, 1.0f), "%d error%s", errors, errors == 1 ? "" : "s");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.91f, 0.66f, 0.23f, 1.0f), "%d warning%s", warnings, warnings == 1 ? "" : "s");
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 24.0f);
    if (ImGui::SmallButton("x##close_problems")) g_prefs.problemsOpen = false;
    ImGui::Separator();

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    for (int i = 0; i < static_cast<int>(diagnostics_.size()); ++i) {
        const Diagnostic& diagnostic = diagnostics_[static_cast<size_t>(i)];
        ImGui::PushID(i);
        const ImVec2 rowMin = ImGui::GetCursorScreenPos();
        const std::string message = diagnostic.message.substr(0, diagnostic.message.find('\n'));
        if (ImGui::Selectable("##problem", false, ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetTextLineHeight() + 2.0f))) {
            const std::string& text = diagnostic.line < static_cast<int>(lines_.size()) ? lines_[static_cast<size_t>(diagnostic.line)] : "";
            const TextEditor::Coordinates target(diagnostic.line, byteToColumn(text, diagnostic.column, kTabSize));
            editor_->SetCursorPosition(target);
            editor_->SetSelection(target, target);
            refocusEditor_ = true;
        }
        const float iconSize = ImGui::GetTextLineHeight() * 0.8f;
        const ImVec2 iconCenter(rowMin.x + iconSize * 0.5f + 4.0f, rowMin.y + ImGui::GetTextLineHeight() * 0.5f + 1.0f);
        if (diagnostic.warning) {
            drawIcon(drawList, Icon::Warning, iconCenter, iconSize, IM_COL32(232, 169, 59, 255));
        } else {
            drawList->AddCircleFilled(iconCenter, iconSize * 0.42f, IM_COL32(229, 83, 75, 255));
            drawList->AddLine(ImVec2(iconCenter.x - 2.5f, iconCenter.y - 2.5f), ImVec2(iconCenter.x + 2.5f, iconCenter.y + 2.5f),
                              IM_COL32(255, 255, 255, 230), 1.4f);
            drawList->AddLine(ImVec2(iconCenter.x + 2.5f, iconCenter.y - 2.5f), ImVec2(iconCenter.x - 2.5f, iconCenter.y + 2.5f),
                              IM_COL32(255, 255, 255, 230), 1.4f);
        }
        drawList->AddText(ImVec2(rowMin.x + iconSize + 12.0f, rowMin.y + 1.0f), ImGui::GetColorU32(ImGuiCol_Text), message.c_str());
        char location[32];
        std::snprintf(location, sizeof(location), "Ln %d, Col %d", diagnostic.line + 1, diagnostic.column + 1);
        const float locationWidth = ImGui::CalcTextSize(location).x;
        drawList->AddText(ImVec2(rowMin.x + ImGui::GetContentRegionAvail().x - locationWidth - 6.0f, rowMin.y + 1.0f),
                          ImGui::GetColorU32(ImGuiCol_TextDisabled), location);
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void ColorTextEditBackend::drawStatusBar() {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    int errors = 0;
    int warnings = 0;
    for (const Diagnostic& diagnostic : diagnostics_) (diagnostic.warning ? warnings : errors)++;

    const ImVec2 min = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    const ImVec2 max(min.x + ImGui::GetContentRegionAvail().x, min.y + height);
    const ImVec4 accent = ui::accent();
    ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::GetColorU32(ImVec4(accent.x * 0.35f, accent.y * 0.35f, accent.z * 0.35f, 0.55f)));
    ImGui::SetCursorScreenPos(ImVec2(min.x + 10.0f, min.y + (height - ImGui::GetTextLineHeight()) * 0.5f));

    ImGui::BeginGroup();
    const ImVec2 errorPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(10.0f, ImGui::GetTextLineHeight()));
    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(errorPos.x + 5.0f, errorPos.y + ImGui::GetTextLineHeight() * 0.5f), 4.0f,
                                                errors > 0 ? IM_COL32(229, 83, 75, 255) : IM_COL32(120, 128, 140, 255));
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::Text("%d", errors);
    ImGui::SameLine(0.0f, 10.0f);
    const ImVec2 warnPos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(12.0f, ImGui::GetTextLineHeight()));
    drawIcon(ImGui::GetWindowDrawList(), Icon::Warning, ImVec2(warnPos.x + 6.0f, warnPos.y + ImGui::GetTextLineHeight() * 0.5f), 12.0f,
             warnings > 0 ? IM_COL32(232, 169, 59, 255) : IM_COL32(120, 128, 140, 255));
    ImGui::SameLine(0.0f, 4.0f);
    ImGui::Text("%d", warnings);
    ImGui::EndGroup();
    if (ImGui::IsItemClicked()) g_prefs.problemsOpen = !g_prefs.problemsOpen;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("Toggle the Problems panel");

    ImGui::SameLine(0.0f, 18.0f);
    ImGui::Text("Ln %d, Col %d", cursor.mLine + 1, cursor.mColumn + 1);
    ImGui::SameLine(0.0f, 18.0f);
    ImGui::TextDisabled("%d lines", editor_->GetTotalLines());
    if (!extraCarets_.empty()) {
        ImGui::SameLine(0.0f, 18.0f);
        ImGui::TextColored(accent, "%d cursors", static_cast<int>(extraCarets_.size()) + 1);
    }
    if (!notice_.empty() && ImGui::GetTime() < noticeUntil_) {
        ImGui::SameLine(0.0f, 18.0f);
        ImGui::TextUnformatted(notice_.c_str());
    }

    char right[96];
    std::snprintf(right, sizeof(right), "Luau   UTF-8   Tab %d   %s", kTabSize, editor_->IsOverwrite() ? "OVR" : "INS");
    const float rightWidth = ImGui::CalcTextSize(right).x;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 16.0f, ImGui::GetContentRegionMax().x - rightWidth - 10.0f));
    ImGui::TextDisabled("%s", right);
    ImGui::SetCursorScreenPos(ImVec2(min.x, max.y));
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

} // namespace engine::studio::panels
