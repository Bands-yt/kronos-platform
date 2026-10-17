#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Logic for plugin catalogue, AI helper safety and community features. No network, no accounts.
namespace engine::core::community {

inline std::string lowerCase(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// ---- Plugin catalogue (704, 726 to 750)
struct CatalogEntry {
    std::string id, title, task;
    std::set<std::string> tags;
    int minHostMajor = 1, maxHostMajor = 99;
    std::string author;
    bool verified = false, deprecated = false;
    long long daysSinceUpdate = 0;
    std::vector<std::string> depends;
};

inline std::vector<std::string> searchCatalog(const std::vector<CatalogEntry>& all, const std::string& text, const std::string& task, int hostMajor) {
    std::vector<std::string> out;
    const std::string q = lowerCase(text);
    for (const auto& e : all) {
        if (hostMajor < e.minHostMajor || hostMajor > e.maxHostMajor) continue;
        if (!task.empty() && e.task != task) continue;
        if (!q.empty() && lowerCase(e.title).find(q) == std::string::npos && !e.tags.count(q)) continue;
        out.push_back(e.id);
    }
    return out;
}

inline std::vector<std::string> featuredPlugins(const std::vector<CatalogEntry>& all, std::size_t n) {
    std::vector<const CatalogEntry*> ok;
    for (const auto& e : all) {
        if (e.verified && !e.deprecated) ok.push_back(&e);
    }
    std::sort(ok.begin(), ok.end(), [](const CatalogEntry* a, const CatalogEntry* b) { return a->daysSinceUpdate < b->daysSinceUpdate; });
    std::vector<std::string> out;
    for (std::size_t i = 0; i < ok.size() && i < n; ++i) out.push_back(ok[i]->id);
    return out;
}

// Reviews (729): one per user, short ones from brand new accounts are held for a person to look at.
class Reviews {
public:
    enum class Result { Accepted, Duplicate, Held };
    Result add(const std::string& user, const std::string& plugin, int stars, const std::string& text, long long accountAgeDays) {
        if (!seen_.insert(user + "|" + plugin).second) return Result::Duplicate;
        const bool suspicious = (accountAgeDays < 2 && text.size() < 10) || stars < 1 || stars > 5;
        (suspicious ? held_ : accepted_).push_back({plugin, stars});
        return suspicious ? Result::Held : Result::Accepted;
    }
    [[nodiscard]] double average(const std::string& plugin) const {
        int n = 0, sum = 0;
        for (const auto& r : accepted_) {
            if (r.first == plugin) { sum += r.second; ++n; }
        }
        return n ? static_cast<double>(sum) / n : 0.0;
    }
    [[nodiscard]] std::size_t held() const { return held_.size(); }

private:
    std::set<std::string> seen_;
    std::vector<std::pair<std::string, int>> accepted_, held_;
};

// Verified authors (730).
struct AuthorRecord {
    int publishedPlugins = 0;
    long long accountAgeDays = 0;
    bool twoFactor = false;
    int upheldReports = 0;
};

inline std::vector<std::string> verificationGaps(const AuthorRecord& a) {
    std::vector<std::string> out;
    if (a.publishedPlugins < 2) out.push_back("needs at least 2 published plugins");
    if (a.accountAgeDays < 90) out.push_back("account must be 90 days old");
    if (!a.twoFactor) out.push_back("turn on two-factor sign-in");
    if (a.upheldReports > 0) out.push_back("has upheld reports");
    return out;
}

// Pinned versions (731): a project stays on its pin until the owner changes it.
inline std::string resolveVersion(const std::map<std::string, std::string>& pins, const std::string& plugin, const std::string& latest) { return pins.count(plugin) ? pins.at(plugin) : latest; }

// Dependency warnings (732).
inline std::vector<std::string> dependencyWarnings(const std::vector<CatalogEntry>& catalog, const std::set<std::string>& installed, const std::string& toInstall, int hostMajor) {
    std::vector<std::string> out;
    for (const auto& e : catalog) {
        if (e.id != toInstall) continue;
        if (hostMajor < e.minHostMajor || hostMajor > e.maxHostMajor) out.push_back(e.id + " does not support this version of Kronos");
        for (const auto& d : e.depends) {
            if (!installed.count(d)) out.push_back(e.id + " needs " + d);
        }
        if (e.deprecated) out.push_back(e.id + " is no longer maintained");
    }
    return out;
}

// Private registry (733): a team sees public plugins and its own.
inline std::vector<std::string> visibleTo(const std::map<std::string, std::string>& pluginToTeam, const std::string& team) { // team empty = public
    std::vector<std::string> out;
    for (const auto& [p, t] : pluginToTeam) {
        if (t.empty() || t == team) out.push_back(p);
    }
    return out;
}

// Install plan (734, 735).
struct PlanStep {
    std::string action, plugin;
};

inline std::vector<PlanStep> installPlan(const std::vector<CatalogEntry>& catalog, std::set<std::string> installed, const std::string& wanted) {
    std::vector<PlanStep> out;
    std::function<void(const std::string&, int)> add = [&](const std::string& id, int depth) {
        if (installed.count(id) || depth > 16) return;
        for (const auto& e : catalog) {
            if (e.id != id) continue;
            for (const auto& d : e.depends) add(d, depth + 1);
        }
        installed.insert(id);
        out.push_back({"install", id});
    };
    add(wanted, 0);
    return out;
}

// Reports (736): a simple state machine.
class AbuseReport {
public:
    enum class State { Open, Reviewing, ActionTaken, Dismissed };
    bool advance(State next) {
        const bool ok = (state_ == State::Open && next == State::Reviewing) || (state_ == State::Reviewing && (next == State::ActionTaken || next == State::Dismissed));
        if (ok) state_ = next;
        return ok;
    }
    [[nodiscard]] State state() const { return state_; }

private:
    State state_ = State::Open;
};

// Package scanning (741): a manifest asking for much more than its description needs gets flagged.
inline std::vector<std::string> scanManifest(const std::set<std::string>& permissions, const std::vector<std::string>& importedNames) {
    static const std::set<std::string> dangerousPermissions = {"filesystem.all", "network.any", "process.spawn"};
    static const std::set<std::string> suspiciousImports = {"system", "popen", "dlopen", "CreateProcess", "LoadLibrary"};
    std::vector<std::string> out;
    for (const auto& p : permissions) {
        if (dangerousPermissions.count(p)) out.push_back("asks for " + p);
    }
    for (const auto& i : importedNames) {
        if (suspiciousImports.count(i)) out.push_back("uses " + i);
    }
    return out;
}

// Deprecation (745).
inline bool looksAbandoned(const CatalogEntry& e, long long days) { return e.daysSinceUpdate > days && !e.deprecated; }

// Plugin sets (750).
inline std::string exportPluginSet(const std::map<std::string, std::string>& idToVersion) {
    std::string s;
    for (const auto& kv : idToVersion) s += kv.first + "@" + kv.second + "\n";
    return s;
}

inline std::map<std::string, std::string> importPluginSet(const std::string& text) {
    std::map<std::string, std::string> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t at = line.find('@');
        if (at != std::string::npos && at > 0) out[line.substr(0, at)] = line.substr(at + 1);
    }
    return out;
}

// Changelog (743): "## 1.2.0" headings to version list.
inline std::vector<std::string> changelogVersions(const std::string& markdown) {
    std::vector<std::string> out;
    std::istringstream in(markdown);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("## ", 0) == 0) out.push_back(line.substr(3));
    }
    return out;
}

