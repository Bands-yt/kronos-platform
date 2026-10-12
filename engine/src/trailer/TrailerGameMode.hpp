#pragma once

#include "core/GameMode.hpp"
#include "core/TrailerScriptApi.hpp"

#include <memory>

namespace engine::trailer {

class TrailerDirector;

// --trailer: exposes the `cinematic` script table backed by a TrailerDirector.
class TrailerGameMode final : public core::GameMode {
public:
    explicit TrailerGameMode(TrailerDirector& director) : director_(director) {}

    void registerScripting(lua_State* L) override;

private:
    TrailerDirector& director_;
    std::unique_ptr<core::TrailerScriptApi> api_;
};

} // namespace engine::trailer
