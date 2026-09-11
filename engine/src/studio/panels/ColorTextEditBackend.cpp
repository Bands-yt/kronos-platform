#include "studio/panels/ColorTextEditBackend.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include <imgui.h>
#include <TextEditor.h>

#include "Luau/Autocomplete.h"
#include "Luau/BuiltinDefinitions.h"
#include "Luau/Frontend.h"
#include "Luau/TypeArena.h"

#include "core/ResourcePaths.hpp"

namespace engine::studio::panels {

namespace {

// Not a real path -- Luau.Analysis identifies modules by name, not
// filesystem location, and this editor only ever has one buffer, backed
// by whatever core::Script component is currently selected (see
// ScriptEditorPanel::loadFromEntity()), never a real file on disk.
constexpr const char* kBufferModuleName = "=script";

// Kronos: the in-memory FileResolver Luau.Analysis needs. readSource()
// is FileResolver's only pure-virtual method (Luau/FileResolver.h) --
// returns whatever the editor buffer currently holds instead of reading
// a real file. Mirrors the shape of Luau's own CLI resolver
// (CLI/src/Analyze.cpp's CliFileResolver) with everything real projects
// need for `require()` resolution stripped out: this engine's scripts
// are single Script components, not multi-file Luau projects, so
// resolveModule() keeps FileResolver's own real "no result" default.
struct BufferFileResolver final : Luau::FileResolver {
    std::string source;

