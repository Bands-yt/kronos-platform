#include "core/RobloxDataStore.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unordered_map>

#include <lua.h>
#include <lualib.h>

#include "core/InstanceSignals.hpp"
#include "core/LocalGameDirectory.hpp"
#include "core/Logger.hpp"
#include "core/RobloxPlayers.hpp"

namespace engine::core::datastore {
namespace {

constexpr int kMaxDepth = 100;

struct Stores {
    std::string path;
    bool loaded = false;
    nlohmann::json data = nlohmann::json::object(); // store -> scope -> key -> value
    double getTokens = -1.0;
    double setTokens = -1.0;
    double lastRefill = 0.0;
    std::unordered_map<std::string, double> lastWrite;
};

Stores& storesOf(ECS& ecs) {
    Stores& s = ecs.raw().ctx().emplace<Stores>();
    if (!s.loaded) {
        s.loaded = true;
        if (!s.path.empty()) {
            std::ifstream in(s.path);
            if (in) {
                nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
                if (parsed.is_object()) s.data = std::move(parsed);
                else logWarn("DataStore", "%s is damaged; starting empty", s.path.c_str());
            }
        }
    }
    return s;
}

void save(Stores& s) {
    if (s.path.empty()) return;
    std::error_code ec;
    const std::filesystem::path path(s.path);
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    const std::string temp = s.path + ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            logWarn("DataStore", "can't write %s", temp.c_str());
            return;
        }
        out << s.data.dump(1);
    }
    std::filesystem::rename(temp, path, ec);
    if (ec) logWarn("DataStore", "can't save %s: %s", s.path.c_str(), ec.message().c_str());
}

std::string typeName(lua_State* L, int index) {
    if (lua_getmetatable(L, index)) {
        lua_getfield(L, -1, "__type");
        std::string name = lua_isstring(L, -1) ? lua_tostring(L, -1) : "";
        lua_pop(L, 2);
        if (!name.empty()) return name;
    }
    return luaL_typename(L, index);
}

bool convert(lua_State* L, int index, nlohmann::json& out, std::string& error, int depth) {
    if (depth > kMaxDepth) {
        error = "Cannot store a table nested more than 100 levels deep";
        return false;
    }
    index = lua_absindex(L, index);
    switch (lua_type(L, index)) {
        case LUA_TNIL: out = nullptr; return true;
        case LUA_TBOOLEAN: out = lua_toboolean(L, index) != 0; return true;
        case LUA_TNUMBER: {
            const double n = lua_tonumber(L, index);
            if (!std::isfinite(n)) {
                error = "Cannot store NaN or infinity in data store";
                return false;
            }
            out = n;
            return true;
        }
        case LUA_TSTRING: {
            size_t size = 0;
            const char* text = lua_tolstring(L, index, &size);
            out = std::string(text, size);
            return true;
        }
        case LUA_TTABLE: {
            if (lua_getmetatable(L, index)) {
                lua_pop(L, 1);
                error = "Cannot store " + typeName(L, index) + " in data store";
                return false;
            }
            const int length = lua_objlen(L, index);
            int count = 0;
            bool stringKeys = true;
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                ++count;
                if (lua_type(L, -2) != LUA_TSTRING) stringKeys = false;
                lua_pop(L, 1);
            }
            if (count == 0 || (count == length && !stringKeys)) {
                out = nlohmann::json::array();
                for (int i = 1; i <= length; ++i) {
                    lua_rawgeti(L, index, i);
                    nlohmann::json item;
                    const bool ok = convert(L, -1, item, error, depth + 1);
                    lua_pop(L, 1);
                    if (!ok) return false;
                    out.push_back(std::move(item));
                }
                return true;
            }
            if (!stringKeys) {
                error = "Cannot store a table that mixes array items and other keys in data store";
                return false;
            }
            out = nlohmann::json::object();
            lua_pushnil(L);
            while (lua_next(L, index) != 0) {
                nlohmann::json item;
                if (!convert(L, -1, item, error, depth + 1)) {
                    lua_pop(L, 2);
                    return false;
                }
                out[lua_tostring(L, -2)] = std::move(item);
                lua_pop(L, 1);
            }
            return true;
        }
        default: error = "Cannot store " + typeName(L, index) + " in data store"; return false;
    }
}

} // namespace

void setFile(ECS& ecs, const std::string& path) {
    Stores& s = ecs.raw().ctx().emplace<Stores>();
    s = Stores{};
    s.path = path;
}

std::string file(ECS& ecs) { return storesOf(ecs).path; }

