#include "core/RobloxApiDefinitions.hpp"

#include <cctype>
#include <set>
#include <vector>

#include "core/InstanceTree.hpp"

namespace engine::core {

namespace {

const char* luauTypeFor(PropertyType type) {
    switch (type) {
        case PropertyType::Bool: return "boolean";
        case PropertyType::Number: return "number";
        case PropertyType::String: return "string";
        default: return "any";
    }
}

bool isIdentifier(const std::string& name) {
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    static const std::set<std::string> reserved = {"and", "break", "do", "else", "elseif", "end", "false", "for", "function",
                                                   "if", "in", "local", "nil", "not", "or", "repeat", "return", "then",
                                                   "true", "until", "while", "continue", "export", "type"};
    return reserved.count(name) == 0;
}

} // namespace

std::string robloxLuauDefinitions() {
    std::set<std::string> known;
    for (const ClassDef& cls : instances::allClasses()) known.insert(cls.name);

    // A parent must be declared before its children.
    std::string out;
    std::set<std::string> done;
    std::vector<const ClassDef*> pending;
    for (const ClassDef& cls : instances::allClasses()) pending.push_back(&cls);
    while (!pending.empty()) {
        bool progressed = false;
        for (auto it = pending.begin(); it != pending.end();) {
            const ClassDef& cls = **it;
            const bool parentReady = cls.superclass.empty() || known.count(cls.superclass) == 0 || done.count(cls.superclass) > 0;
            if (!isIdentifier(cls.name)) {
                it = pending.erase(it);
                continue;
            }
            if (!parentReady) {
                ++it;
                continue;
            }
            out += "declare class " + cls.name;
            if (!cls.superclass.empty() && known.count(cls.superclass) > 0) out += " extends " + cls.superclass;
            out += "\n";
            std::set<std::string> written;
            auto write = [&](const std::string& member, const std::string& type) {
                if (!isIdentifier(member) || !written.insert(member).second) return;
                out += "    " + member + ": " + type + "\n";
            };
            for (const PropertyDef& property : cls.properties) write(property.name, luauTypeFor(property.type));
            for (const std::string& method : cls.methods) write(method, "(self: " + cls.name + ", ...any) -> ...any");
            for (const std::string& event : cls.events) write(event, "any");
            out += "    [string]: any\nend\n";
            done.insert(cls.name);
            it = pending.erase(it);
            progressed = true;
        }
        if (!progressed) break; // a cycle can't happen with a real table; stop rather than loop
    }
    return out;
}

} // namespace engine::core
