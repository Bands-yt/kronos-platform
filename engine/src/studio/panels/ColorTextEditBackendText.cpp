#include "studio/panels/ColorTextEditBackend.hpp"

#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

// The editor's pure text helpers, kept free of TextEditor/Luau so engine_tests can link them.
namespace engine::studio::panels {

namespace {

bool isIdentifierChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

int utf8Length(unsigned char lead) {
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

std::string trim(const std::string& text) {
    size_t begin = text.find_first_not_of(" \t");
    if (begin == std::string::npos) return {};
    size_t end = text.find_last_not_of(" \t");
    return text.substr(begin, end - begin + 1);
}

} // namespace

bool ColorTextEditBackend::positionInCommentOrString(const std::string& line, int byte) {
    char quote = 0;
    for (int i = 0; i < byte && i < static_cast<int>(line.size()); ++i) {
        char c = line[i];
        if (quote != 0) {
            if (c == '\\') ++i;
            else if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '-' && i + 1 < static_cast<int>(line.size()) && line[i + 1] == '-') {
            return true;
        }
    }
    return quote != 0;
}

int ColorTextEditBackend::identifierPrefixStart(const std::string& lineText, int column) {
    int prefixStart = std::min<int>(column, static_cast<int>(lineText.size()));
    while (prefixStart > 0 && isIdentifierChar(lineText[static_cast<size_t>(prefixStart - 1)])) --prefixStart;
    return prefixStart;
}

bool ColorTextEditBackend::completionAnchorValid(int anchorLine, int anchorColumn, int cursorLine, int cursorColumn,
                                                  const std::string& cursorLineText) {
    if (cursorLine != anchorLine) return false;
    if (cursorColumn < anchorColumn) return false;
    return identifierPrefixStart(cursorLineText, cursorColumn) == anchorColumn;
}

int ColorTextEditBackend::fuzzyScore(const std::string& candidate, const std::string& pattern) {
    if (pattern.empty()) return 1;
    auto lower = [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); };
    if (candidate.compare(0, pattern.size(), pattern) == 0) return 1000 - static_cast<int>(candidate.size());
    if (candidate.size() >= pattern.size() &&
        std::equal(pattern.begin(), pattern.end(), candidate.begin(), [&](char a, char b) { return lower(a) == lower(b); })) {
        return 800 - static_cast<int>(candidate.size());
    }
    int score = 0;
    int previous = -2;
    size_t at = 0;
    for (char wanted : pattern) {
        bool found = false;
        for (; at < candidate.size(); ++at) {
            if (lower(candidate[at]) != lower(wanted)) continue;
            const int index = static_cast<int>(at);
            const bool boundary = index == 0 || candidate[at - 1] == '_' || candidate[at - 1] == '.' ||
                                  (std::isupper(static_cast<unsigned char>(candidate[at])) &&
                                   std::islower(static_cast<unsigned char>(candidate[at - 1])));
            score += index == previous + 1 ? 15 : boundary ? 10 : 1;
            previous = index;
            ++at;
            found = true;
            break;
        }
        if (!found) return -1;
    }
    return std::min(700, 100 + score - static_cast<int>(candidate.size()) / 4);
}

int ColorTextEditBackend::byteToColumn(const std::string& line, int byteIndex, int tabSize) {
    int column = 0;
    const int limit = std::min(byteIndex, static_cast<int>(line.size()));
    for (int i = 0; i < limit;) {
        const unsigned char c = static_cast<unsigned char>(line[static_cast<size_t>(i)]);
        if (c == '\t') column = (column / tabSize) * tabSize + tabSize;
        else ++column;
        i += utf8Length(c);
    }
    return column;
}

int ColorTextEditBackend::columnToByte(const std::string& line, int column, int tabSize) {
    int current = 0;
    int i = 0;
    while (i < static_cast<int>(line.size())) {
        const unsigned char c = static_cast<unsigned char>(line[static_cast<size_t>(i)]);
        const int next = c == '\t' ? (current / tabSize) * tabSize + tabSize : current + 1;
        if (next > column) break;
        current = next;
        i += utf8Length(c);
    }
    return std::min(i, static_cast<int>(line.size()));
}

void ColorTextEditBackend::toggleLineComments(std::vector<std::string>& lines, int first, int last) {
    if (lines.empty()) return;
    first = std::clamp(first, 0, static_cast<int>(lines.size()) - 1);
    last = std::clamp(last, first, static_cast<int>(lines.size()) - 1);

    bool allCommented = true;
    bool anyContent = false;
    size_t indent = std::string::npos;
    for (int i = first; i <= last; ++i) {
        const std::string& line = lines[static_cast<size_t>(i)];
        const size_t content = line.find_first_not_of(" \t");
        if (content == std::string::npos) continue;
        anyContent = true;
        indent = std::min(indent, content);
        if (line.compare(content, 2, "--") != 0) allCommented = false;
    }
    if (!anyContent) return;

    for (int i = first; i <= last; ++i) {
        std::string& line = lines[static_cast<size_t>(i)];
        const size_t content = line.find_first_not_of(" \t");
        if (content == std::string::npos) continue;
        if (allCommented) {
            const size_t length = line.compare(content, 3, "-- ") == 0 ? 3 : 2;
            line.erase(content, length);
        } else {
            line.insert(indent, "-- ");
        }
    }
}

ColorTextEditBackend::CallContext ColorTextEditBackend::findCallContext(const std::string& lineText, int cursorByte) {
    CallContext context;
    cursorByte = std::clamp(cursorByte, 0, static_cast<int>(lineText.size()));
    if (positionInCommentOrString(lineText, cursorByte)) return context;

    std::vector<bool> masked(lineText.size(), false);
    char quote = 0;
    for (int i = 0; i < cursorByte; ++i) {
        const char c = lineText[static_cast<size_t>(i)];
        if (quote != 0) {
            masked[static_cast<size_t>(i)] = true;
            if (c == '\\' && i + 1 < cursorByte) masked[static_cast<size_t>(++i)] = true;
            else if (c == quote) quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
            masked[static_cast<size_t>(i)] = true;
        }
    }

    int depth = 0;
    int arguments = 0;
    int open = -1;
    for (int i = cursorByte - 1; i >= 0; --i) {
        if (masked[static_cast<size_t>(i)]) continue;
        const char c = lineText[static_cast<size_t>(i)];
        if (c == ')' || c == ']' || c == '}') ++depth;
        else if (c == '[' || c == '{') {
            if (depth == 0) return context;
            --depth;
        } else if (c == '(') {
            if (depth == 0) {
                open = i;
                break;
            }
            --depth;
        } else if (c == ',' && depth == 0) {
            ++arguments;
        }
    }
    if (open <= 0) return context;

    int end = open;
    while (end > 0 && lineText[static_cast<size_t>(end - 1)] == ' ') --end;
    int start = end;
    while (start > 0) {
        const char c = lineText[static_cast<size_t>(start - 1)];
        if (!isIdentifierChar(c) && c != '.' && c != ':') break;
        --start;
    }
    if (start == end) return context;
    const std::string callee = lineText.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
    if (!(std::isalpha(static_cast<unsigned char>(callee.front())) || callee.front() == '_')) return context;
    static const char* const kNotCalls[] = {"function", "if", "while", "until", "return", "and", "or", "not", "elseif", "in"};
    for (const char* keyword : kNotCalls) {
        if (callee == keyword) return context;
    }
    std::string before = trim(lineText.substr(0, static_cast<size_t>(start)));
    if (before.size() >= 8 && before.compare(before.size() - 8, 8, "function") == 0) return context;

    context.active = true;
    context.callee = callee;
    context.calleeEnd = end;
    context.argumentIndex = arguments;
    return context;
}

std::vector<ColorTextEditBackend::Symbol> ColorTextEditBackend::findSymbols(const std::vector<std::string>& lines) {
    static const std::regex kFunction(R"(^\s*(?:local\s+)?function\s+([A-Za-z_][\w\.:]*)\s*\()");
    static const std::regex kAssigned(R"(^\s*(?:local\s+)?([A-Za-z_][\w\.]*)\s*=\s*function\s*\()");
    static const std::regex kEvent(R"(^\s*events\.(\w+)\s*\(\s*function)");
    std::vector<Symbol> symbols;
    std::smatch match;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const std::string& line = lines[static_cast<size_t>(i)];
        if (line.find("function") == std::string::npos) continue;
        if (std::regex_search(line, match, kFunction) || std::regex_search(line, match, kAssigned)) {
            symbols.push_back({match[1].str(), i});
        } else if (std::regex_search(line, match, kEvent)) {
            symbols.push_back({"events." + match[1].str(), i});
        }
    }
    return symbols;
}