// Licence line (749).
inline std::string licenceBadge(const std::string& spdx) {
    if (spdx.empty()) return "No licence given: ask the author before using it";
    if (spdx == "Apache-2.0" || spdx == "MIT" || spdx == "BSD-3-Clause") return spdx + ": free to use, keep the notice";
    return spdx + ": read the licence";
}

// ---- AI helper safety (776 to 800)
struct ProviderRequest {
    std::string prompt;
    int maxTokens = 512;
};

class AiProvider {
public:
    virtual ~AiProvider() = default;
    virtual bool available() = 0;
    virtual std::string complete(const ProviderRequest& r) = 0;
};

class OfflineProvider : public AiProvider {
public:
    bool available() override { return false; }
    std::string complete(const ProviderRequest&) override { return {}; }
};

// Always works (796): when the provider is missing, a fixed hint is used instead of an error.
inline std::string completeOrFallback(AiProvider& p, const ProviderRequest& r, const std::string& fallback) {
    if (!p.available()) return fallback;
    const std::string s = p.complete(r);
    return s.empty() ? fallback : s;
}

// Context budget (797): pieces with a priority are kept until the budget is used up.
struct ContextPiece {
    std::string name, text;
    int priority;
};

inline int estimateTokens(const std::string& text) { return static_cast<int>((text.size() + 3) / 4); }

