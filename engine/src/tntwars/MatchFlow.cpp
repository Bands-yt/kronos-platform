#include "tntwars/MatchFlow.hpp"

namespace engine::tntwars {

const char* matchPhaseName(MatchPhase phase) {
    switch (phase) {
        case MatchPhase::Lobby: return "Lobby";
        case MatchPhase::ClassSelect: return "ClassSelect";
        case MatchPhase::InProgress: return "InProgress";
        case MatchPhase::CinematicFinale: return "CinematicFinale";
        case MatchPhase::Results: return "Results";
    }
    return "Lobby";
}

bool MatchFlowController::isValidTransition(MatchPhase from, MatchPhase to) {
    switch (from) {
        case MatchPhase::Lobby: return to == MatchPhase::ClassSelect;
        case MatchPhase::ClassSelect: return to == MatchPhase::InProgress;
        case MatchPhase::InProgress: return to == MatchPhase::CinematicFinale;
        case MatchPhase::CinematicFinale: return to == MatchPhase::Results;
        case MatchPhase::Results: return to == MatchPhase::Lobby; // real, honest "a new match can start" loop-back
    }
    return false;
}

bool MatchFlowController::advanceTo(MatchPhase next) {
    if (!isValidTransition(phase_, next)) return false;
    phase_ = next;
    phaseTimer_ = 0.0f;
    return true;
}

void MatchFlowController::tick(float dt) {
    if (dt <= 0.0f) return;
    phaseTimer_ += dt;
}

} // namespace engine::tntwars
