#include "studio/panels/ColorTextEditBackend.hpp"

#include <algorithm>
#include <cctype>

#include <imgui.h>
#include <imgui_stdlib.h>
#include <TextEditor.h>

#include "studio/panels/LuauSymbolIndex.hpp"

namespace engine::studio::panels {

namespace {

constexpr int kTabSize = 4;

bool isWordChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

bool isContinuationByte(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

std::string encodeUtf8(unsigned int codepoint) {
    std::string out;
    if (codepoint < 0x80) {
        out += static_cast<char>(codepoint);
    } else if (codepoint < 0x800) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint < 0x10000) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
    return out;
}

} // namespace

std::string ColorTextEditBackend::flatText() const {
    std::string text;
    for (size_t i = 0; i < lines_.size(); ++i) {
        if (i > 0) text += '\n';
        text += lines_[i];
    }
    return text;
}

int ColorTextEditBackend::offsetOf(int line, int column) const {
    if (lines_.empty()) return 0;
    line = std::clamp(line, 0, static_cast<int>(lines_.size()) - 1);
    int offset = 0;
    for (int i = 0; i < line; ++i) offset += static_cast<int>(lines_[static_cast<size_t>(i)].size()) + 1;
    const std::string& text = lines_[static_cast<size_t>(line)];
    return offset + std::min(columnToByte(text, std::max(0, column), kTabSize), static_cast<int>(text.size()));
}

std::pair<int, int> ColorTextEditBackend::positionOf(int offset) const {
    int line = 0;
    while (line + 1 < static_cast<int>(lines_.size()) && offset > static_cast<int>(lines_[static_cast<size_t>(line)].size())) {
        offset -= static_cast<int>(lines_[static_cast<size_t>(line)].size()) + 1;
        ++line;
    }
    if (lines_.empty()) return {0, 0};
    const std::string& text = lines_[static_cast<size_t>(line)];
    return {line, byteToColumn(text, std::clamp(offset, 0, static_cast<int>(text.size())), kTabSize)};
}

ColorTextEditBackend::Caret ColorTextEditBackend::primaryCaret() const {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    Caret caret;
    caret.position = caret.anchor = offsetOf(cursor.mLine, cursor.mColumn);
    if (!editor_->HasSelection()) return caret;
    const std::string selected = editor_->GetSelectedText();
    const int length = static_cast<int>(selected.size());
    const std::string text = flatText();
    if (caret.position >= length && text.compare(static_cast<size_t>(caret.position - length), selected.size(), selected) == 0) {
        caret.anchor = caret.position - length;
    } else {
        caret.anchor = std::min(caret.position + length, static_cast<int>(text.size()));
    }
    return caret;
}

std::vector<ColorTextEditBackend::Caret> ColorTextEditBackend::allCarets() const {
    std::vector<Caret> carets{primaryCaret()};
    carets.insert(carets.end(), extraCarets_.begin(), extraCarets_.end());
    return carets;
}

void ColorTextEditBackend::setCarets(const std::vector<Caret>& carets) {
    if (carets.empty()) return;
    auto coords = [&](int offset) {
        const auto [line, column] = positionOf(offset);
        return TextEditor::Coordinates(line, column);
    };
    const Caret& primary = carets.front();
    editor_->SetCursorPosition(coords(primary.position));
    editor_->SetSelection(coords(std::min(primary.anchor, primary.position)), coords(std::max(primary.anchor, primary.position)));
    extraCarets_.clear();
    for (size_t i = 1; i < carets.size(); ++i) {
        const Caret& caret = carets[i];
        const bool duplicate = caret.position == primary.position ||
                               std::any_of(extraCarets_.begin(), extraCarets_.end(),
                                           [&](const Caret& other) { return other.position == caret.position; });
        if (!duplicate) extraCarets_.push_back(caret);
    }
}

void ColorTextEditBackend::commitEdits(std::vector<TextEdit> edits, std::vector<Caret> carets) {
    if (edits.empty()) return;
    const std::string before = flatText();
    std::vector<Caret> mapped = carets;
    std::vector<TextEdit> ordered = edits;
    (void)applyEdits(before, edits, mapped);

    std::sort(ordered.begin(), ordered.end(), [](const TextEdit& a, const TextEdit& b) { return a.start > b.start; });
    int floor = static_cast<int>(before.size()) + 1;
    for (const TextEdit& edit : ordered) {
        if (edit.end > floor || (edit.start == edit.end && edit.text.empty())) continue;
        floor = edit.start;
        const auto [startLine, startColumn] = positionOf(edit.start);
        const auto [endLine, endColumn] = positionOf(edit.end);
        const TextEditor::Coordinates start(startLine, startColumn);
        const TextEditor::Coordinates end(endLine, endColumn);
        editor_->SetCursorPosition(end);
        editor_->SetSelection(start, end);
        if (edit.end > edit.start) editor_->Delete();
        editor_->SetCursorPosition(start);
        editor_->SetSelection(start, start);
        if (!edit.text.empty()) editor_->InsertText(edit.text);
    }
    reanalyze();
    setCarets(mapped);
}

void ColorTextEditBackend::editAtCarets(const std::function<TextEdit(const Caret&, const std::string&)>& makeEdit) {
    const std::string text = flatText();
    std::vector<Caret> carets = allCarets();
    std::vector<TextEdit> edits;
    for (const Caret& caret : carets) edits.push_back(makeEdit(caret, text));
    commitEdits(std::move(edits), std::move(carets));
}

bool ColorTextEditBackend::handleMultiCursorKeys() {
    ImGuiIO& io = ImGui::GetIO();
    const bool ctrl = io.KeyCtrl;
    const bool shift = io.KeyShift;
    auto pressed = [](ImGuiKey key) { return ImGui::IsKeyPressed(key, true); };

    if (ctrl && (pressed(ImGuiKey_Z) || pressed(ImGuiKey_Y) || pressed(ImGuiKey_A))) {
        extraCarets_.clear();
        return false;
    }
    if (pressed(ImGuiKey_Escape)) {
        extraCarets_.clear();
        return true;
    }

    auto replaceSelection = [](const std::string& insert) {
        return [insert](const Caret& caret, const std::string&) {
            return TextEdit{std::min(caret.anchor, caret.position), std::max(caret.anchor, caret.position), insert};
        };
    };

    if (ctrl && (pressed(ImGuiKey_C) || pressed(ImGuiKey_X))) {
        const std::string text = flatText();
        std::vector<Caret> carets = allCarets();
        std::sort(carets.begin(), carets.end(), [](const Caret& a, const Caret& b) { return a.position < b.position; });
        std::string clip;
        for (size_t i = 0; i < carets.size(); ++i) {
            const int from = std::min(carets[i].anchor, carets[i].position);
            const int to = std::max(carets[i].anchor, carets[i].position);
            if (i > 0) clip += '\n';
            clip += text.substr(static_cast<size_t>(from), static_cast<size_t>(to - from));
        }
        ImGui::SetClipboardText(clip.c_str());
        if (ImGui::IsKeyPressed(ImGuiKey_X, false)) editAtCarets(replaceSelection(""));
        return true;
    }
    if (ctrl && pressed(ImGuiKey_V)) {
        const char* clipboard = ImGui::GetClipboardText();
        std::string clip = clipboard != nullptr ? clipboard : "";
        std::vector<std::string> pieces;
        for (size_t start = 0;;) {
            const size_t end = clip.find('\n', start);
            pieces.push_back(clip.substr(start, end == std::string::npos ? std::string::npos : end - start));
            if (end == std::string::npos) break;
            start = end + 1;
        }
        std::vector<Caret> carets = allCarets();
        if (pieces.size() == carets.size()) {
            std::vector<int> order(carets.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = static_cast<int>(i);
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                return carets[static_cast<size_t>(a)].position < carets[static_cast<size_t>(b)].position;
            });
            std::vector<TextEdit> edits(carets.size());
            for (size_t rank = 0; rank < order.size(); ++rank) {
                const Caret& caret = carets[static_cast<size_t>(order[rank])];
                edits[static_cast<size_t>(order[rank])] = {std::min(caret.anchor, caret.position),
                                                           std::max(caret.anchor, caret.position), pieces[rank]};
            }
            commitEdits(std::move(edits), std::move(carets));
        } else {
            editAtCarets(replaceSelection(clip));
        }
        return true;
    }