inline std::vector<std::string> fitContext(std::vector<ContextPiece> pieces, int budgetTokens) {
    std::stable_sort(pieces.begin(), pieces.end(), [](const ContextPiece& a, const ContextPiece& b) { return a.priority > b.priority; });
    std::vector<std::string> kept;
    int used = 0;
    for (const auto& p : pieces) {
        const int t = estimateTokens(p.text);
        if (used + t <= budgetTokens) {
            used += t;
            kept.push_back(p.name);
        }
    }
    return kept;
}

// Cost meter (791).
inline double costUsd(long long inputTokens, long long outputTokens, double inputPerMillion, double outputPerMillion) { return static_cast<double>(inputTokens) / 1e6 * inputPerMillion + static_cast<double>(outputTokens) / 1e6 * outputPerMillion; }

// Imported text that talks to the assistant instead of the user (798).
inline std::vector<std::string> injectionHints(const std::string& text) {
    static const std::vector<std::string> phrases = {"ignore previous instructions", "ignore all previous", "disregard the above", "you are now", "reveal your system prompt", "send the contents of"};
    std::vector<std::string> out;
    const std::string low = lowerCase(text);
    for (const auto& p : phrases) {
        if (low.find(p) != std::string::npos) out.push_back(p);
    }
    return out;
}

// Opt-in and opt-out (793, 800): everything is off until the user turns it on.
struct AiSettings {
    bool enabled = false, telemetry = false, allowCloud = false;
};

inline bool aiMayRun(const AiSettings& s, bool needsCloud) { return s.enabled && (!needsCloud || s.allowCloud); }

// Provenance (794).
inline std::string provenanceTag(const std::string& tool, const std::string& model, const std::string& date) { return "generated-by=" + tool + ";model=" + model + ";date=" + date; }

inline bool isGenerated(const std::string& metadata) { return metadata.find("generated-by=") != std::string::npos; }

// Rights warnings (795).
inline std::vector<std::string> rightsWarnings(const std::string& licence, bool generated, bool trainedOnUnknownData) {
    std::vector<std::string> out;
    if (generated) out.push_back("Made by an AI tool: check the tool's terms before selling it");
    if (trainedOnUnknownData) out.push_back("The training data is unknown");
    if (licence.empty()) out.push_back("No licence recorded");
    return out;
}

// Never overwrite silently (788): applying a change needs the exact text it was made against, and keeps a backup.
struct Proposal {
    std::string basedOn, replacement;
};

struct ApplyResult {
    bool applied = false;
    std::string newText, backup, reason;
};

inline ApplyResult applyProposal(const std::string& current, const Proposal& p, bool userAccepted) {
    ApplyResult r;
    if (!userAccepted) { r.reason = "not accepted"; r.newText = current; return r; }
    if (p.basedOn != current) { r.reason = "the file changed since the suggestion was made"; r.newText = current; return r; }
    r.applied = true;
    r.backup = current;
    r.newText = p.replacement;
    return r;
}

// Diff view (787): changed lines as +/- text.
inline std::string simpleDiff(const std::string& before, const std::string& after) {
    auto split = [](const std::string& s) {
        std::vector<std::string> v;
        std::istringstream in(s);
        std::string l;
        while (std::getline(in, l)) v.push_back(l);
        return v;
    };
    const auto a = split(before), b = split(after);
    std::string out;
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const bool hasA = i < a.size(), hasB = i < b.size();
        if (hasA && hasB && a[i] == b[i]) out += "  " + a[i] + "\n";
        else {
            if (hasA) out += "- " + a[i] + "\n";
            if (hasB) out += "+ " + b[i] + "\n";
        }
    }
    return out;
}

// Tests before insertion (780) and benchmark (799): fraction of fixtures where the result passed.
inline double successRate(const std::vector<std::pair<std::string, bool>>& results) {
    if (results.empty()) return 0;
    int ok = 0;
    for (const auto& r : results) ok += r.second;
    return static_cast<double>(ok) / static_cast<double>(results.size());
}

// Natural language scene search (785): words in the question matched against object names and tags.
inline std::vector<int> sceneSearch(const std::vector<std::pair<std::string, std::set<std::string>>>& objects, const std::string& question) {
    std::set<std::string> words;
    std::istringstream in(lowerCase(question));
    std::string w;
    while (in >> w) {
        if (w.size() > 2 && w != "the" && w != "all" && w != "show" && w != "find") words.insert(w);
    }
    std::vector<int> out;
    for (std::size_t i = 0; i < objects.size(); ++i) {
        const std::string name = lowerCase(objects[i].first);
        for (const auto& x : words) {
            if (name.find(x) != std::string::npos || objects[i].second.count(x)) {
                out.push_back(static_cast<int>(i));
                break;
            }
        }
    }
    return out;
}