std::unordered_map<std::string, std::string> ColorTextEditBackend::parseApiDocs(const std::string& definitions) {
    static const std::regex kDeclareTable(R"(^declare\s+(\w+)\s*:\s*\{)");
    static const std::regex kDeclareFunction(R"(^declare\s+function\s+(\w+))");
    static const std::regex kMember(R"(^\s+(\w+)\s*:)");
    std::unordered_map<std::string, std::string> docs;
    std::istringstream in(definitions);
    std::string line;
    std::string block;
    std::string pending;
    std::smatch match;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.rfind("--- ", 0) == 0) {
            pending = trimmed.substr(4);
            continue;
        }
        if (std::regex_search(line, match, kDeclareTable)) {
            block = match[1].str();
            if (!pending.empty()) docs[block] = pending;
        } else if (std::regex_search(line, match, kDeclareFunction)) {
            if (!pending.empty()) docs[match[1].str()] = pending;
        } else if (!block.empty() && std::regex_search(line, match, kMember)) {
            if (!pending.empty()) docs[block + "." + match[1].str()] = pending;
        } else if (trimmed.rfind('}', 0) == 0 && line.rfind('}', 0) == 0) {
            block.clear();
        }
        if (!trimmed.empty() && trimmed.rfind("--", 0) != 0) pending.clear();
    }
    return docs;
}

