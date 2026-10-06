#include "studio/panels/LuauSymbolIndex.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

#include "Luau/Ast.h"
#include "Luau/Parser.h"

namespace engine::studio::luau_symbols {

namespace {

struct Key {
    const Luau::AstLocal* local = nullptr;
    std::string global;
    std::string member;

    bool operator==(const Key&) const = default;
};

struct Occurrence {
    Key key;
    Range range;
    bool definition = false;
};

Range toRange(const Luau::Location& location) {
    const int endColumn = location.end.line == location.begin.line ? static_cast<int>(location.end.column)
                                                                   : static_cast<int>(location.begin.column);
    return {static_cast<int>(location.begin.line), static_cast<int>(location.begin.column), endColumn};
}

std::optional<Key> baseKey(Luau::AstExpr* expr) {
    if (auto* local = expr->as<Luau::AstExprLocal>()) return Key{local->local, {}, {}};
    if (auto* global = expr->as<Luau::AstExprGlobal>()) return Key{nullptr, global->name.value, {}};
    return std::nullopt;
}

class Collector final : public Luau::AstVisitor {
public:
    std::vector<Occurrence> occurrences;
    std::set<std::pair<unsigned, unsigned>> definitionStarts;

    void declare(const Luau::AstLocal* local) {
        if (local == nullptr || local->name.value == nullptr) return;
        occurrences.push_back({Key{local, {}, {}}, toRange(local->location), true});
    }

    void markDefinition(Luau::AstExpr* target) {
        if (auto* index = target->as<Luau::AstExprIndexName>()) {
            definitionStarts.insert({index->indexLocation.begin.line, index->indexLocation.begin.column});
        } else {
            definitionStarts.insert({target->location.begin.line, target->location.begin.column});
        }
    }

    bool visit(Luau::AstStatLocal* node) override {
        for (size_t i = 0; i < node->vars.size; ++i) {
            declare(node->vars.data[i]);
            if (i < node->values.size) recordTableFields(node->vars.data[i], node->values.data[i]);
        }
        return true;
    }
    bool visit(Luau::AstStatLocalFunction* node) override {
        declare(node->name);
        return true;
    }
    bool visit(Luau::AstExprFunction* node) override {
        for (Luau::AstLocal* arg : node->args) declare(arg);
        return true;
    }
    bool visit(Luau::AstStatFor* node) override {
        declare(node->var);
        return true;
    }
    bool visit(Luau::AstStatForIn* node) override {
        for (Luau::AstLocal* var : node->vars) declare(var);
        return true;
    }
    bool visit(Luau::AstStatFunction* node) override {
        markDefinition(node->name);
        return true;
    }
    bool visit(Luau::AstStatAssign* node) override {
        for (size_t i = 0; i < node->vars.size; ++i) {
            markDefinition(node->vars.data[i]);
            if (auto* global = node->vars.data[i]->as<Luau::AstExprGlobal>(); global != nullptr && i < node->values.size) {
                recordTableFields(Key{nullptr, global->name.value, {}}, node->values.data[i]);
            }
        }
        return true;
    }
    bool visit(Luau::AstExprLocal* node) override {
        occurrences.push_back({Key{node->local, {}, {}}, toRange(node->location), false});
        return true;
    }
    bool visit(Luau::AstExprGlobal* node) override {
        occurrences.push_back({Key{nullptr, node->name.value, {}}, toRange(node->location), false});
        return true;
    }
    bool visit(Luau::AstExprIndexName* node) override {
        if (std::optional<Key> base = baseKey(node->expr)) {
            base->member = node->index.value;
            occurrences.push_back({*base, toRange(node->indexLocation), false});
        }
        return true;
    }

private:
    void recordTableFields(const Luau::AstLocal* local, Luau::AstExpr* value) { recordTableFields(Key{local, {}, {}}, value); }

