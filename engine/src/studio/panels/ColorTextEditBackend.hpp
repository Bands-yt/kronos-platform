#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include "studio/panels/ScriptEditorPanel.hpp"

class TextEditor;

namespace engine::studio::panels {

struct LuauLiveAnalyzer;

// The Studio script editor: ImGuiColorTextEdit for editing plus Luau.Analysis
// for live type errors, lint warnings, autocomplete and signature help against
// the engine API declared in assets/luau/kronos_globals.d.lua. The editor's
// scroll region is owned here (SetImGuiChildIgnored) so popups, find
// highlights and squiggles can be placed at real caret/line positions.
class ColorTextEditBackend final : public IScriptEditorBackend {
public:
    ColorTextEditBackend();
    ~ColorTextEditBackend() override;

    [[nodiscard]] bool initialize() override;
    void shutdown() override;

    void setSource(const std::string& source) override;
    [[nodiscard]] const std::string& source() const override;

    void draw() override;
    [[nodiscard]] const char* backendName() const override { return "Kronos Script Editor (Luau)"; }

    void moveCaretToLine(int oneBasedLine) override;
    void setHooks(ScriptEditorHooks hooks) override { hooks_ = std::move(hooks); }
    void selectSourceRange(int line, int byteStart, int byteEnd) override;
    [[nodiscard]] std::set<int> breakpoints() const override { return breakpoints_; }
    void setBreakpoints(const std::set<int>& lines) override { breakpoints_ = lines; }
    void setExecutionLine(int oneBasedLine) override { executionLine_ = oneBasedLine; }

    // A caret plus the other end of its selection, as flat byte offsets.
    struct Caret {
        int anchor = 0;
        int position = 0;
    };
    struct TextEdit {
        int start = 0;
        int end = 0;
        std::string text;
    };
    // Applies edits (any order, overlaps dropped) and maps each caret through them.
    [[nodiscard]] static std::string applyEdits(const std::string& text, std::vector<TextEdit> edits, std::vector<Caret>& carets);

    enum class CompletionKind { Keyword, Variable, Function, Method, Property, Table, Type, Module, Snippet };

    struct CompletionItem {
        std::string label;
        std::string insertText;
        std::string filterText;
        std::string detail;
        std::string doc;
        CompletionKind kind = CompletionKind::Variable;
        bool callable = false;
        bool cursorInsideParens = false;
        bool typeCorrect = false;
        int score = 0;
    };

    struct Diagnostic {
        int line = 0; // 0-based
        int column = 0; // byte offsets within the line
        int endLine = 0;
        int endColumn = 0;
        std::string message;
        bool warning = false;
    };

    struct CallContext {
        bool active = false;
        std::string callee;
        int calleeEnd = 0; // byte index just past the callee name
        int argumentIndex = 0;
    };

    struct Symbol {
        std::string name;
        int line = 0;
    };

    // Pure text helpers, exposed for tests.
    [[nodiscard]] static int identifierPrefixStart(const std::string& lineText, int column);
    [[nodiscard]] static bool completionAnchorValid(int anchorLine, int anchorColumn, int cursorLine, int cursorColumn,
                                                     const std::string& cursorLineText);
    // Higher is better; -1 when `pattern` is not a (case-insensitive) subsequence of `candidate`.
    [[nodiscard]] static int fuzzyScore(const std::string& candidate, const std::string& pattern);
    [[nodiscard]] static int byteToColumn(const std::string& line, int byteIndex, int tabSize);
    [[nodiscard]] static int columnToByte(const std::string& line, int column, int tabSize);
    // Comments every line in [first, last] with "-- ", or uncomments them all when they already are.
    static void toggleLineComments(std::vector<std::string>& lines, int first, int last);
    // True when `byte` sits inside a string literal or after a line comment.
    [[nodiscard]] static bool positionInCommentOrString(const std::string& line, int byte);
    [[nodiscard]] static CallContext findCallContext(const std::string& lineText, int cursorByte);
    [[nodiscard]] static std::vector<Symbol> findSymbols(const std::vector<std::string>& lines);
    // "--- text" lines above `name:` members (or `declare function name`) in a definition file.
    [[nodiscard]] static std::unordered_map<std::string, std::string> parseApiDocs(const std::string& definitions);
    // Splits "(a: number, b: string?) -> number" into its top-level parameters and the trailing return part.
    [[nodiscard]] static std::vector<std::string> splitSignatureParameters(const std::string& signature,
                                                                            std::string& returnPart);

private:
    struct FindMatch {
        int line = 0;
        int byteStart = 0;
        int byteEnd = 0;
    };

