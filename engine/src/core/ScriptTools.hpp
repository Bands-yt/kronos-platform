#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace engine::core::scripttools {

struct Use {
    int line = 1, column = 1; // 1-based
};

// Finds identifier uses in Luau or Python source, skipping strings and comments (405, 406).
inline std::vector<Use> findIdentifierUses(const std::string& src, const std::string& name) {
    std::vector<Use> out;
    int line = 1, col = 1;
    std::size_t i = 0;
    auto adv = [&](std::size_t n = 1) {
        for (std::size_t k = 0; k < n && i < src.size(); ++k, ++i) {
            if (src[i] == '\n') {
                ++line;
                col = 1;
            } else ++col;
        }
    };
    auto isId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    while (i < src.size()) {
        const char c = src[i];
        if (c == '-' && src.compare(i, 2, "--") == 0) { // Luau comment
            if (src.compare(i, 4, "--[[") == 0) {
                const std::size_t end = src.find("]]", i + 4);
                adv(end == std::string::npos ? src.size() - i : end + 2 - i);
            } else {
                while (i < src.size() && src[i] != '\n') adv();
            }
        } else if (c == '#') { // Python comment
            while (i < src.size() && src[i] != '\n') adv();
        } else if (c == '"' || c == '\'') {
            const char q = c;
            adv();
            while (i < src.size() && src[i] != q && src[i] != '\n') adv(src[i] == '\\' ? 2 : 1);
            adv();
        } else if (isId(c) && !std::isdigit(static_cast<unsigned char>(c))) {
            const std::size_t start = i;
            const int sl = line, sc = col;
            while (i < src.size() && isId(src[i])) adv();
            const bool member = start > 0 && (src[start - 1] == '.' || src[start - 1] == ':');
            if (src.compare(start, i - start, name) == 0 && i - start == name.size() && !member) out.push_back({sl, sc});
        } else {
            adv();
        }
    }
    return out;
}

// Renames every use of `from` to `to` (425). Refuses when `to` is not a valid name or already used.
inline bool renameIdentifier(std::string& src, const std::string& from, const std::string& to) {
    if (to.empty() || std::isdigit(static_cast<unsigned char>(to[0]))) return false;
    for (char c : to) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    if (!findIdentifierUses(src, to).empty()) return false;
    const auto uses = findIdentifierUses(src, from);
    if (uses.empty()) return false;
    std::vector<std::size_t> offsets;
    int line = 1, col = 1;
    std::size_t u = 0;
    for (std::size_t i = 0; i < src.size() && u < uses.size(); ++i) {
        if (line == uses[u].line && col == uses[u].column) {
            offsets.push_back(i);
            ++u;
        }
        if (src[i] == '\n') {
            ++line;
            col = 1;
        } else ++col;
    }
    for (auto it = offsets.rbegin(); it != offsets.rend(); ++it) src.replace(*it, from.size(), to);
    return true;
}

// Format on save (410): trailing spaces off, tabs to 4 spaces at the start of lines, exactly one newline at the end.
inline std::string formatOnSave(const std::string& src) {
    std::string out, line;
    auto flush = [&]() {
        std::size_t tabs = 0;
        while (tabs < line.size() && line[tabs] == '\t') ++tabs;
        line = std::string(tabs * 4, ' ') + line.substr(tabs);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t' || line.back() == '\r')) line.pop_back();
        out += line + "\n";
        line.clear();
    };
    for (char c : src) {
        if (c == '\n') flush();
        else line += c;
    }
    if (!line.empty()) flush();
    while (out.size() > 1 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    return out == "\n" ? std::string{} : out;
}

// Lint rules that can be switched on and off (411).
struct LintConfig {
    bool trailingWhitespace = true;
    bool tabsInIndent = true;
    int maxLineLength = 120; // 0 = off
    bool todoComments = false;
};

struct LintIssue {
    int line = 0;
    std::string rule, message;
};

inline std::vector<LintIssue> lint(const std::string& src, const LintConfig& cfg) {
    std::vector<LintIssue> out;
    int line = 0;
    std::size_t pos = 0;
    while (pos <= src.size()) {
        std::size_t end = src.find('\n', pos);
        if (end == std::string::npos) end = src.size();
        const std::string l = src.substr(pos, end - pos);
        ++line;
        if (cfg.trailingWhitespace && !l.empty() && (l.back() == ' ' || l.back() == '\t')) out.push_back({line, "trailing-whitespace", "Trailing spaces."});
        if (cfg.tabsInIndent && !l.empty() && l[0] == '\t') out.push_back({line, "tabs", "Tab used for indentation."});
        if (cfg.maxLineLength > 0 && static_cast<int>(l.size()) > cfg.maxLineLength) out.push_back({line, "line-length", "Line is longer than " + std::to_string(cfg.maxLineLength) + "."});
        if (cfg.todoComments && l.find("TODO") != std::string::npos) out.push_back({line, "todo", "TODO left in code."});
        if (end == src.size()) break;
        pos = end + 1;
    }
    return out;
}

// Filter for the script tree (409).
inline std::vector<std::string> filterNames(const std::vector<std::string>& names, const std::string& text) {
    std::string t = text;
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return std::tolower(c); });
    std::vector<std::string> out;
    for (const auto& n : names) {
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return std::tolower(c); });
        if (l.find(t) != std::string::npos) out.push_back(n);
    }
    return out;
}

// Publish filter (565): which project files go into a published package.
inline bool includeInPublish(const std::string& path) {
    static const char* skip[] = {".blend", ".psd", ".kra", ".xcf", ".bak", ".tmp", ".log", ".kronos-cache", ".git/", "imgui.ini", "crash_report_", ".swp"};
    for (const char* s : skip) {
        if (path.find(s) != std::string::npos) return false;
    }
    return true;
}

} // namespace engine::core::scripttools
