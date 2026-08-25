#include "studio/panels/ColorTextEditBackend.hpp"

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

#include <imgui.h>
#include <TextEditor.h>

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

void ColorTextEditBackend::reanalyze() {
    TextEditor::ErrorMarkers markers;
    for (const LuauLiveAnalyzer::Diagnostic& diagnostic : analyzer_->analyze(editor_->GetText())) {
        markers[diagnostic.line] = diagnostic.message;
    }
    editor_->SetErrorMarkers(markers);
}

void ColorTextEditBackend::draw() {
    editor_->Render("##luau_source", ImGui::GetContentRegionAvail());
    // Render() resets its own internal "changed this frame" flag at
    // entry and sets it if an edit happened during the call just above
    // (TextEditor.cpp's own Render()/HandleKeyboardInputs() flow) -- so
    // checking it immediately after, rather than polling every frame
    // regardless, only re-typechecks on a real edit.
    if (editor_->IsTextChanged()) reanalyze();
}

} // namespace engine::studio::panels