    std::optional<Luau::SourceCode> readSource(const Luau::ModuleName& name) override {
        if (name != kBufferModuleName) return std::nullopt;
        return Luau::SourceCode{source, Luau::SourceCode::Script};
    }
};

// Real Luau reserved-word set (same list ScriptEditorPanel.cpp's own
// luauKeywords() already established for the fallback editor's
// tokenizer -- see https://luau.org/syntax#keywords). Small enough (22
// words) that duplicating it here beats sharing a header across two
// otherwise-independent backends just to avoid repeating a literal list.
TextEditor::LanguageDefinition luauLanguageDefinition() {
    static bool inited = false;
    static TextEditor::LanguageDefinition langDef;
    if (!inited) {
        static const char* const keywords[] = {
            "and", "break", "do",       "else",  "elseif", "end",    "false", "for",     "function", "if",   "in",
            "local", "nil", "not",      "or",    "repeat", "return", "then",  "true",    "until",    "while", "continue",
        };
        for (const char* keyword : keywords) langDef.mKeywords.insert(keyword);

        langDef.mTokenRegexStrings.emplace_back("\"(\\\\.|[^\"])*\"", TextEditor::PaletteIndex::String);
        langDef.mTokenRegexStrings.emplace_back("\'(\\\\.|[^\'])*\'", TextEditor::PaletteIndex::String);
        langDef.mTokenRegexStrings.emplace_back("[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?",
                                                  TextEditor::PaletteIndex::Number);
        langDef.mTokenRegexStrings.emplace_back("[a-zA-Z_][a-zA-Z0-9_]*", TextEditor::PaletteIndex::Identifier);
        langDef.mTokenRegexStrings.emplace_back("[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.\\:]",
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

// Kronos ("Luau Global Type-Checking Gap"): real engine API definitions
// -- see assets/luau/kronos_globals.d.lua's own header comment for what
// this covers and why it's an honest transcription of the real C++-side
// globals, not a guessed/Roblox-shaped one. Same real "packaged sibling
// dir, else fall back to this compile-time source path" resolution
// every other Studio asset load already uses (core/ResourcePaths.hpp),
// so this works both from engine/build/ (dev convention) and from
// inside a packaged Alpha build.
std::string kronosGlobalDefinitionsPath() {
    return core::resolveResourceDir(core::executableDirectory(), "assets", ENGINE_ASSET_DIR) + "/luau/kronos_globals.d.lua";
}

// Real file read -- same plain std::ifstream(path, std::ios::binary)
// convention studio/plugins/ScriptedPlugin.cpp's own script-loading path
// already uses, not a new pattern. Returns std::nullopt (not an empty
// string) on any failure to open, so the caller can tell "missing file"
// apart from "genuinely empty file".
std::optional<std::string> readFileToString(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

} // namespace

// Kronos: real Luau.Analysis wiring, using the exact Frontend/
// FileResolver/ConfigResolver shape Luau's own CLI (CLI/src/Analyze.cpp)
// uses -- that file is the precedent this mirrors, trimmed to a single
// in-memory module. NullConfigResolver (Luau/ConfigResolver.h) is Luau's
// own real default-config resolver -- its Config::mode defaults to
// Mode::Nonstrict (Luau/Config.h), so a script without a `--!strict`
// hot-comment gets the same permissive checking Luau gives any
// unannotated script: real syntax/parse errors and real mismatches on
// explicitly-typed locals surface.
//
// Kronos ("Luau Global Type-Checking Gap"): this engine's own script API
// surface (`world`, `avatar`, `network`, `ui`, `TextChatService`,
// `events`, `task`, `engine`, plus `print`/`require`) is now described to
// the type checker for real, via loadKronosGlobalDefinitions() below --
// an undeclared global like a typo'd `workd.createEntity` is flagged the
// same way a real type mismatch already is. This is a genuine, honest
// engine API surface, not Roblox's `game`/`workspace`/`script` -- those
// don't exist in this codebase (see assets/luau/kronos_globals.d.lua's
// own header comment).
struct LuauLiveAnalyzer {
    BufferFileResolver fileResolver;
    Luau::NullConfigResolver configResolver;
    Luau::Frontend frontend{&fileResolver, &configResolver};

    LuauLiveAnalyzer() {
        Luau::registerBuiltinGlobals(frontend, frontend.globals);
        loadKronosGlobalDefinitions();
        // Freezing must happen after loadDefinitionFile() -- that call
        // allocates new types (world/network/ui/...) into this same
        // globalTypes arena, which a real, live TypeArena::freeze() (see
        // its own doc comment) would reject as a real, honest use-after-
        // freeze bug, not silently ignore.
        Luau::freeze(frontend.globals.globalTypes);
    }

    // Real, non-fatal on failure -- a missing or unparseable definition
    // file degrades this backend back to exactly what it already was
    // (real syntax/parse-error checking, no engine-global awareness),
    // the same graceful-degradation shape MonacoWebViewEditor's own
    // initialize()-returns-false fallback uses, just one level deeper:
    // this backend still becomes ScriptEditorPanel's chosen backend
    // either way, it just knows less.
    void loadKronosGlobalDefinitions() {
        const std::string path = kronosGlobalDefinitionsPath();
        std::optional<std::string> source = readFileToString(path);
        if (!source) {
            std::fprintf(stderr,
                          "ColorTextEditBackend: could not read Kronos global definitions at \"%s\" -- Script "
                          "Editor will type-check syntax only, without engine-global awareness.\n",
                          path.c_str());
            return;
        }

        Luau::LoadDefinitionFileResult result = frontend.loadDefinitionFile(
            frontend.globals, frontend.globals.globalScope, *source, "kronos_globals",
            /*captureComments*/ false);
        if (!result.success) {
            std::fprintf(stderr,
                          "ColorTextEditBackend: \"%s\" failed to parse/typecheck (%zu error(s)) -- Script Editor "
                          "will type-check syntax only, without engine-global awareness.\n",
                          path.c_str(), result.parseResult.errors.size());
        }
    }

    struct Diagnostic {
        int line; // 1-based -- matches TextEditor::ErrorMarkers' own convention (mErrorMarkers.find(lineNo + 1))
        std::string message;
    };

    std::vector<Diagnostic> analyze(const std::string& source) {
        fileResolver.source = source;
        frontend.markDirty(kBufferModuleName);
        Luau::CheckResult result = frontend.check(kBufferModuleName);

        std::vector<Diagnostic> diagnostics;
        diagnostics.reserve(result.errors.size());
        for (const Luau::TypeError& error : result.errors) {
            diagnostics.push_back(Diagnostic{static_cast<int>(error.location.begin.line) + 1, Luau::toString(error)});
        }
        return diagnostics;
    }

    // Real Luau.Analysis autocomplete, mirroring the exact
    // "FrontendOptions{forAutocomplete=true, retainFullTypeGraphs=true};
    // frontend.check(name, opts); Luau::autocomplete(...)" sequence
    // Luau's own test fixture (Analysis/tests/Autocomplete.test.cpp's
    // ACFixtureImpl) uses -- forAutocomplete runs stricter internal
    // typechecking so member/argument suggestions carry real inferred
    // types, not just "check() already ran for diagnostics so reuse
    // that" (that check used default, non-autocomplete options).
    // line/column are 0-based, matching TextEditor::Coordinates directly.
    Luau::AutocompleteResult complete(const std::string& source, int line, int column) {
        fileResolver.source = source;
        frontend.markDirty(kBufferModuleName);

        Luau::FrontendOptions options;
        options.forAutocomplete = true;
        options.retainFullTypeGraphs = true;
        frontend.check(kBufferModuleName, options);

        // No `require("...")` string-literal completion in this
        // single-buffer editor (see BufferFileResolver's own comment on
        // why resolveModule() stays a no-op) -- every real call site
        // this callback would matter for already resolves to nothing.
        auto noStringCompletions = [](const std::string&, std::optional<const Luau::ExternType*>,
                                       std::optional<std::string>) -> std::optional<Luau::AutocompleteEntryMap> {
            return std::nullopt;
        };
        return Luau::autocomplete(frontend, kBufferModuleName, Luau::Position{static_cast<unsigned>(line), static_cast<unsigned>(column)},
                                   noStringCompletions);
    }
};

ColorTextEditBackend::ColorTextEditBackend() = default;
ColorTextEditBackend::~ColorTextEditBackend() = default;

bool ColorTextEditBackend::initialize() {
    editor_ = std::make_unique<TextEditor>();
    editor_->SetLanguageDefinition(luauLanguageDefinition());
    editor_->SetPalette(TextEditor::GetDarkPalette());
    analyzer_ = std::make_unique<LuauLiveAnalyzer>();
    return true; // both dependencies are statically linked in, not runtime-optional like MonacoWebViewEditor's webview
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
    return sourceCache_;
}

void ColorTextEditBackend::moveCaretToLine(int oneBasedLine) {
    const int zeroBasedLine = std::max(0, oneBasedLine - 1);
    const TextEditor::Coordinates target(zeroBasedLine, 0);
    editor_->SetCursorPosition(target);
    editor_->SetSelection(target, target);
}

void ColorTextEditBackend::reanalyze() {
    TextEditor::ErrorMarkers markers;
    for (const LuauLiveAnalyzer::Diagnostic& diagnostic : analyzer_->analyze(editor_->GetText())) {
        markers[diagnostic.line] = diagnostic.message;
    }
    editor_->SetErrorMarkers(markers);
}

void ColorTextEditBackend::draw() {
    ImGuiIO& io = ImGui::GetIO();
    // ImGuiFocusedFlags_ChildWindows: at this point in the frame the
    // editor's own child window (created inside Render() below) hasn't
    // been (re-)entered yet, so this reads back last frame's focus state
    // -- the standard way to gate a shortcut to "this panel's text area
    // has focus" without needing Render() to have run first.
    const bool editorFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    const bool triggerKey = editorFocused && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Space, false);
    const bool acceptKey =
        showCompletions_ && !completionEntries_.empty() &&
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_Tab, false));
    const bool dismissKey = showCompletions_ && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    const bool navUp = showCompletions_ && ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);
    const bool navDown = showCompletions_ && ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);