    bool edited = false;
    for (int i = 0; i < io.InputQueueCharacters.Size; ++i) {
        const unsigned int c = io.InputQueueCharacters[i];
        if (c == 0 || (c < 32 && c != '\t')) continue;
        editAtCarets(replaceSelection(encodeUtf8(c)));
        edited = true;
    }
    if (edited) return true;

    if (pressed(ImGuiKey_Tab)) {
        editAtCarets(replaceSelection("\t"));
        return true;
    }
    if (pressed(ImGuiKey_Enter) || pressed(ImGuiKey_KeypadEnter)) {
        editAtCarets([](const Caret& caret, const std::string& text) {
            const int from = std::min(caret.anchor, caret.position);
            size_t lineStart = text.rfind('\n', from > 0 ? static_cast<size_t>(from - 1) : 0);
            lineStart = (lineStart == std::string::npos || from == 0) ? 0 : lineStart + 1;
            size_t indentEnd = lineStart;
            while (indentEnd < static_cast<size_t>(from) && (text[indentEnd] == ' ' || text[indentEnd] == '\t')) ++indentEnd;
            return TextEdit{from, std::max(caret.anchor, caret.position), "\n" + text.substr(lineStart, indentEnd - lineStart)};
        });
        return true;
    }
    if (pressed(ImGuiKey_Backspace) || pressed(ImGuiKey_Delete)) {
        const bool backspace = ImGui::IsKeyPressed(ImGuiKey_Backspace, true);
        editAtCarets([backspace](const Caret& caret, const std::string& text) {
            if (caret.anchor != caret.position) {
                return TextEdit{std::min(caret.anchor, caret.position), std::max(caret.anchor, caret.position), ""};
            }
            int at = caret.position;
            if (backspace) {
                if (at == 0) return TextEdit{0, 0, ""};
                int start = at - 1;
                while (start > 0 && isContinuationByte(text[static_cast<size_t>(start)])) --start;
                return TextEdit{start, at, ""};
            }
            if (at >= static_cast<int>(text.size())) return TextEdit{at, at, ""};
            int end = at + 1;
            while (end < static_cast<int>(text.size()) && isContinuationByte(text[static_cast<size_t>(end)])) ++end;
            return TextEdit{at, end, ""};
        });
        return true;
    }

