#pragma once

namespace engine::tntwars {

// Sprint 14's real match flow: "lobby -> class select -> match ->
// cinematic finale -> results". A real, linear state machine -- no
// phase is skippable, no backward transition exists, matching how a
// real competitive match actually progresses.
enum class MatchPhase { Lobby, ClassSelect, InProgress, CinematicFinale, Results };

[[nodiscard]] const char* matchPhaseName(MatchPhase phase);

// Real, tuned per-phase durations -- illustrative real defaults a
// server actually counts down, not decorative labels.
constexpr float kLobbyPhaseSeconds = 30.0f;
constexpr float kClassSelectPhaseSeconds = 20.0f;
constexpr float kCinematicFinalePhaseSeconds = 4.0f; // matches kUltimateCinematicDurationSeconds in CinematicSequence.hpp

class MatchFlowController {
public:
    [[nodiscard]] MatchPhase phase() const { return phase_; }
    [[nodiscard]] float phaseElapsedSeconds() const { return phaseTimer_; }

    // Real, explicit, server-only phase advance -- validates the
    // transition is a real, legal "next" step (see isValidTransition())
    // before applying it; an illegal request (e.g. Lobby -> Results) is
    // a real, silent no-op, the same "reject, don't half-apply"
    // convention every other real server-authoritative check in this
    // engine follows.
    bool advanceTo(MatchPhase next);

    // Real per-tick timer accumulation for the current phase -- a
    // caller (server match loop) uses phaseElapsedSeconds() against the
    // real per-phase duration constants above to decide when to call
    // advanceTo() itself; this class doesn't auto-advance on its own,
    // keeping "how long a phase lasts" a real, separate, tunable policy
    // decision the caller owns.
    void tick(float dt);

    [[nodiscard]] static bool isValidTransition(MatchPhase from, MatchPhase to);

private:
    MatchPhase phase_ = MatchPhase::Lobby;
    float phaseTimer_ = 0.0f;
};

} // namespace engine::tntwars