    // Suppress the editor's own handling of exactly these keys for the
    // one frame they're pressed -- HandleKeyboardInputs() (called inside
    // Render()) has no "someone else already consumed this" concept, so
    // without this an accepted Enter would also insert a newline and an
    // Up/Down nav would also move the real text cursor underneath the
    // popup. Ordinary typing (including narrowing the completion filter
    // further) is unaffected and still reaches the editor normally.
    const bool suppressThisFrame = triggerKey || acceptKey || dismissKey || navUp || navDown;
    editor_->SetHandleKeyboardInputs(!suppressThisFrame);

    constexpr float kCompletionStripHeight = 110.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const bool stripVisible = showCompletions_ && !completionEntries_.empty();
    const ImVec2 editorSize =
        stripVisible ? ImVec2(avail.x, std::max(0.0f, avail.y - kCompletionStripHeight)) : avail;
    editor_->Render("##luau_source", editorSize);
    editor_->SetHandleKeyboardInputs(true); // only ever suppressed for the Render() call just above

    // Render() resets its own internal "changed this frame" flag at
    // entry and sets it if an edit happened during the call just above
    // (TextEditor.cpp's own Render()/HandleKeyboardInputs() flow) -- so
    // checking it immediately after, rather than polling every frame
    // regardless, only re-typechecks on a real edit.
    if (editor_->IsTextChanged()) {
        reanalyze();
        // Auto-open the same Ctrl+Space suggestion list right after '.' or
        // ':' -- member/method access is worth reflowing on every
        // keystroke for (Luau::autocomplete() naturally returns a narrow,
        // high-value list there: a table/class's own fields), unlike a
        // bare letter mid-identifier, which would retypecheck the whole
        // buffer on every keystroke for little benefit -- see
        // updateCompletions()'s own Ctrl+Space framing.
        const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
        const std::string line = editor_->GetCurrentLineText();
        // Gate the whole chain below (auto-close pairing/skip-over and the
        // '.'/':'  completion trigger) on "exactly one character was typed
        // this frame" -- IsTextChanged() alone also fires for paste, Undo/
        // Redo, and insertCompletion()'s own InsertText(), none of which
        // should be mistaken for a fresh keystroke (a skip-over match on
        // pasted text would silently eat a pasted character; Undo/Redo
        // mutating the buffer here would desync the editor's own undo
        // stack).
        const bool singleCharTyped = io.InputQueueCharacters.Size == 1;
        if (singleCharTyped && cursor.mColumn > 0 && cursor.mColumn <= static_cast<int>(line.size())) {
            const char lastTyped = line[cursor.mColumn - 1];
            const char nextChar = cursor.mColumn < static_cast<int>(line.size()) ? line[cursor.mColumn] : '\0';

            // Kronos ("Script Editor QoL" -- auto-closing brackets/
            // quotes): real, minimal pair-insertion -- ImGuiColorTextEdit
            // itself has no built-in support for this (see TextEditor.h's
            // own public API surface), so this watches the same
            // IsTextChanged()-plus-last-typed-character shape the '.'/':'
            // trigger above already uses. Typing a closer (or a quote,
            // whose opener==closer) that's immediately followed by that
            // same character steps over it instead of inserting a
            // duplicate -- the standard "typing through your own
            // auto-close" case; any other opener/quote gets its matching
            // closer inserted right after, with the caret left between
            // the pair.
            const bool isQuote = (lastTyped == '"' || lastTyped == '\'');
            const bool isCloser = (lastTyped == ')' || lastTyped == ']' || lastTyped == '}');
            if ((isQuote || isCloser) && nextChar == lastTyped) {
                const TextEditor::Coordinates typedStart(cursor.mLine, cursor.mColumn - 1);
                editor_->SetSelection(typedStart, cursor);
                editor_->Delete();
                // SetCursorPosition() alone doesn't clear the selection
                // range Delete() just consumed (mSelectionStart/End are
                // separate state) -- left stale, the very next EnterCharacter()
                // would see HasSelection() still true and delete a character
                // out from under the next keystroke.
                const TextEditor::Coordinates target(cursor.mLine, cursor.mColumn);
                editor_->SetCursorPosition(target);
                editor_->SetSelection(target, target);
                reanalyze();
            } else if (isQuote || lastTyped == '(' || lastTyped == '[' || lastTyped == '{') {
                char closer = '\0';
                switch (lastTyped) {
                    case '(': closer = ')'; break;
                    case '[': closer = ']'; break;
                    case '{': closer = '}'; break;
                    case '"': closer = '"'; break;
                    case '\'': closer = '\''; break;
                    default: break;
                }
                editor_->InsertText(std::string(1, closer));
                // step back between the just-inserted pair; InsertText() also
                // leaves no stray selection, but set one explicitly anyway so
                // this doesn't depend on that implementation detail.
                editor_->SetCursorPosition(cursor);
                editor_->SetSelection(cursor, cursor);
                reanalyze();
            } else if (lastTyped == '.' || lastTyped == ':') {
                updateCompletions();
            }
        }
    }
    // Runs every frame the strip is visible (not just on text change) so a mouse click that
    // moves the caret without editing text still gets caught before insertCompletion() could act on it.
    if (showCompletions_) refreshCompletionFilter();

