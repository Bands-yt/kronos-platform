#pragma once

#include <memory>
#include <string>
#include <vector>

#include "studio/panels/ScriptEditorPanel.hpp"

class TextEditor;

namespace engine::studio::panels {

struct LuauLiveAnalyzer;

// Kronos ("Studio Revamp" -- "Native Syntax-Highlighting Editor"): the
// real IScriptEditorBackend implementation the Monaco/webview seam
// (see IScriptEditorBackend's own class comment) was always meant to be
// swapped in for -- except this backend needs no embedded webview at
// all. ImGuiColorTextEdit is a native ImGui widget (real line numbers,
// syntax highlighting, an error-marker gutter it renders itself), and
// Luau.Analysis -- already vendored via Dependencies.cmake's FetchContent
// but unused until now -- is Luau's own real type-checker, not a
// hand-rolled linter. Together: real-time error squiggles with no
// Ultralight/CEF licensing dependency (see engine/external/ultralight-sdk/
// README.md for why that path is shelved).
//
// Honesty note on scope: Luau.Analysis here checks the buffer in
// isolation (Mode::Nonstrict, no `--!strict` requirement). This engine's
// own script API globals (`world`, `avatar`, `network`, `ui`,
// `TextChatService`, `events`, `task`, `engine`) ARE declared to the type
// checker -- see assets/luau/kronos_globals.d.lua and
// LuauLiveAnalyzer::loadKronosGlobalDefinitions() in
// ColorTextEditBackend.cpp -- a real, honest transcription of what this
// engine's C++ side actually registers, not Roblox's `game`/`workspace`/
// `script` (those don't exist in this codebase). Real syntax/parse
// errors, real type mismatches on annotated locals, and now real
// unknown-global/wrong-argument-type errors against the engine's own API
// all surface correctly.
class ColorTextEditBackend final : public IScriptEditorBackend {
public:
    ColorTextEditBackend();
    ~ColorTextEditBackend() override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

    void setSource(const std::string& source) override;
    [[nodiscard]] const std::string& source() const override;

    void draw() override;
    [[nodiscard]] const char* backendName() const override {
        return "Native editor (ImGuiColorTextEdit + Luau.Analysis)";
    }

    // Kronos ("Script Editor QoL" -- Engine Console click-to-jump): real
    // caret placement via TextEditor::SetCursorPosition, unlike
    // ImGuiFallbackEditor's plain InputTextMultiline (no addressable
    // cursor API) -- this backend can seek to the exact line.
    // SetCursorPosition() calls the editor's own EnsureCursorVisible()
    // internally, so the jump also scrolls the target line into view.
    void moveCaretToLine(int oneBasedLine) override;

private:
    void reanalyze();

    // Ctrl+Space triggered (not per-keystroke) since Luau::autocomplete() re-typechecks the whole buffer.
    // Fixed strip below the editor, not a caret-tracked popup: ImGuiColorTextEdit exposes no public API
    // for caret pixel position or child-window scroll offset.
    void updateCompletions();
    // Re-filters completionRawEntries_ locally (no re-typecheck); dismisses the strip if the caret has
    // drifted off completionAnchorLine_/Column_ since the last frame (e.g. a mouse click).
    void refreshCompletionFilter();
    void insertCompletion(const std::string& text);

public:
    // Pulled out for direct test coverage, same as InspectorPanel::hasInvalidComponents.
    [[nodiscard]] static int identifierPrefixStart(const std::string& lineText, int column);
    [[nodiscard]] static bool completionAnchorValid(int anchorLine, int anchorColumn, int cursorLine, int cursorColumn,
                                                     const std::string& cursorLineText);

private:
    std::unique_ptr<TextEditor> editor_;
    std::unique_ptr<LuauLiveAnalyzer> analyzer_;
    // source() must return a stable const&; TextEditor::GetText() returns by value.
    mutable std::string sourceCache_;

    bool showCompletions_ = false;
    std::vector<std::string> completionRawEntries_; // unfiltered result of the last updateCompletions() fetch
    std::vector<std::string> completionEntries_;
    int completionSelected_ = 0;
    int completionAnchorLine_ = 0;
    int completionAnchorColumn_ = 0;
};

} // namespace engine::studio::panels