// Level seeds (782): the same seed always gives the same suggestion list.
inline std::vector<uint32_t> levelSeeds(uint32_t base, int count) {
    std::vector<uint32_t> out;
    uint32_t x = base ? base : 1;
    for (int i = 0; i < count; ++i) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        out.push_back(x % 100000);
    }
    return out;
}

// Asset tags from names (784): local, no network.
inline std::set<std::string> tagsFromName(const std::string& fileName) {
    static const std::map<std::string, std::string> words = {{"tree", "nature"}, {"rock", "nature"}, {"grass", "nature"}, {"wall", "building"}, {"door", "building"}, {"car", "vehicle"}, {"sword", "weapon"}, {"gun", "weapon"}};
    std::set<std::string> out;
    const std::string low = lowerCase(fileName);
    for (const auto& [w, tag] : words) {
        if (low.find(w) != std::string::npos) out.insert(tag);
    }
    return out;
}

// ---- Community (951 to 975)
inline bool validSlug(const std::string& s) {
    if (s.size() < 3 || s.size() > 32 || s.front() == '-' || s.back() == '-') return false;
    return std::all_of(s.begin(), s.end(), [](char c) { return std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-'; });
}

struct GamePage {
    std::string name, creator;
    std::set<std::string> tags;
    long long updatedDay = 0;
    double rating = 0;
    int plays = 0;
};

inline std::vector<std::string> searchGames(const std::vector<GamePage>& games, const std::string& text, const std::set<std::string>& tags) {
    std::vector<std::string> out;
    const std::string q = lowerCase(text);
    for (const auto& g : games) {
        if (!q.empty() && lowerCase(g.name).find(q) == std::string::npos) continue;
        if (!std::all_of(tags.begin(), tags.end(), [&](const std::string& t) { return g.tags.count(t) > 0; })) continue;
        out.push_back(g.name);
    }
    return out;
}

// Featured collection rule with its explanation shown to everyone (954).
struct CollectionRule {
    std::string name;
    double minRating;
    int minPlays;
    long long maxAgeDays;
};

inline std::vector<std::string> collectionGames(const CollectionRule& r, const std::vector<GamePage>& games, long long today) {
    std::vector<std::string> out;
    for (const auto& g : games) {
        if (g.rating >= r.minRating && g.plays >= r.minPlays && today - g.updatedDay <= r.maxAgeDays) out.push_back(g.name);
    }
    return out;
}

inline std::string explainRule(const CollectionRule& r) { return r.name + ": rating at least " + std::to_string(static_cast<int>(r.minRating)) + ", at least " + std::to_string(r.minPlays) + " plays, updated in the last " + std::to_string(r.maxAgeDays) + " days. No one is picked by hand."; }

inline std::vector<std::string> recentlyUpdated(std::vector<GamePage> games, std::size_t n) {
    std::sort(games.begin(), games.end(), [](const GamePage& a, const GamePage& b) { return a.updatedDay > b.updatedDay; });
    std::vector<std::string> out;
    for (std::size_t i = 0; i < games.size() && i < n; ++i) out.push_back(games[i].name);
    return out;
}

// Safe demo link (957): only links to Kronos launch URLs are accepted.
inline bool safeLaunchLink(const std::string& url) { return url.rfind("kronos://launch?game=", 0) == 0 && url.size() < 200 && url.find_first_of(" \"'<>") == std::string::npos; }

// Survey widget (959).
inline double averageAnswer(const std::vector<int>& answers) {
    if (answers.empty()) return 0;
    double s = 0;
    for (int a : answers) s += std::clamp(a, 1, 5);
    return s / static_cast<double>(answers.size());
}

// Followers (961): who gets told about an update.
inline std::vector<std::string> notifyFollowers(const std::map<std::string, std::set<std::string>>& followersOf, const std::string& creator, const std::set<std::string>& mutedUsers) {
    std::vector<std::string> out;
    auto it = followersOf.find(creator);
    if (it == followersOf.end()) return out;
    for (const auto& u : it->second) {
        if (!mutedUsers.count(u)) out.push_back(u);
    }
    return out;
}

// Showcase submissions (962).
class Submission {
public:
    enum class State { Submitted, InReview, Approved, Rejected };
    bool advance(State next) {
        const bool ok = (state_ == State::Submitted && next == State::InReview) || (state_ == State::InReview && (next == State::Approved || next == State::Rejected));
        if (ok) state_ = next;
        return ok;
    }
    [[nodiscard]] State state() const { return state_; }

private:
    State state_ = State::Submitted;
};

inline std::vector<std::string> videoMetadataProblems(const std::map<std::string, std::string>& meta) {
    std::vector<std::string> out;
    for (const char* k : {"title", "duration", "thumbnail"}) {
        if (!meta.count(k) || meta.at(k).empty()) out.push_back(std::string("missing ") + k);
    }
    return out;
}

// Official links (964).
inline bool officialDownloadLink(const std::string& url, const std::vector<std::string>& allowedPrefixes) {
    return std::any_of(allowedPrefixes.begin(), allowedPrefixes.end(), [&](const std::string& p) { return url.rfind(p, 0) == 0; });
}

// Impersonation (966): two names that differ by one or two characters.
inline int editDistance(const std::string& a, const std::string& b) {
    std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) prev[j] = static_cast<int>(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        cur[0] = static_cast<int>(i);
        for (std::size_t j = 1; j <= b.size(); ++j) cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

inline std::vector<std::string> lookalikes(const std::string& name, const std::vector<std::string>& existing) {
    std::vector<std::string> out;
    const std::string n = lowerCase(name);
    for (const auto& e : existing) {
        const int d = editDistance(n, lowerCase(e));
        if (d > 0 && d <= 2 && e.size() >= 5) out.push_back(e);
    }
    return out;
}

// Forum categories (967) and starter kits (968).
inline std::vector<std::string> forumCategories() { return {"Announcements", "Help", "Showcase", "Studio and tools", "Scripting", "Plugins", "Roblox migration", "Off topic"}; }

inline std::string challengeKit(const std::string& theme, const std::string& month) { return "# " + month + " challenge: " + theme + "\n\nStart from templates/project. Make a small game about \"" + theme + "\" that plays for two minutes. Share it in Showcase.\n"; }

// Contributor issues (970).
struct Issue {
    std::string title;
    std::set<std::string> labels;
    bool open = true;
};

inline std::vector<std::string> goodFirstIssues(const std::vector<Issue>& issues) {
    std::vector<std::string> out;
    for (const auto& i : issues) {
        if (i.open && i.labels.count("good first issue")) out.push_back(i.title);
    }
    return out;
}

// Announcements (971).
inline std::string announcement(const std::string& version, const std::vector<std::string>& changes, const std::vector<std::string>& knownIssues) {
    std::string s = "# Kronos " + version + "\n\n## What's new\n";
    for (const auto& c : changes) s += "- " + c + "\n";
    s += "\n## Known problems\n";
    for (const auto& k : knownIssues) s += "- " + k + "\n";
    return s;
}

// Compatibility badge (972): shown only when backed by a recent passing run.
inline std::string compatBadge(int passed, int total, long long daysSinceRun) {
    if (total <= 0 || daysSinceRun > 30) return "unknown";
    return std::to_string(passed) + "/" + std::to_string(total) + " places pass";
}

// Analytics privacy (973): small groups are hidden so a person can't be picked out.
inline std::map<std::string, int> hideSmallGroups(const std::map<std::string, int>& counts, int minimum) {
    std::map<std::string, int> out;
    int hidden = 0;
    for (const auto& [k, v] : counts) {
        if (v >= minimum) out[k] = v;
        else hidden += v;
    }
    if (hidden > 0) out["other"] = hidden;
    return out;
}

// Credits page (974).
inline std::string creditsPage(const std::vector<std::pair<std::string, std::string>>& roleAndName) {
    std::string s = "# Credits\n";
    for (const auto& [role, name] : roleAndName) s += "- " + role + ": " + name + "\n";
    return s;
}

// Project health (975): open issues, days since the last release and the share of tests passing.
inline std::string projectHealth(int openIssues, long long daysSinceRelease, double testsPassingShare) {
    int score = 0;
    score += openIssues < 50 ? 1 : 0;
    score += daysSinceRelease < 60 ? 1 : 0;
    score += testsPassingShare >= 0.99 ? 1 : 0;
    return score == 3 ? "healthy" : score == 2 ? "ok" : "needs attention";
}

} // namespace engine::core::community
