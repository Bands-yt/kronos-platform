#include "tntwars/Team.hpp"

namespace engine::tntwars {

const char* teamName(TeamId team) {
    switch (team) {
        case TeamId::A: return "TeamA";
        case TeamId::B: return "TeamB";
    }
    return "TeamA";
}

TeamId opposingTeam(TeamId team) { return team == TeamId::A ? TeamId::B : TeamId::A; }

} // namespace engine::tntwars
