#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/ECS.hpp"
#include "core/PhysicsRollback.hpp"

struct lua_State;

namespace engine::core {

// A Luau script that runs inside a rollback match. All match state lives in one table that is
// saved with every snapshot, so a rollback rewinds the script together with the physics.
//   function init(players) return { score = {} } end  -- optional; the starting state
//   function step(state, ctx) ... end                 -- once per simulated frame
// See docs/ROLLBACK.md "Match scripts".
class RollbackScript {
public:
    RollbackScript() = default;
    ~RollbackScript();
    RollbackScript(const RollbackScript&) = delete;
    RollbackScript& operator=(const RollbackScript&) = delete;

    bool load(const std::string& chunkName, const std::string& source, std::string& error);
    [[nodiscard]] bool loaded() const { return L_ != nullptr; }

    // The encoded starting state, appended to the match's game state.
    bool initialState(uint32_t players, std::vector<uint8_t>& out, std::string& error);
    // Runs step() on the state stored in frame.gameState from `offset` on. `playerBodies` are
    // the players' character bodies (ctx.player). A failing step leaves the state unchanged.
    void step(RollbackFrameContext& frame, size_t offset, const std::vector<EntityId>& playerBodies);

    [[nodiscard]] const std::string& lastError() const { return lastError_; }
    [[nodiscard]] uint32_t errorCount() const { return errorCount_; }

    // Instruction budget per call, counted at loop back-edges and calls so it's the same on every machine.
    static constexpr uint32_t kStepBudget = 2'000'000;

private:
    static void interrupt(lua_State* L, int gc);
    static RollbackScript& self(lua_State* L);
    static RollbackFrameContext& frameIn(lua_State* L);
    bool call(int args, int results, std::string& error);
    void fail(const std::string& message);
    void pushContext();

    static int ctxInput(lua_State* L);
    static int ctxPlayer(lua_State* L);
    static int ctxFind(lua_State* L);
    static int ctxPosition(lua_State* L);
    static int ctxVelocity(lua_State* L);
    static int ctxSetPosition(lua_State* L);
    static int ctxSetVelocity(lua_State* L);
    static int ctxImpulse(lua_State* L);
    static int ctxMoveKinematic(lua_State* L);
    static int ctxContacts(lua_State* L);
    static int mathRandom(lua_State* L);
    static int mathRandomSeed(lua_State* L);

    lua_State* L_ = nullptr;
    lua_State* thread_ = nullptr;
    int threadRef_ = -1;
    int initRef_ = -1;
    int stepRef_ = -1;
    int contextRef_ = -1;
    uint64_t sourceHash_ = 0;
    uint64_t rng_ = 0;
    uint32_t budgetLeft_ = 0;
    RollbackFrameContext* frame_ = nullptr;
    const std::vector<EntityId>* playerBodies_ = nullptr;
    std::string lastError_;
    uint32_t errorCount_ = 0;
    std::vector<std::string> reported_;
};

namespace rollback_state {
// Canonical encoding of a Luau value (tables with sorted keys), so equal states give equal bytes.
bool encode(lua_State* L, int index, std::vector<uint8_t>& out, std::string& error);
// Pushes the decoded value; returns false (pushing nothing) on malformed bytes.
bool decode(lua_State* L, const uint8_t* data, size_t size, size_t& at);
} // namespace rollback_state

} // namespace engine::core
