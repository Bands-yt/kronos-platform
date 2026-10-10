#pragma once

#include <cstddef>
#include <string>

#include <nlohmann/json.hpp>

#include "core/ECS.hpp"

struct lua_State;

// Roblox DataStores, kept in a local JSON file per game (or only in memory,
// as in Studio). The script side lives in core/ScriptInstanceApi.cpp.
namespace engine::core::datastore {

inline constexpr size_t kMaxNameLength = 50;
inline constexpr size_t kMaxKeyLength = 50;
inline constexpr size_t kMaxValueBytes = 4 * 1024 * 1024;
// Roblox's per-minute budget for each request kind: 60 + 10 per player.
inline constexpr double kBudgetBase = 60.0;
inline constexpr double kBudgetPerPlayer = 10.0;
inline constexpr double kKeyWriteCooldownSeconds = 6.0;

enum class Request { Get, Set };

// Where this game's stores are saved. Empty keeps them in memory only.
void setFile(ECS& ecs, const std::string& path);
[[nodiscard]] std::string file(ECS& ecs);
// datastores/<slug>.json, or under KRONOS_DATASTORE_DIR when set.
[[nodiscard]] std::string defaultFile(const std::string& gameName);

bool get(ECS& ecs, const std::string& store, const std::string& scope, const std::string& key, nlohmann::json& out);
void set(ECS& ecs, const std::string& store, const std::string& scope, const std::string& key,
         const nlohmann::json& value);
bool remove(ECS& ecs, const std::string& store, const std::string& scope, const std::string& key,
            nlohmann::json& old);

// Spends one request. Over budget or writing a key too often only logs a
// warning (Roblox queues the request instead).
void charge(ECS& ecs, Request kind, const std::string& keyForWrite);

// Empty when fine, else a Roblox-style error.
[[nodiscard]] std::string checkName(const std::string& name);
[[nodiscard]] std::string checkKey(const std::string& key);

// Lua value <-> stored value. Only nil, booleans, numbers, strings and tables
// of them (arrays or string-keyed) can be stored.
bool toJson(lua_State* L, int index, nlohmann::json& out, std::string& error);
void pushJson(lua_State* L, const nlohmann::json& value);
// The stored text, or an error when it is too big or not valid UTF-8.
bool encode(const nlohmann::json& value, std::string& out, std::string& error);

} // namespace engine::core::datastore