    const bool left = pressed(ImGuiKey_LeftArrow);
    const bool right = pressed(ImGuiKey_RightArrow);
    const bool up = !ctrl && pressed(ImGuiKey_UpArrow);
    const bool down = !ctrl && pressed(ImGuiKey_DownArrow);
    const bool home = pressed(ImGuiKey_Home);
    const bool end = pressed(ImGuiKey_End);
    if (left || right || up || down || home || end) {
        const std::string text = flatText();
        std::vector<Caret> carets = allCarets();
        for (Caret& caret : carets) {
            int p = caret.position;
            if (!shift && caret.anchor != caret.position && (left || right)) {
                p = left ? std::min(caret.anchor, caret.position) : std::max(caret.anchor, caret.position);
            } else if (left && p > 0) {
                --p;
                while (p > 0 && isContinuationByte(text[static_cast<size_t>(p)])) --p;
            } else if (right && p < static_cast<int>(text.size())) {
                ++p;
                while (p < static_cast<int>(text.size()) && isContinuationByte(text[static_cast<size_t>(p)])) ++p;
            } else if (up || down) {
                const auto [line, column] = positionOf(p);
                p = offsetOf(line + (up ? -1 : 1), column);
                if ((up && line == 0) || (down && line + 1 >= static_cast<int>(lines_.size()))) p = caret.position;
            } else if (home || end) {
                const auto [line, column] = positionOf(p);
                const std::string& lineText = lines_[static_cast<size_t>(line)];
                const int lineStart = offsetOf(line, 0);
                if (end) {
                    p = lineStart + static_cast<int>(lineText.size());
                } else {
                    int indent = 0;
                    while (indent < static_cast<int>(lineText.size()) && (lineText[indent] == ' ' || lineText[indent] == '\t')) ++indent;
                    p = (p == lineStart + indent) ? lineStart : lineStart + indent;
                }
                (void)column;
            }
            caret.position = p;
            if (!shift) caret.anchor = p;
        }
        setCarets(carets);
        return true;
    }