std::string defaultFile(const std::string& gameName) {
    const char* dir = std::getenv("KRONOS_DATASTORE_DIR");
    const std::string base = dir != nullptr && *dir != '\0' ? dir : "datastores";
    return (std::filesystem::path(base) / (slugifyGameName(gameName) + ".json")).string();
}

bool get(ECS& ecs, const std::string& store, const std::string& scope, const std::string& key, nlohmann::json& out) {
    const nlohmann::json& data = storesOf(ecs).data;
    const auto s = data.find(store);
    if (s == data.end()) return false;
    const auto sc = s->find(scope);
    if (sc == s->end()) return false;
    const auto k = sc->find(key);
    if (k == sc->end()) return false;
    out = *k;
    return true;
}

void set(ECS& ecs, const std::string& store, const std::string& scope, const std::string& key,
         const nlohmann::json& value) {
    Stores& s = storesOf(ecs);
    s.data[store][scope][key] = value;
    save(s);
}

bool remove(ECS& ecs, const std::string& store, const std::string& scope, const std::string& key,
            nlohmann::json& old) {
    Stores& s = storesOf(ecs);
    if (!get(ecs, store, scope, key, old)) return false;
    s.data[store][scope].erase(key);
    save(s);
    return true;
}

void charge(ECS& ecs, Request kind, const std::string& keyForWrite) {
    Stores& s = storesOf(ecs);
    const double now = signals::runService(ecs).time;
    const double perMinute = kBudgetBase + kBudgetPerPlayer * static_cast<double>(players::list(ecs).size());
    if (s.getTokens < 0.0) {
        s.getTokens = s.setTokens = perMinute;
        s.lastRefill = now;
    }
    const double refill = std::max(0.0, now - s.lastRefill) * perMinute / 60.0;
    s.lastRefill = now;
    // Roblox lets unused budget build up to three minutes' worth.
    s.getTokens = std::min(perMinute * 3.0, s.getTokens + refill);
    s.setTokens = std::min(perMinute * 3.0, s.setTokens + refill);
    double& tokens = kind == Request::Get ? s.getTokens : s.setTokens;
    if (tokens < 1.0) {
        logWarn("DataStore", "DataStore request was added to queue. If request queue fills, further requests will be "
                             "dropped. Try sending fewer requests.");
    }
    tokens = std::max(0.0, tokens - 1.0);
    if (kind == Request::Set && !keyForWrite.empty()) {
        const auto found = s.lastWrite.find(keyForWrite);
        if (found != s.lastWrite.end() && now - found->second < kKeyWriteCooldownSeconds) {
            logWarn("DataStore", "DataStore request was added to queue. Key: %s is written more often than every "
                                 "%.0f seconds.",
                    keyForWrite.c_str(), kKeyWriteCooldownSeconds);
        }
        s.lastWrite[keyForWrite] = now;
    }
}

std::string checkName(const std::string& name) {
    if (name.empty()) return "DataStore name can't be empty";
    if (name.size() > kMaxNameLength) return "DataStore name exceeds the 50 character limit";
    return {};
}

std::string checkKey(const std::string& key) {
    if (key.empty()) return "Key name can't be empty";
    if (key.size() > kMaxKeyLength) return "Key name exceeds the 50 character limit";
    return {};
}

bool toJson(lua_State* L, int index, nlohmann::json& out, std::string& error) {
    return convert(L, index, out, error, 0);
}

void pushJson(lua_State* L, const nlohmann::json& value) {
    if (value.is_boolean()) {
        lua_pushboolean(L, value.get<bool>() ? 1 : 0);
    } else if (value.is_number()) {
        lua_pushnumber(L, value.get<double>());
    } else if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        lua_pushlstring(L, text.data(), text.size());
    } else if (value.is_array()) {
        lua_createtable(L, static_cast<int>(value.size()), 0);
        int i = 1;
        for (const auto& item : value) {
            pushJson(L, item);
            lua_rawseti(L, -2, i++);
        }
    } else if (value.is_object()) {
        lua_createtable(L, 0, static_cast<int>(value.size()));
        for (const auto& [key, item] : value.items()) {
            pushJson(L, item);
            lua_setfield(L, -2, key.c_str());
        }
    } else {
        lua_pushnil(L);
    }
}

bool encode(const nlohmann::json& value, std::string& out, std::string& error) {
    try {
        out = value.dump();
    } catch (const nlohmann::json::exception&) {
        error = "Cannot store invalid UTF-8 text in data store";
        return false;
    }
    if (out.size() > kMaxValueBytes) {
        error = "Value exceeds the 4194304 character limit";
        return false;
    }
    return true;
}

} // namespace engine::core::datastore