    if (triggerKey) {
        updateCompletions();
    } else if (dismissKey) {
        showCompletions_ = false;
    } else if (acceptKey) {
        insertCompletion(completionEntries_[completionSelected_]);
    } else if (navUp) {
        completionSelected_ = std::max(0, completionSelected_ - 1);
    } else if (navDown) {
        completionSelected_ = std::min(static_cast<int>(completionEntries_.size()) - 1, completionSelected_ + 1);
    }

    if (showCompletions_ && !completionEntries_.empty()) {
        ImGui::TextDisabled("Ctrl+Space suggestions -- Enter/Tab to insert, Esc to dismiss");
        ImGui::BeginChild("##luau_completions", ImVec2(avail.x, kCompletionStripHeight - ImGui::GetFrameHeightWithSpacing()),
                            true);
        for (int i = 0; i < static_cast<int>(completionEntries_.size()); ++i) {
            const bool selected = (i == completionSelected_);
            if (ImGui::Selectable(completionEntries_[i].c_str(), selected)) {
                insertCompletion(completionEntries_[i]);
            }
            if (selected) ImGui::SetScrollHereY();
        }
        ImGui::EndChild();
    }
}

int ColorTextEditBackend::identifierPrefixStart(const std::string& lineText, int column) {
    int prefixStart = std::min<int>(column, static_cast<int>(lineText.size()));
    while (prefixStart > 0 &&
           (std::isalnum(static_cast<unsigned char>(lineText[prefixStart - 1])) || lineText[prefixStart - 1] == '_')) {
        --prefixStart;
    }
    return prefixStart;
}

