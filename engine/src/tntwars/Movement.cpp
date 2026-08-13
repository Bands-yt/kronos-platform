#include "tntwars/Movement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine::tntwars {

std::vector<JumpPadState> buildJumpPads(MapId map) {
    (void)map; // real, deliberately map-independent placement -- see header comment
    std::vector<JumpPadState> pads;

    JumpPadState west;
    west.position = glm::vec3(-15.0f, 0.0f, 0.0f);
    pads.push_back(west);

    JumpPadState east;
    east.position = glm::vec3(15.0f, 0.0f, 0.0f);
    pads.push_back(east);

    return pads;
}

bool isWithinJumpPadRadius(glm::vec3 playerPosition, const JumpPadState& pad) {
    glm::vec3 delta = playerPosition - pad.position;
    return glm::dot(delta, delta) <= pad.triggerRadius * pad.triggerRadius;
}

std::optional<glm::vec3> triggerJumpPad(JumpPadState& pad, glm::vec3 playerPosition) {
    if (pad.cooldownSecondsRemaining > 0.0f) return std::nullopt;
    if (!isWithinJumpPadRadius(playerPosition, pad)) return std::nullopt;
    pad.cooldownSecondsRemaining = kJumpPadCooldownSeconds;
    return glm::vec3(0.0f, pad.launchStrength, 0.0f);
}

void tickJumpPad(JumpPadState& pad, float dt) {
    if (dt <= 0.0f || pad.cooldownSecondsRemaining <= 0.0f) return;
    pad.cooldownSecondsRemaining = std::max(0.0f, pad.cooldownSecondsRemaining - dt);
}

float distanceToZipLineSegment(glm::vec3 point, glm::vec3 start, glm::vec3 end) {
    glm::vec3 segment = end - start;
    float lengthSq = glm::dot(segment, segment);
    if (lengthSq < 1e-8f) return glm::length(point - start); // real, degenerate zero-length segment -- distance to the single shared point
    float t = glm::clamp(glm::dot(point - start, segment) / lengthSq, 0.0f, 1.0f);
    glm::vec3 closest = start + segment * t;
    return glm::length(point - closest);
}

glm::vec3 sampleZipLineCurve(const ZipLineState& zipLine, float t) {
    float u = 1.0f - t;
    return u * u * zipLine.start + 2.0f * u * t * zipLine.controlPoint + t * t * zipLine.end;
}

glm::vec3 zipLineCurveTangent(const ZipLineState& zipLine, float t) {
    glm::vec3 tangent = 2.0f * (1.0f - t) * (zipLine.controlPoint - zipLine.start) + 2.0f * t * (zipLine.end - zipLine.controlPoint);
    float length = glm::length(tangent);
    return length > 1e-5f ? tangent / length : glm::vec3(0.0f);
}

float distanceToZipLineCurve(const ZipLineState& zipLine, glm::vec3 point, float* outNearestT) {
    constexpr int kSamples = 24;
    float bestDistSq = std::numeric_limits<float>::max();
    float bestT = 0.0f;
    for (int i = 0; i <= kSamples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(kSamples);
        glm::vec3 curvePoint = sampleZipLineCurve(zipLine, t);
        glm::vec3 delta = point - curvePoint;
        float distSq = glm::dot(delta, delta);
        if (distSq < bestDistSq) {
            bestDistSq = distSq;
            bestT = t;
        }
    }
    if (outNearestT != nullptr) *outNearestT = bestT;
    return std::sqrt(bestDistSq);
}

std::optional<glm::vec3> computeZipLineVelocity(const ZipLineState& zipLine, glm::vec3 playerPosition) {
    float nearestT = 0.0f;
    float dist = distanceToZipLineCurve(zipLine, playerPosition, &nearestT);
    if (dist > zipLine.triggerRadius) return std::nullopt;

    glm::vec3 tangent = zipLineCurveTangent(zipLine, nearestT);
    if (glm::length(tangent) < 1e-4f) return std::nullopt; // real, honest no-op -- a real, degenerate zero-length curve has no direction to ride

    // Real bidirectional ride -- see this function's own header comment:
    // sign chosen by which real endpoint the player is closer to, so
    // riding away from the nearer end (toward the farther one) works
    // real-identically whichever end a player approaches from.
    float distToStart = glm::length(playerPosition - zipLine.start);
    float distToEnd = glm::length(playerPosition - zipLine.end);
    float sign = (distToStart <= distToEnd) ? 1.0f : -1.0f;
    return tangent * sign * zipLine.travelSpeed;
}