std::vector<std::string> ColorTextEditBackend::splitSignatureParameters(const std::string& signature, std::string& returnPart) {
    std::vector<std::string> parameters;
    returnPart.clear();
    size_t open = std::string::npos;
    int angle = 0;
    for (size_t i = 0; i < signature.size(); ++i) {
        if (signature[i] == '<') ++angle;
        else if (signature[i] == '>' && angle > 0) --angle;
        else if (signature[i] == '(' && angle == 0) {
            open = i;
            break;
        }
    }
    if (open == std::string::npos) {
        returnPart = signature;
        return parameters;
    }
    int depth = 0;
    size_t segmentStart = open + 1;
    for (size_t i = open; i < signature.size(); ++i) {
        const char c = signature[i];
        if (c == '(' || c == '{' || c == '[' || c == '<') ++depth;
        else if (c == ')' || c == '}' || c == ']' || (c == '>' && i > 0 && signature[i - 1] != '-')) {
            --depth;
            if (depth == 0 && c == ')') {
                const std::string last = trim(signature.substr(segmentStart, i - segmentStart));
                if (!last.empty()) parameters.push_back(last);
                returnPart = trim(signature.substr(i + 1));
                return parameters;
            }
        } else if (c == ',' && depth == 1) {
            parameters.push_back(trim(signature.substr(segmentStart, i - segmentStart)));
            segmentStart = i + 1;
        }
    }
    return parameters;
}

std::string ColorTextEditBackend::applyEdits(const std::string& text, std::vector<TextEdit> edits, std::vector<Caret>& carets) {
    std::sort(edits.begin(), edits.end(), [](const TextEdit& a, const TextEdit& b) { return a.start < b.start; });
    std::vector<TextEdit> kept;
    for (TextEdit& edit : edits) {
        edit.start = std::clamp(edit.start, 0, static_cast<int>(text.size()));
        edit.end = std::clamp(edit.end, edit.start, static_cast<int>(text.size()));
        if (!kept.empty() && edit.start < kept.back().end) continue;
        if (!kept.empty() && edit.start == kept.back().start && edit.end == kept.back().end) continue;
        kept.push_back(std::move(edit));
    }

    std::string out;
    out.reserve(text.size());
    std::vector<int> newStarts;
    int cursor = 0;
    for (const TextEdit& edit : kept) {
        out.append(text, static_cast<size_t>(cursor), static_cast<size_t>(edit.start - cursor));
        newStarts.push_back(static_cast<int>(out.size()));
        out += edit.text;
        cursor = edit.end;
    }
    out.append(text, static_cast<size_t>(cursor), std::string::npos);

    auto map = [&](int offset) {
        int delta = 0;
        for (size_t i = 0; i < kept.size(); ++i) {
            const TextEdit& edit = kept[i];
            if (offset < edit.start) break;
            if (offset <= edit.end) return newStarts[i] + static_cast<int>(edit.text.size());
            delta += static_cast<int>(edit.text.size()) - (edit.end - edit.start);
        }
        return offset + delta;
    };
    for (Caret& caret : carets) {
        caret.anchor = map(caret.anchor);
        caret.position = map(caret.position);
    }
    return out;
}

} // namespace engine::studio::panels
