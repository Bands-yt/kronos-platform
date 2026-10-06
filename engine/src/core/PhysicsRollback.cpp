#include "core/PhysicsRollback.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>

namespace engine::core {

namespace {

constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
constexpr char kReplayMagic[4] = {'K', 'R', 'P', 'L'};
constexpr uint32_t kReplayVersion = 1;
constexpr size_t kChecksumHistory = 256;

int16_t quantizeAngle(float radians) {
    const auto turns = static_cast<int64_t>(std::lround(static_cast<double>(radians) / kTwoPi * 65536.0));
    return static_cast<int16_t>(static_cast<uint16_t>(turns & 0xFFFF));
}

int8_t quantizeAxis(float value) {
    return static_cast<int8_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 127.0f));
}

void stepFrame(Physics& physics, ECS& ecs, const RollbackStepFn& step, const RollbackInputs& inputs,
               uint32_t players, uint32_t frame, float dt, std::vector<uint8_t>& game,
               std::vector<Physics::CollisionEvent>& contacts) {
    if (step) {
        RollbackFrameContext context{physics, ecs, inputs, players, frame, dt, game, contacts};
        step(context);
    }
    (void)physics.drainCollisionEvents();
    physics.step(dt, ecs);
    contacts = physics.drainCollisionEvents();
}

template <typename T>
void put(std::vector<uint8_t>& out, const T& value) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

struct Reader {
    const std::vector<uint8_t>& bytes;
    size_t at = 0;
    bool ok = true;

    template <typename T>
    T get() {
        T value{};
        if (!ok || at + sizeof(T) > bytes.size()) {
            ok = false;
            return value;
        }
        std::memcpy(&value, bytes.data() + at, sizeof(T));
        at += sizeof(T);
        return value;
    }
    bool blob(std::vector<uint8_t>& out) {
        const auto size = get<uint32_t>();
        if (!ok || at + size > bytes.size()) return ok = false;
        out.assign(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.begin() + static_cast<std::ptrdiff_t>(at + size));
        at += size;
        return true;
    }
};

void putBlob(std::vector<uint8_t>& out, const std::vector<uint8_t>& blob) {
    put(out, static_cast<uint32_t>(blob.size()));
    out.insert(out.end(), blob.begin(), blob.end());
}

} // namespace

RollbackInput RollbackInput::make(glm::vec2 move, float yawRadians, float pitchRadians, uint16_t buttons) {
    RollbackInput input;
    input.moveX = quantizeAxis(move.x);
    input.moveZ = quantizeAxis(move.y);
    input.yaw = quantizeAngle(yawRadians);
    input.pitch = quantizeAngle(pitchRadians);
    input.buttons = buttons;
    return input;
}

float RollbackInput::yawRadians() const { return static_cast<float>(yaw) / 65536.0f * kTwoPi; }
float RollbackInput::pitchRadians() const { return static_cast<float>(pitch) / 65536.0f * kTwoPi; }

uint64_t rollbackChecksum(const std::vector<uint8_t>& physics, const std::vector<uint8_t>& game) {
    uint64_t hash = Physics::hashStateBytes(physics);
    for (uint8_t byte : game) hash = (hash ^ byte) * 1099511628211ull;
    return hash ^ game.size();
}

std::vector<uint8_t> PhysicsReplay::serialize() const {
    std::vector<uint8_t> out;
    out.insert(out.end(), kReplayMagic, kReplayMagic + 4);
    put(out, kReplayVersion);
    put(out, players);
    put(out, frameDt);
    putBlob(out, initialPhysics);
    putBlob(out, initialGame);
    put(out, static_cast<uint32_t>(frames.size()));
    for (const Frame& frame : frames) {
        for (uint32_t p = 0; p < players; ++p) put(out, frame.inputs[p]);
        put(out, frame.checksum);
    }
    return out;
}

bool PhysicsReplay::parse(const std::vector<uint8_t>& bytes, std::string& error) {
    if (bytes.size() < 8 || std::memcmp(bytes.data(), kReplayMagic, 4) != 0) {
        error = "not a Kronos replay";
        return false;
    }
    Reader reader{bytes, 4};
    if (reader.get<uint32_t>() != kReplayVersion) {
        error = "unsupported replay version";
        return false;
    }
    PhysicsReplay parsed;
    parsed.players = reader.get<uint32_t>();
    parsed.frameDt = reader.get<float>();
    if (!reader.ok || parsed.players == 0 || parsed.players > kMaxRollbackPlayers || !(parsed.frameDt > 0.0f)) {
        error = "bad replay header";
        return false;
    }
    if (!reader.blob(parsed.initialPhysics) || !reader.blob(parsed.initialGame)) {
        error = "truncated replay";
        return false;
    }
    const auto count = reader.get<uint32_t>();
    const size_t frameSize = parsed.players * sizeof(RollbackInput) + sizeof(uint64_t);
    if (!reader.ok || count > (bytes.size() - reader.at) / frameSize) {
        error = "truncated replay";
        return false;
    }
    parsed.frames.resize(count);
    for (Frame& frame : parsed.frames) {
        for (uint32_t p = 0; p < parsed.players; ++p) frame.inputs[p] = reader.get<RollbackInput>();
        frame.checksum = reader.get<uint64_t>();
    }
    if (!reader.ok || reader.at != bytes.size()) {
        error = "malformed replay";
        return false;
    }
    *this = std::move(parsed);
    return true;
}

bool PhysicsReplay::save(const std::string& path, std::string* error) const {
    const std::vector<uint8_t> bytes = serialize();
    const std::string temp = path + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            if (error) *error = "could not write " + temp;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        if (error) *error = "could not replace " + path + ": " + ec.message();
        return false;
    }
    return true;
}

