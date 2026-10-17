#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace engine::core {

// Finds require("name") / require(script.Parent.Name) style dependencies between scripts by name.
inline std::set<std::string> findRequires(const std::string& source) {
    std::set<std::string> out;
    std::size_t pos = 0;
    while ((pos = source.find("require", pos)) != std::string::npos) {
        pos += 7;
        while (pos < source.size() && (source[pos] == ' ' || source[pos] == '(')) ++pos;
        if (pos < source.size() && (source[pos] == '"' || source[pos] == '\'')) {
            const char quote = source[pos++];
            const std::size_t end = source.find(quote, pos);
            if (end == std::string::npos) break;
            out.insert(source.substr(pos, end - pos));
            pos = end;
        } else {
            std::size_t end = pos;
            while (end < source.size() && (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '_' ||
                                           source[end] == '.')) {
                ++end;
            }
            std::string path = source.substr(pos, end - pos);
            const std::size_t dot = path.rfind('.');
            if (dot != std::string::npos) path = path.substr(dot + 1);
            if (!path.empty() && path != "script" && path != "Parent") out.insert(path);
            pos = end;
        }
    }
    return out;
}

struct ModuleGraph {
    std::map<std::string, std::set<std::string>> edges; // script -> modules it requires
    std::vector<std::vector<std::string>> cycles;       // each cycle listed once, starting at its smallest name
};

inline ModuleGraph buildModuleGraph(const std::map<std::string, std::string>& sources) {
    ModuleGraph g;
    for (const auto& [name, src] : sources) g.edges[name] = findRequires(src);

    std::map<std::string, int> state; // 0 new, 1 in progress, 2 done
    std::vector<std::string> stack;
    std::set<std::vector<std::string>> seen;
    auto visit = [&](auto&& self, const std::string& n) -> void {
        state[n] = 1;
        stack.push_back(n);
        auto it = g.edges.find(n);
        if (it != g.edges.end()) {
            for (const std::string& next : it->second) {
                if (state[next] == 1) {
                    std::vector<std::string> cycle(std::find(stack.begin(), stack.end(), next), stack.end());
                    std::rotate(cycle.begin(), std::min_element(cycle.begin(), cycle.end()), cycle.end());
                    if (seen.insert(cycle).second) g.cycles.push_back(cycle);
                } else if (state[next] == 0) {
                    self(self, next);
                }
            }
        }
        stack.pop_back();
        state[n] = 2;
    };
    for (const auto& [name, deps] : g.edges) {
        (void)deps;
        if (state[name] == 0) visit(visit, name);
    }
    return g;
}

} // namespace engine::core