bool ColorTextEditBackend::completionAnchorValid(int anchorLine, int anchorColumn, int cursorLine, int cursorColumn,
                                                  const std::string& cursorLineText) {
    if (cursorLine != anchorLine) return false;
    if (cursorColumn < anchorColumn) return false;
    return identifierPrefixStart(cursorLineText, cursorColumn) == anchorColumn;
}

void ColorTextEditBackend::updateCompletions() {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    const Luau::AutocompleteResult result = analyzer_->complete(editor_->GetText(), cursor.mLine, cursor.mColumn);

    completionRawEntries_.clear();
    for (const auto& [name, entry] : result.entryMap) {
        (void)entry;
        completionRawEntries_.push_back(name);
    }
    std::sort(completionRawEntries_.begin(), completionRawEntries_.end());

    completionAnchorLine_ = cursor.mLine;
    completionAnchorColumn_ = identifierPrefixStart(editor_->GetCurrentLineText(), cursor.mColumn);
    showCompletions_ = true;
    refreshCompletionFilter();
}

void ColorTextEditBackend::refreshCompletionFilter() {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    const std::string line = editor_->GetCurrentLineText();
    if (!completionAnchorValid(completionAnchorLine_, completionAnchorColumn_, cursor.mLine, cursor.mColumn, line)) {
        showCompletions_ = false;
        return;
    }

    const std::string prefix = line.substr(completionAnchorColumn_, cursor.mColumn - completionAnchorColumn_);
    completionEntries_.clear();
    for (const std::string& name : completionRawEntries_) {
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        completionEntries_.push_back(name);
    }
    constexpr size_t kMaxCompletionEntries = 50;
    if (completionEntries_.size() > kMaxCompletionEntries) completionEntries_.resize(kMaxCompletionEntries);

    completionSelected_ = std::clamp(completionSelected_, 0, std::max(0, static_cast<int>(completionEntries_.size()) - 1));
    showCompletions_ = !completionEntries_.empty();
}

void ColorTextEditBackend::insertCompletion(const std::string& text) {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    if (!completionAnchorValid(completionAnchorLine_, completionAnchorColumn_, cursor.mLine, cursor.mColumn,
                                editor_->GetCurrentLineText())) {
        showCompletions_ = false;
        return;
    }

    if (completionAnchorColumn_ != cursor.mColumn) {
        const TextEditor::Coordinates anchor(completionAnchorLine_, completionAnchorColumn_);
        editor_->SetSelection(anchor, cursor);
        editor_->Delete();
    }
    // anchor == cursor means an empty prefix (e.g. right after "foo.");
    // Delete() with no selection deletes the char AFTER the cursor instead, so skip it.
    editor_->InsertText(text);
    showCompletions_ = false;
    reanalyze();
}

} // namespace engine::studio::panels