bool PhysicsReplay::load(const std::string& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "could not open " + path;
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::string message;
    if (!parse(bytes, message)) {
        if (error) *error = path + ": " + message;
        return false;
    }
    return true;
}

PhysicsReplay::Verification PhysicsReplay::verify(Physics& physics, ECS& ecs, const RollbackStepFn& step) const {
    Verification result;
    if (!physics.restoreState(initialPhysics, &ecs)) return result;
    result.restored = true;
    std::vector<uint8_t> game = initialGame;
    std::vector<Physics::CollisionEvent> contacts;
    std::vector<uint8_t> bytes;
    for (uint32_t f = 0; f < frames.size(); ++f) {
        stepFrame(physics, ecs, step, frames[f].inputs, players, f, frameDt, game, contacts);
        physics.saveState(bytes);
        ++result.framesChecked;
        if (rollbackChecksum(bytes, game) != frames[f].checksum) {
            result.firstMismatch = f;
            break;
        }
    }
    result.finalGame = std::move(game);
    return result;
}

PhysicsRollback::State PhysicsRollback::initialState(Physics& physics, std::vector<uint8_t> game) {
    State state;
    state.serial = 1;
    physics.saveState(state.physics);
    state.game = std::move(game);
    return state;
}

PhysicsRollback::PhysicsRollback(Physics& physics, ECS& ecs, const Settings& settings, RollbackStepFn step,
                                 std::vector<uint8_t> initialGameState)
    : physics_(physics),
      ecs_(ecs),
      settings_(settings),
      step_(std::move(step)),
      session_(initialState(physics, std::move(initialGameState)),
               std::clamp<uint32_t>(settings.players, 1, kMaxRollbackPlayers), settings.localPlayer,
               [this](State& state, const RollbackInputs& inputs, uint32_t frame) { simulate(state, inputs, frame); },
               settings.maxRollbackFrames) {
    settings_.players = session_.playerCount();
    initialChecksum_ = rollbackChecksum(session_.state().physics, session_.state().game);
    (void)physics_.drainCollisionEvents();
    if (settings_.record) {
        replay_.players = settings_.players;
        replay_.frameDt = settings_.frameDt;
        replay_.initialPhysics = session_.state().physics;
        replay_.initialGame = session_.state().game;
    }
}

void PhysicsRollback::simulate(State& state, const RollbackInputs& inputs, uint32_t frame) {
    if (state.serial != liveSerial_) {
        if (!physics_.restoreState(state.physics, &ecs_)) restoreFailed_ = true;
    }
    stepFrame(physics_, ecs_, step_, inputs, settings_.players, frame, settings_.frameDt, state.game, state.contacts);
    physics_.saveState(state.physics);
    state.serial = liveSerial_ = nextSerial_++;
}

bool PhysicsRollback::advance(const RollbackInput& localInput) {
    if (!session_.advance(localInput)) return false;
    collectConfirmed();
    return true;
}

bool PhysicsRollback::addRemoteInput(uint32_t player, uint32_t frame, const RollbackInput& input) {
    return session_.addRemoteInput(player, frame, input);
}

void PhysicsRollback::synchronize() {
    session_.synchronize();
    // A rollback leaves the world at an older snapshot's serial only while
    // resimulating; once done, the live world matches state() again.
    if (session_.state().serial != liveSerial_ && !physics_.restoreState(session_.state().physics, &ecs_)) {
        restoreFailed_ = true;
    }
    collectConfirmed();
}

void PhysicsRollback::collectConfirmed() {
    const int64_t confirmed = session_.confirmedFrame();
    while (static_cast<int64_t>(nextChecksumFrame_) <= confirmed + 1 && nextChecksumFrame_ <= session_.currentFrame()) {
        const uint32_t frame = nextChecksumFrame_;
        const State* state = session_.stateAt(frame);
        RollbackInputs inputs{};
        if (!state || !session_.confirmedInputs(frame - 1, inputs)) break;
        const uint64_t sum = rollbackChecksum(state->physics, state->game);
        checksums_.emplace_back(frame, sum);
        if (checksums_.size() > kChecksumHistory) checksums_.pop_front();
        if (settings_.record) replay_.frames.push_back({inputs, sum});
        for (size_t i = 0; i < pendingRemote_.size();) {
            if (pendingRemote_[i].first == frame) {
                compare(frame, sum, pendingRemote_[i].second);
                pendingRemote_[i] = pendingRemote_.back();
                pendingRemote_.pop_back();
            } else {
                ++i;
            }
        }
        ++nextChecksumFrame_;
    }
}

std::optional<uint64_t> PhysicsRollback::checksum(uint32_t frame) const {
    for (const auto& [f, sum] : checksums_) {
        if (f == frame) return sum;
    }
    return std::nullopt;
}

std::optional<std::pair<uint32_t, uint64_t>> PhysicsRollback::latestChecksum() const {
    if (checksums_.empty()) return std::nullopt;
    return checksums_.back();
}

void PhysicsRollback::addRemoteChecksum(uint32_t frame, uint64_t remote) {
    if (const auto local = checksum(frame)) {
        compare(frame, *local, remote);
    } else if (frame >= nextChecksumFrame_ && pendingRemote_.size() < kChecksumHistory) {
        pendingRemote_.emplace_back(frame, remote);
    }
}

void PhysicsRollback::compare(uint32_t frame, uint64_t local, uint64_t remote) {
    if (local != remote && (!desyncFrame_ || frame < *desyncFrame_)) desyncFrame_ = frame;
}

} // namespace engine::core