    return ctrl ? false : true;
}

void ColorTextEditBackend::addNextOccurrence() {
    const std::string text = flatText();
    Caret primary = primaryCaret();
    if (primary.anchor == primary.position) {
        int start = primary.position;
        int finish = primary.position;
        while (start > 0 && isWordChar(text[static_cast<size_t>(start - 1)])) --start;
        while (finish < static_cast<int>(text.size()) && isWordChar(text[static_cast<size_t>(finish)])) ++finish;
        if (start == finish) return;
        std::vector<Caret> carets{{start, finish}};
        carets.insert(carets.end(), extraCarets_.begin(), extraCarets_.end());
        setCarets(carets);
        return;
    }
    const Caret& last = extraCarets_.empty() ? primary : extraCarets_.back();
    const int from = std::min(last.anchor, last.position);
    const int to = std::max(last.anchor, last.position);
    const std::string needle = text.substr(static_cast<size_t>(from), static_cast<size_t>(to - from));
    if (needle.empty()) return;
    std::vector<Caret> carets = allCarets();
    size_t at = text.find(needle, static_cast<size_t>(to));
    if (at == std::string::npos) at = text.find(needle);
    for (size_t guard = 0; at != std::string::npos && guard < 4096; ++guard) {
        const int start = static_cast<int>(at);
        const bool taken = std::any_of(carets.begin(), carets.end(),
                                       [&](const Caret& c) { return std::min(c.anchor, c.position) == start; });
        if (!taken) {
            carets.push_back({start, start + static_cast<int>(needle.size())});
            setCarets(carets);
            return;
        }
        at = text.find(needle, at + 1);
        if (at == std::string::npos && guard == 0) at = text.find(needle);
    }
    setNotice("No more occurrences of \"" + needle + "\"");
}

void ColorTextEditBackend::addCaretVertically(int direction) {
    std::vector<Caret> carets = allCarets();
    const Caret* edge = &carets.front();
    for (const Caret& caret : carets) {
        if ((direction < 0 && caret.position < edge->position) || (direction > 0 && caret.position > edge->position)) edge = &caret;
    }
    const auto [line, column] = positionOf(edge->position);
    const int target = line + direction;
    if (target < 0 || target >= static_cast<int>(lines_.size())) return;
    const int offset = offsetOf(target, column);
    carets.push_back({offset, offset});
    setCarets(carets);
}

std::pair<int, int> ColorTextEditBackend::mouseTextPosition() const {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int line = std::clamp(static_cast<int>((mouse.y - editorOrigin_.y) / charAdvance_.y), 0,
                                std::max(0, static_cast<int>(lines_.size()) - 1));
    const int column = std::max(0, static_cast<int>((mouse.x - editorOrigin_.x - textStart_) / charAdvance_.x + 0.5f));
    if (lines_.empty()) return {0, 0};
    const std::string& text = lines_[static_cast<size_t>(line)];
    return {line, std::min(columnToByte(text, column, kTabSize), static_cast<int>(text.size()))};
}

