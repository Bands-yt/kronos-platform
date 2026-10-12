#include "trailer/TrailerGameMode.hpp"

namespace engine::trailer {

void TrailerGameMode::registerScripting(lua_State* L) {
    if (!api_) api_ = std::make_unique<core::TrailerScriptApi>(director_);
    api_->registerInto(L);
}

} // namespace engine::trailer