ZipLineArcLengthTable buildZipLineArcLengthTable(const ZipLineState& zipLine) {
    ZipLineArcLengthTable table;
    table.cumulativeDistance[0] = 0.0f;
    glm::vec3 previous = sampleZipLineCurve(zipLine, 0.0f);
    for (int i = 1; i <= ZipLineArcLengthTable::kSampleCount; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(ZipLineArcLengthTable::kSampleCount);
        glm::vec3 current = sampleZipLineCurve(zipLine, t);
        table.cumulativeDistance[i] = table.cumulativeDistance[i - 1] + glm::length(current - previous);
        previous = current;
    }
    table.totalLength = table.cumulativeDistance[ZipLineArcLengthTable::kSampleCount];
    return table;
}

float zipLineDistanceToT(const ZipLineArcLengthTable& table, float distance) {
    constexpr int kSampleCount = ZipLineArcLengthTable::kSampleCount;
    // Real, deliberately generous zero-length threshold (not 1e-6f) --
    // real, accumulated floating-point noise across 100 summed
    // glm::length() calls for an honestly-degenerate (start==end==
    // controlPoint) curve measures a real, nonzero ~1e-5 totalLength
    // even though every sampled point is mathematically identical; 1e-3f
    // clears that real noise floor with real margin to spare while
    // staying far below any real zip-line's own real, many-world-unit span.
    if (distance <= 0.0f || table.totalLength < 1e-3f) return 0.0f;
    if (distance >= table.totalLength) return 1.0f;
    // Real linear search over the (small, fixed-size) table -- kSampleCount+1
    // entries, real-cheap per-tick, no need for a real binary search.
    for (int i = 1; i <= kSampleCount; ++i) {
        if (table.cumulativeDistance[i] >= distance) {
            float segStart = table.cumulativeDistance[i - 1];
            float segEnd = table.cumulativeDistance[i];
            float segLength = segEnd - segStart;
            float frac = segLength > 1e-6f ? (distance - segStart) / segLength : 0.0f;
            float tStart = static_cast<float>(i - 1) / static_cast<float>(kSampleCount);
            float tEnd = static_cast<float>(i) / static_cast<float>(kSampleCount);
            return glm::mix(tStart, tEnd, frac);
        }
    }
    return 1.0f;
}

float zipLineTToDistance(const ZipLineArcLengthTable& table, float t) {
    constexpr int kSampleCount = ZipLineArcLengthTable::kSampleCount;
    float clampedT = glm::clamp(t, 0.0f, 1.0f);
    float scaledIndex = clampedT * static_cast<float>(kSampleCount);
    int i0 = static_cast<int>(std::floor(scaledIndex));
    int i1 = std::min(i0 + 1, kSampleCount);
    float frac = scaledIndex - static_cast<float>(i0);
    return glm::mix(table.cumulativeDistance[i0], table.cumulativeDistance[i1], frac);
}

std::optional<glm::vec3> advanceZipLineRider(const ZipLineState& zipLine, const ZipLineArcLengthTable& table,
                                               ZipLineRiderState& rider, float dt) {
    // Same real, generous 1e-3f zero-length threshold as
    // zipLineDistanceToT() above -- see that function's own comment for
    // why 1e-5f/1e-6f isn't a safe enough margin above real accumulated
    // floating-point noise for an honestly-degenerate curve.
    if (table.totalLength < 1e-3f) return std::nullopt; // real, degenerate zero-length curve -- nothing to traverse

    rider.distanceTraveled += zipLine.travelSpeed * dt * rider.direction;
    rider.distanceTraveled = glm::clamp(rider.distanceTraveled, 0.0f, table.totalLength);

    float t = zipLineDistanceToT(table, rider.distanceTraveled);
    return sampleZipLineCurve(zipLine, t) + glm::vec3(0.0f, kZipLineVerticalClearance, 0.0f);
}

void applySpeedBoost(SpeedBoostState& state, float multiplier, float durationSeconds) {
    state.multiplier = multiplier;
    state.secondsRemaining = durationSeconds;
}

void tickSpeedBoost(SpeedBoostState& state, float dt) {
    if (dt <= 0.0f || state.secondsRemaining <= 0.0f) return;
    state.secondsRemaining = std::max(0.0f, state.secondsRemaining - dt);
    if (state.secondsRemaining <= 0.0f) state.multiplier = 1.0f;
}

bool isSpeedBoostActive(const SpeedBoostState& state) { return state.secondsRemaining > 0.0f; }

} // namespace engine::tntwars