    void reanalyze();
    void applyThemeIfChanged();
    void drawToolbar();
    void drawFindBar();
    void drawEditor(ImVec2 size);
    void drawOverlays(ImDrawList* drawList, ImVec2 origin, ImVec2 charAdvance, float textStart);
    void drawCompletionPopup();
    void drawSignaturePopup();
    void drawProblems(float height);
    void drawStatusBar();
    void handleShortcuts(bool editorFocused);

    void updateCompletions(bool explicitRequest);
    void refreshCompletionFilter();
    void insertCompletion(const CompletionItem& item);
    void insertSnippet(const std::string& body);
    void updateSignatureHelp();

    void openFind(bool withReplace);
    void recomputeMatches();
    void goToMatch(int index);
    void replaceCurrentMatch();
    void replaceAllMatches();
    void toggleCommentOnSelection();
    void duplicateLine();
    void selectRange(int line, int byteStart, int byteEnd);

    [[nodiscard]] std::string flatText() const;
    [[nodiscard]] int offsetOf(int line, int column) const;
    [[nodiscard]] std::pair<int, int> positionOf(int offset) const; // (line, column)
    [[nodiscard]] Caret primaryCaret() const;
    [[nodiscard]] std::vector<Caret> allCarets() const;
    void setCarets(const std::vector<Caret>& carets);
    void commitEdits(std::vector<TextEdit> edits, std::vector<Caret> carets);
    void editAtCarets(const std::function<TextEdit(const Caret&, const std::string&)>& makeEdit);
    bool handleMultiCursorKeys();
    void addNextOccurrence();
    void addCaretVertically(int direction);
    [[nodiscard]] std::pair<int, int> mouseTextPosition() const;
    void goToDefinitionAt(int line, int byteColumn);
    void beginRename();
    void drawRenamePopup();
    void applyRename();
    void toggleBreakpoint(int oneBasedLine);
    void setNotice(std::string text);
    [[nodiscard]] bool selectionLines(int& firstLine, int& lastLine) const;
    [[nodiscard]] std::string memberContainerBefore(int line, int anchorByte) const;
    [[nodiscard]] ImVec2 caretScreenPos() const;
    [[nodiscard]] bool caretVisible() const;

    std::unique_ptr<TextEditor> editor_;
    std::unique_ptr<LuauLiveAnalyzer> analyzer_;
    mutable std::string sourceCache_;
    std::vector<std::string> lines_;
    uint64_t textVersion_ = 0;

    std::vector<Diagnostic> diagnostics_;
    std::vector<Symbol> symbols_;
    int appliedTheme_ = -1;

    bool showCompletions_ = false;
    bool completionExplicit_ = false;
    std::vector<CompletionItem> completionRawEntries_;
    std::vector<CompletionItem> completionEntries_;
    int completionSelected_ = 0;
    int completionAnchorLine_ = 0;
    int completionAnchorColumn_ = 0;

    CallContext callContext_;
    std::string signatureKey_;
    std::string signatureName_;
    std::vector<std::string> signatureParams_;
    std::string signatureReturn_;
    std::string signatureDoc_;

    bool findOpen_ = false;
    bool replaceOpen_ = false;
    bool focusFindInput_ = false;
    bool matchCase_ = false;
    bool wholeWord_ = false;
    std::string findQuery_;
    std::string replaceText_;
    std::string matchedQuery_;
    uint64_t matchedVersion_ = ~0ull;
    std::vector<FindMatch> matches_;
    int currentMatch_ = -1;

    ScriptEditorHooks hooks_;
    std::set<int> breakpoints_;
    int executionLine_ = 0;
    std::vector<Caret> extraCarets_;
    bool openRename_ = false;
    std::string renameText_;
    int renameLine_ = 0;
    int renameByte_ = 0;
    std::string notice_;
    double noticeUntil_ = 0.0;

    unsigned int editorWindowId_ = 0;
    bool editorWasFocused_ = false;
    bool refocusEditor_ = false;
    bool openSymbolPicker_ = false;
    ImVec2 editorOrigin_{0.0f, 0.0f};
    ImVec2 charAdvance_{8.0f, 16.0f};
    float textStart_ = 0.0f;
    ImVec2 editorClipMin_{0.0f, 0.0f};
    ImVec2 editorClipMax_{0.0f, 0.0f};
};

} // namespace engine::studio::panels
