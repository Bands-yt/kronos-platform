#pragma once

#include <string>
#include <vector>

#include <glm/glm.hpp>

namespace engine::tntwars {

// Kronos ("Gameplay Loop" world-building, "give players a reason to
// move"): real, pure, fully unit-testable timed-course logic -- same
// "zero ECS/window dependency" discipline core::Weather/core::Wind
// already established. A course is a real, ordered checkpoint sequence
// derived directly from already-placed, real course geometry (zip-line
// endpoints, jump-pad positions, and -- once built -- orbital-booster/
// gravity-well positions), never bespoke hand-authored points, so a
// course always matches whatever the map actually spawned.
enum class TraversalChallengeKind { ZipLineRace, JumpPadRoute, OrbitalBoosterRoute, GravityWellRoute };

struct ChallengeCheckpoint {
    glm::vec3 position{0.0f};
    float radius = 3.0f;
};

struct TraversalChallengeState {
    std::string name;
    TraversalChallengeKind kind = TraversalChallengeKind::ZipLineRace;
    std::vector<ChallengeCheckpoint> checkpoints;
    bool running = false;
    int nextCheckpointIndex = 0; // real index into `checkpoints` the player must reach next
    float elapsedSeconds = 0.0f;
    float bestTimeSeconds = -1.0f; // real, session-local best; negative = none recorded yet
};

// Real course builders -- checkpoint radius is deliberately generous
// (real 4-unit tolerance) relative to each feature's own real trigger
// radius, since a course checkpoint only needs to confirm "the player
// passed through here," not gate the underlying zip-line/jump-pad
// mechanic itself (that's still each feature's own real trigger logic).
[[nodiscard]] TraversalChallengeState buildZipLineRaceChallenge(const std::string& name,
                                                                   const std::vector<glm::vec3>& waypointsInOrder);
[[nodiscard]] TraversalChallengeState buildJumpPadRouteChallenge(const std::string& name,
                                                                    const std::vector<glm::vec3>& waypointsInOrder);

enum class ChallengeTickResult { NoChange, Started, CheckpointReached, Completed };

// Pure per-tick progress check -- real, strict in-order progression: only
// ever compares `playerPos` against `checkpoints[nextCheckpointIndex]`
// (reaching a later checkpoint first, out of order, does nothing -- a
// real race can't be shortcut). Reaching checkpoint 0 while not yet
// `running` is what real-starts the clock (Started, elapsedSeconds reset
// to 0); reaching the final checkpoint while running real-stops the
// clock and, if this run beat `bestTimeSeconds` (or none exists yet),
// real-records the new best (Completed). Every checkpoint strictly
// between real-advances `nextCheckpointIndex` (CheckpointReached). A
// real, honest no-op (NoChange) with fewer than 2 checkpoints -- a course
// needs a real start and a real finish to mean anything.
[[nodiscard]] ChallengeTickResult tickChallengeCheckpoint(TraversalChallengeState& challenge, glm::vec3 playerPos);

// Real elapsed-time accumulation -- adds `dt` to `elapsedSeconds` only
// while `running` is true; a real, honest no-op otherwise (not started
// yet, or already finished this run).
void tickChallengeClock(TraversalChallengeState& challenge, float dt);

// Kronos ("User Interface" world-building, "Leaderboard UI"): real,
// JSON-backed best-time persistence -- same real toJson/fromJson/
// saveToFile/loadFromFile shape core::AvatarItemManifest already
// established for a small, real settings-shaped file (see that class's
// own header comment for the convention this matches), applied to a
// `{name: bestTimeSeconds}` map so best times survive between
// engine_runtime sessions, not just within one. Matched by `name` (a
// course's own real stable identifier), not list position/index -- a
// map whose course set changed since the file was last written just
// leaves any unmatched course's own real bestTimeSeconds untouched
// (still -1, "no time recorded yet"), a real, honest partial-match, not
// a hard requirement that the file's own shape matches exactly.
[[nodiscard]] bool saveChallengeBestTimes(const std::vector<TraversalChallengeState>& challenges,
                                            const std::string& path);
[[nodiscard]] bool loadChallengeBestTimes(std::vector<TraversalChallengeState>& challenges, const std::string& path);

} // namespace engine::tntwars