    void recordTableFields(Key base, Luau::AstExpr* value) {
        auto* table = value->as<Luau::AstExprTable>();
        if (table == nullptr) return;
        for (const Luau::AstExprTable::Item& item : table->items) {
            if (item.kind != Luau::AstExprTable::Item::Kind::Record || item.key == nullptr) continue;
            auto* key = item.key->as<Luau::AstExprConstantString>();
            if (key == nullptr) continue;
            Key member = base;
            member.member = std::string(key->value.data, key->value.size);
            occurrences.push_back({member, toRange(key->location), true});
        }
    }
};

std::vector<Occurrence> collect(const std::string& source) {
    Luau::Allocator allocator;
    Luau::AstNameTable names(allocator);
    Luau::ParseOptions options;
    Luau::ParseResult result = Luau::Parser::parse(source.data(), source.size(), names, allocator, options);
    Collector collector;
    if (result.root != nullptr) result.root->visit(&collector);
    for (Occurrence& occurrence : collector.occurrences) {
        if (collector.definitionStarts.count(
                {static_cast<unsigned>(occurrence.range.line), static_cast<unsigned>(occurrence.range.column)}) != 0) {
            occurrence.definition = true;
        }
    }
    std::stable_sort(collector.occurrences.begin(), collector.occurrences.end(), [](const Occurrence& a, const Occurrence& b) {
        return std::tie(a.range.line, a.range.column) < std::tie(b.range.line, b.range.column);
    });
    return std::move(collector.occurrences);
}

SymbolInfo gather(const std::vector<Occurrence>& all, const Key& key, const std::string& name) {
    SymbolInfo info;
    info.name = name;
    for (const Occurrence& occurrence : all) {
        if (!(occurrence.key == key)) continue;
        if (!info.occurrences.empty() && info.occurrences.back() == occurrence.range) continue;
        info.occurrences.push_back(occurrence.range);
        if (occurrence.definition && !info.definition) info.definition = occurrence.range;
    }
    return info;
}

Key keyFor(const GlobalRef& ref) { return Key{nullptr, ref.global, ref.member}; }

} // namespace

std::optional<SymbolInfo> symbolAt(const std::string& source, int line, int byteColumn) {
    const std::vector<Occurrence> all = collect(source);
    for (const Occurrence& occurrence : all) {
        if (occurrence.range.line != line || byteColumn < occurrence.range.column || byteColumn > occurrence.range.endColumn) {
            continue;
        }
        const Key& key = occurrence.key;
        std::string name = !key.member.empty() ? key.member
                         : key.local != nullptr ? std::string(key.local->name.value)
                                                : key.global;
        SymbolInfo info = gather(all, key, name);
        if (key.local == nullptr) info.globalRef = GlobalRef{key.global, key.member};
        return info;
    }
    return std::nullopt;
}

std::vector<Range> occurrencesOf(const std::string& source, const GlobalRef& ref) {
    return gather(collect(source), keyFor(ref), ref.member.empty() ? ref.global : ref.member).occurrences;
}

std::optional<Range> definitionOf(const std::string& source, const GlobalRef& ref) {
    return gather(collect(source), keyFor(ref), ref.member.empty() ? ref.global : ref.member).definition;
}

std::string replaceRanges(const std::string& source, std::vector<Range> ranges, const std::string& replacement) {
    std::vector<size_t> lineStarts{0};
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '\n') lineStarts.push_back(i + 1);
    }
    std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) {
        return std::tie(a.line, a.column) > std::tie(b.line, b.column);
    });
    std::string out = source;
    for (const Range& range : ranges) {
        if (range.line < 0 || range.line >= static_cast<int>(lineStarts.size())) continue;
        const size_t start = lineStarts[static_cast<size_t>(range.line)] + static_cast<size_t>(range.column);
        const size_t end = lineStarts[static_cast<size_t>(range.line)] + static_cast<size_t>(range.endColumn);
        if (end > out.size() || start > end) continue;
        out.replace(start, end - start, replacement);
    }
    return out;
}

bool isValidIdentifier(const std::string& name) {
    static const std::set<std::string> kKeywords = {"and",  "break", "do",   "else",  "elseif",   "end",   "false",
                                                    "for",  "function", "if", "in",   "local",    "nil",   "not",
                                                    "or",   "repeat", "return", "then", "true", "until", "while", "continue"};
    if (name.empty() || std::isdigit(static_cast<unsigned char>(name[0]))) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return kKeywords.count(name) == 0;
}

std::vector<TextHit> findText(const std::string& source, const std::string& query, bool matchCase, bool wholeWord) {
    std::vector<TextHit> hits;
    if (query.empty()) return hits;
    auto fold = [matchCase](std::string text) {
        if (!matchCase) {
            for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    };
    const std::string needle = fold(query);
    auto isWordChar = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    int line = 0;
    size_t lineStart = 0;
    while (lineStart <= source.size()) {
        size_t lineEnd = source.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = source.size();
        const std::string text = source.substr(lineStart, lineEnd - lineStart);
        const std::string haystack = fold(text);
        for (size_t at = haystack.find(needle); at != std::string::npos; at = haystack.find(needle, at + 1)) {
            const size_t end = at + needle.size();
            if (wholeWord && ((at > 0 && isWordChar(text[at - 1])) || (end < text.size() && isWordChar(text[end])))) continue;
            hits.push_back({line, static_cast<int>(at), static_cast<int>(end)});
        }
        if (lineEnd == source.size()) break;
        lineStart = lineEnd + 1;
        ++line;
    }
    return hits;
}

} // namespace engine::studio::luau_symbols
