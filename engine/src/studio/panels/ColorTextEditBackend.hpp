#pragma once

#include <memory>
#include <string>

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

private:
    void reanalyze();

    std::unique_ptr<TextEditor> editor_;
    std::unique_ptr<LuauLiveAnalyzer> analyzer_;
    // TextEditor::GetText() returns by value; source() must return a
    // stable const& per IScriptEditorBackend's contract, hence this cache
    // (refreshed on every real call, not a stale snapshot).
    mutable std::string sourceCache_;
};

} // namespace engine::studio::panels