void ColorTextEditBackend::selectSourceRange(int line, int byteStart, int byteEnd) {
    if (lines_.empty()) lines_ = editor_->GetTextLines();
    if (line < 0 || line >= static_cast<int>(lines_.size())) return;
    extraCarets_.clear();
    selectRange(line, byteStart, byteEnd);
    refocusEditor_ = true;
}

void ColorTextEditBackend::goToDefinitionAt(int line, int byteColumn) {
    const std::optional<luau_symbols::SymbolInfo> info = luau_symbols::symbolAt(flatText(), line, byteColumn);
    if (!info) {
        setNotice("No symbol under the caret");
        return;
    }
    if (info->definition) {
        const luau_symbols::Range& range = *info->definition;
        selectSourceRange(range.line, range.column, range.endColumn);
        return;
    }
    if (info->globalRef && hooks_.goToGlobalDefinition) {
        hooks_.goToGlobalDefinition(*info->globalRef);
        return;
    }
    setNotice("\"" + info->name + "\" has no definition in this script");
}

void ColorTextEditBackend::beginRename() {
    const TextEditor::Coordinates cursor = editor_->GetCursorPosition();
    if (lines_.empty()) return;
    const std::string& text = lines_[static_cast<size_t>(cursor.mLine)];
    const int byte = columnToByte(text, cursor.mColumn, kTabSize);
    const std::optional<luau_symbols::SymbolInfo> info = luau_symbols::symbolAt(flatText(), cursor.mLine, byte);
    if (!info) {
        setNotice("Put the caret on a name to rename it");
        return;
    }
    renameLine_ = cursor.mLine;
    renameByte_ = byte;
    renameText_ = info->name;
    openRename_ = true;
}

void ColorTextEditBackend::applyRename() {
    const std::string text = flatText();
    const std::optional<luau_symbols::SymbolInfo> info = luau_symbols::symbolAt(text, renameLine_, renameByte_);
    if (!info) return;
    if (!luau_symbols::isValidIdentifier(renameText_)) {
        setNotice("\"" + renameText_ + "\" is not a valid Luau name");
        return;
    }
    if (renameText_ == info->name) return;
    std::vector<TextEdit> edits;
    for (const luau_symbols::Range& range : info->occurrences) {
        const int lineStart = offsetOf(range.line, 0);
        edits.push_back({lineStart + range.column, lineStart + range.endColumn, renameText_});
    }
    extraCarets_.clear();
    commitEdits(std::move(edits), {primaryCaret()});
    if (info->globalRef && hooks_.renameGlobalElsewhere) hooks_.renameGlobalElsewhere(*info->globalRef, renameText_);
    setNotice("Renamed " + std::to_string(info->occurrences.size()) + " occurrence" +
              (info->occurrences.size() == 1 ? "" : "s") + " of \"" + info->name + "\"");
}

void ColorTextEditBackend::drawRenamePopup() {
    if (openRename_) {
        ImGui::OpenPopup("##rename_symbol");
        openRename_ = false;
    }
    const ImVec2 caret = caretScreenPos();
    ImGui::SetNextWindowPos(ImVec2(caret.x, caret.y + charAdvance_.y + 2.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("##rename_symbol")) {
        ImGui::TextDisabled("Rename symbol");
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(220.0f);
        const bool submit = ImGui::InputText("##rename_input", &renameText_,
                                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        ImGui::TextDisabled("Enter to apply, Esc to cancel");
        if (submit) {
            applyRename();
            ImGui::CloseCurrentPopup();
            refocusEditor_ = true;
        }
        ImGui::EndPopup();
    }
}

void ColorTextEditBackend::toggleBreakpoint(int oneBasedLine) {
    if (!breakpoints_.erase(oneBasedLine)) breakpoints_.insert(oneBasedLine);
}

void ColorTextEditBackend::setNotice(std::string text) {
    notice_ = std::move(text);
    noticeUntil_ = ImGui::GetTime() + 4.0;
}

} // namespace engine::studio::panels
