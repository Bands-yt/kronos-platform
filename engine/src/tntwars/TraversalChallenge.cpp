#include "tntwars/TraversalChallenge.hpp"

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

namespace engine::tntwars {

namespace {
TraversalChallengeState buildChallenge(const std::string& name, TraversalChallengeKind kind,
                                        const std::vector<glm::vec3>& waypointsInOrder, float checkpointRadius) {
    TraversalChallengeState challenge;
    challenge.name = name;
    challenge.kind = kind;
    challenge.checkpoints.reserve(waypointsInOrder.size());
    for (const glm::vec3& waypoint : waypointsInOrder) {
        challenge.checkpoints.push_back({waypoint, checkpointRadius});
    }
    return challenge;
}
} // namespace

TraversalChallengeState buildZipLineRaceChallenge(const std::string& name,
                                                    const std::vector<glm::vec3>& waypointsInOrder) {
    return buildChallenge(name, TraversalChallengeKind::ZipLineRace, waypointsInOrder, 4.0f);
}

TraversalChallengeState buildJumpPadRouteChallenge(const std::string& name,
                                                     const std::vector<glm::vec3>& waypointsInOrder) {
    return buildChallenge(name, TraversalChallengeKind::JumpPadRoute, waypointsInOrder, 4.0f);
}

ChallengeTickResult tickChallengeCheckpoint(TraversalChallengeState& challenge, glm::vec3 playerPos) {
    if (challenge.checkpoints.size() < 2) return ChallengeTickResult::NoChange;
    if (challenge.nextCheckpointIndex < 0 ||
        static_cast<size_t>(challenge.nextCheckpointIndex) >= challenge.checkpoints.size()) {
        return ChallengeTickResult::NoChange;
    }

    const ChallengeCheckpoint& target = challenge.checkpoints[static_cast<size_t>(challenge.nextCheckpointIndex)];
    if (glm::length(playerPos - target.position) > target.radius) return ChallengeTickResult::NoChange;

    if (challenge.nextCheckpointIndex == 0 && !challenge.running) {
        challenge.running = true;
        challenge.elapsedSeconds = 0.0f;
        challenge.nextCheckpointIndex = 1;
        return ChallengeTickResult::Started;
    }

    if (!challenge.running) return ChallengeTickResult::NoChange;

    challenge.nextCheckpointIndex += 1;
    if (static_cast<size_t>(challenge.nextCheckpointIndex) >= challenge.checkpoints.size()) {
        challenge.running = false;
        if (challenge.bestTimeSeconds < 0.0f || challenge.elapsedSeconds < challenge.bestTimeSeconds) {
            challenge.bestTimeSeconds = challenge.elapsedSeconds;
        }
        challenge.nextCheckpointIndex = 0; // real, honest reset -- ready to be started again
        return ChallengeTickResult::Completed;
    }

    return ChallengeTickResult::CheckpointReached;
}

void tickChallengeClock(TraversalChallengeState& challenge, float dt) {
    if (!challenge.running || dt <= 0.0f) return;
    challenge.elapsedSeconds += dt;
}

bool saveChallengeBestTimes(const std::vector<TraversalChallengeState>& challenges, const std::string& path) {
    nlohmann::json j = nlohmann::json::object();
    for (const TraversalChallengeState& challenge : challenges) {
        if (challenge.bestTimeSeconds < 0.0f) continue; // real, honest skip -- nothing real to persist yet
        j[challenge.name] = challenge.bestTimeSeconds;
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;
    out << j.dump(2);
    return out.good();
}

bool loadChallengeBestTimes(std::vector<TraversalChallengeState>& challenges, const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::stringstream buffer;
    buffer << in.rdbuf();

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(buffer.str());
    } catch (const nlohmann::json::parse_error&) {
        return false;
    }
    if (!j.is_object()) return false;

    for (TraversalChallengeState& challenge : challenges) {
        if (!j.contains(challenge.name)) continue;
        try {
            challenge.bestTimeSeconds = j.at(challenge.name).get<float>();
        } catch (const nlohmann::json::exception&) {
            continue; // real, honest skip -- a malformed single entry doesn't fail the whole real load
        }
    }
    return true;
}

} // namespace engine::tntwars
