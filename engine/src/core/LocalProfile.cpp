#include "core/LocalProfile.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

namespace engine::core {

bool LocalProfile::ownsItem(const std::string& itemId) const {
    return std::find(ownedItemIds.begin(), ownedItemIds.end(), itemId) != ownedItemIds.end();
}

bool LocalProfile::hasRatedItem(const std::string& itemId) const {
    return std::find(ratedItemIds.begin(), ratedItemIds.end(), itemId) != ratedItemIds.end();
}

bool LocalProfile::isFriend(const std::string& friendId) const {
    return std::any_of(friends.begin(), friends.end(), [&](const FriendEntry& f) { return f.friendId == friendId; });
}

bool LocalProfile::hasPendingRequestTo(const std::string& friendId) const {
    return std::any_of(pendingRequests.begin(), pendingRequests.end(),
                        [&](const FriendEntry& f) { return f.friendId == friendId; });
}

uint64_t generateProfileId() {
    static std::mt19937_64 rng{std::random_device{}()};
    static std::uniform_int_distribution<uint64_t> dist(1, std::numeric_limits<uint64_t>::max());
    return dist(rng);
}

bool LocalProfile::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "PROFILE 1\n";
    out << "ID " << profileId << "\n";
    out << "CREATED " << createdAtUnixSeconds << "\n";
    out << "AGEGROUP " << static_cast<int>(ageGroup) << "\n";
    out << "CREATORVERIFIED " << (creatorVerified ? 1 : 0) << "\n";
    out << "CREATORID " << creatorId << "\n";
    out << "KRONOSCREDITS " << kronosCredits << "\n";
    for (const std::string& itemId : ownedItemIds) out << "OWNEDITEM " << itemId << "\n";
    out << "SKINTONEINDEX " << skinToneIndex << "\n";
    out << "HEADSHAPEINDEX " << headShapeIndex << "\n";
    out << "BODYHEIGHT " << bodyHeight << "\n";
    out << "BODYWIDTH " << bodyWidth << "\n";
    out << "BODYLIMBSCALE " << bodyLimbScale << "\n";
    out << "BODYTORSOLENGTH " << bodyTorsoLength << "\n";
    out << "BODYSHOULDERWIDTH " << bodyShoulderWidth << "\n";
    out << "ANIMOVERRIDEIDLE " << animOverrideIdleId << "\n";
    out << "ANIMOVERRIDEWALK " << animOverrideWalkId << "\n";
    out << "ANIMOVERRIDERUN " << animOverrideRunId << "\n";
    out << "ANIMOVERRIDEJUMPSTART " << animOverrideJumpStartId << "\n";
    out << "ANIMOVERRIDEJUMPAIR " << animOverrideJumpAirId << "\n";
    out << "ANIMOVERRIDEJUMPLAND " << animOverrideJumpLandId << "\n";
    for (const std::string& itemId : ratedItemIds) out << "RATEDITEM " << itemId << "\n";
    out << "QUALITYPRESET " << qualityPresetIndex << "\n";
    out << "VSYNCENABLED " << (vsyncEnabled ? 1 : 0) << "\n";
    out << "FPSCAP " << fpsCap << "\n";
    out << "MASTERVOLUME " << masterVolume << "\n";
    out << "MUSICVOLUME " << musicVolume << "\n";
    out << "SFXVOLUME " << sfxVolume << "\n";
    out << "UISCALE " << uiScale << "\n";
    out << "TEXTSCALE " << textScale << "\n";
    out << "COLORBLINDMODE " << colorblindModeIndex << "\n";
    out << "REDUCEDMOTION " << (reducedMotion ? 1 : 0) << "\n";
    out << "CLOTHINGFIT " << clothingFitIndex << "\n";
    for (const auto& [actionName, scancode] : inputBindingOverrides) {
        out << "INPUTBIND " << actionName << " " << scancode << "\n";
    }
    // FriendEntry: id first (never contains spaces -- it's a creatorId),
    // display name last on the line (free text, may contain spaces).
    for (const auto& f : friends) out << "FRIEND " << f.friendId << " " << f.displayName << "\n";
    for (const auto& f : pendingRequests) out << "PENDINGREQ " << f.friendId << " " << f.displayName << "\n";
    // FriendMessage: real multi-sub-line-per-record shape (text is real
    // free text), same convention marketplace::TransactionLog's own
    // TXN/ITEMID/ITEMNAME/SELLER record already establishes.
    for (const auto& m : friendMessages) {
        out << "FMSG " << (m.fromMe ? 1 : 0) << " " << m.timestampUnixSeconds << "\n";
        out << "FMSGFRIEND " << m.friendId << "\n";
        out << "FMSGTEXT " << m.text << "\n";
    }
    for (const auto& n : notifications) {
        out << "NOTIF " << static_cast<int>(n.kind) << " " << n.timestampUnixSeconds << " " << (n.read ? 1 : 0)
            << "\n";
        out << "NOTIFTITLE " << n.title << "\n";
        out << "NOTIFBODY " << n.body << "\n";
        out << "NOTIFRELATED " << n.relatedId << "\n";
    }
    // name is last on the line (never quoted -- loadFromFile reads it as
    // "everything after the key"), same trailing-string convention every
    // other name/label field in this codebase's own text formats uses.
    out << "NAME " << displayName << "\n";
    out << "END\n";
    return out.good();
}

bool LocalProfile::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("PROFILE", 0) != 0) return false;

    LocalProfile loaded;
    FriendMessage* currentMessage = nullptr;
    NotificationRecord* currentNotification = nullptr;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("ID ", 0) == 0) {
            loaded.profileId = std::strtoull(line.substr(3).c_str(), nullptr, 10);
        } else if (line.rfind("CREATED ", 0) == 0) {
            loaded.createdAtUnixSeconds = std::strtoll(line.substr(8).c_str(), nullptr, 10);
        } else if (line.rfind("AGEGROUP ", 0) == 0) {
            int ageGroupIndex = std::atoi(line.substr(9).c_str());
            loaded.ageGroup = ageGroupIndex == 1 ? AgeGroup::Minor : ageGroupIndex == 2 ? AgeGroup::Adult : AgeGroup::Unknown;
        } else if (line.rfind("CREATORVERIFIED ", 0) == 0) {
            loaded.creatorVerified = std::atoi(line.substr(16).c_str()) != 0;
        } else if (line.rfind("CREATORID ", 0) == 0) {
            loaded.creatorId = line.substr(10);
        } else if (line.rfind("KRONOSCREDITS ", 0) == 0) {
            loaded.kronosCredits = std::strtoll(line.substr(14).c_str(), nullptr, 10);
        } else if (line.rfind("OWNEDITEM ", 0) == 0) {
            loaded.ownedItemIds.push_back(line.substr(10));
        } else if (line.rfind("SKINTONEINDEX ", 0) == 0) {
            loaded.skinToneIndex = std::atoi(line.substr(14).c_str());
        } else if (line.rfind("HEADSHAPEINDEX ", 0) == 0) {
            loaded.headShapeIndex = std::atoi(line.substr(15).c_str());
        } else if (line.rfind("BODYHEIGHT ", 0) == 0) {
            loaded.bodyHeight = std::strtof(line.substr(11).c_str(), nullptr);
        } else if (line.rfind("BODYWIDTH ", 0) == 0) {
            loaded.bodyWidth = std::strtof(line.substr(10).c_str(), nullptr);
        } else if (line.rfind("BODYLIMBSCALE ", 0) == 0) {
            loaded.bodyLimbScale = std::strtof(line.substr(14).c_str(), nullptr);
        } else if (line.rfind("BODYTORSOLENGTH ", 0) == 0) {
            loaded.bodyTorsoLength = std::strtof(line.substr(16).c_str(), nullptr);
        } else if (line.rfind("BODYSHOULDERWIDTH ", 0) == 0) {
            loaded.bodyShoulderWidth = std::strtof(line.substr(18).c_str(), nullptr);
        } else if (line.rfind("ANIMOVERRIDEIDLE ", 0) == 0) {
            loaded.animOverrideIdleId = line.substr(17);
        } else if (line.rfind("ANIMOVERRIDEWALK ", 0) == 0) {
            loaded.animOverrideWalkId = line.substr(17);
        } else if (line.rfind("ANIMOVERRIDERUN ", 0) == 0) {
            loaded.animOverrideRunId = line.substr(16);
        } else if (line.rfind("ANIMOVERRIDEJUMPSTART ", 0) == 0) {
            loaded.animOverrideJumpStartId = line.substr(22);
        } else if (line.rfind("ANIMOVERRIDEJUMPAIR ", 0) == 0) {
            loaded.animOverrideJumpAirId = line.substr(20);
        } else if (line.rfind("ANIMOVERRIDEJUMPLAND ", 0) == 0) {
            loaded.animOverrideJumpLandId = line.substr(21);
        } else if (line.rfind("RATEDITEM ", 0) == 0) {
            loaded.ratedItemIds.push_back(line.substr(10));
        } else if (line.rfind("QUALITYPRESET ", 0) == 0) {
            loaded.qualityPresetIndex = std::atoi(line.substr(14).c_str());
        } else if (line.rfind("VSYNCENABLED ", 0) == 0) {
            loaded.vsyncEnabled = std::atoi(line.substr(13).c_str()) != 0;
        } else if (line.rfind("FPSCAP ", 0) == 0) {
            loaded.fpsCap = std::atoi(line.substr(7).c_str());
        } else if (line.rfind("MASTERVOLUME ", 0) == 0) {
            loaded.masterVolume = std::strtof(line.substr(13).c_str(), nullptr);
        } else if (line.rfind("MUSICVOLUME ", 0) == 0) {
            loaded.musicVolume = std::strtof(line.substr(12).c_str(), nullptr);
        } else if (line.rfind("SFXVOLUME ", 0) == 0) {
            loaded.sfxVolume = std::strtof(line.substr(10).c_str(), nullptr);
        } else if (line.rfind("UISCALE ", 0) == 0) {
            loaded.uiScale = std::strtof(line.substr(8).c_str(), nullptr);
        } else if (line.rfind("TEXTSCALE ", 0) == 0) {
            loaded.textScale = std::strtof(line.substr(10).c_str(), nullptr);
        } else if (line.rfind("COLORBLINDMODE ", 0) == 0) {
            loaded.colorblindModeIndex = std::atoi(line.substr(15).c_str());
        } else if (line.rfind("REDUCEDMOTION ", 0) == 0) {
            loaded.reducedMotion = std::atoi(line.substr(14).c_str()) != 0;
        } else if (line.rfind("CLOTHINGFIT ", 0) == 0) {
            loaded.clothingFitIndex = std::atoi(line.substr(12).c_str());
        } else if (line.rfind("INPUTBIND ", 0) == 0) {
            std::string rest = line.substr(10);
            size_t spacePos = rest.rfind(' ');
            if (spacePos != std::string::npos) {
                std::string actionName = rest.substr(0, spacePos);
                int scancode = std::atoi(rest.substr(spacePos + 1).c_str());
                loaded.inputBindingOverrides[actionName] = scancode;
            }
        } else if (line.rfind("FRIEND ", 0) == 0) {
            std::string rest = line.substr(7);
            size_t spacePos = rest.find(' ');
            FriendEntry f;
            f.friendId = spacePos == std::string::npos ? rest : rest.substr(0, spacePos);
            f.displayName = spacePos == std::string::npos ? std::string() : rest.substr(spacePos + 1);
            loaded.friends.push_back(std::move(f));
        } else if (line.rfind("PENDINGREQ ", 0) == 0) {
            std::string rest = line.substr(11);
            size_t spacePos = rest.find(' ');
            FriendEntry f;
            f.friendId = spacePos == std::string::npos ? rest : rest.substr(0, spacePos);
            f.displayName = spacePos == std::string::npos ? std::string() : rest.substr(spacePos + 1);
            loaded.pendingRequests.push_back(std::move(f));
        } else if (line.rfind("FMSG ", 0) == 0) {
            std::istringstream iss(line.substr(5));
            int fromMeInt = 0;
            FriendMessage m;
            iss >> fromMeInt >> m.timestampUnixSeconds;
            m.fromMe = fromMeInt != 0;
            loaded.friendMessages.push_back(std::move(m));
            currentMessage = &loaded.friendMessages.back();
            currentNotification = nullptr;
        } else if (line.rfind("FMSGFRIEND ", 0) == 0 && currentMessage != nullptr) {
            currentMessage->friendId = line.substr(11);
        } else if (line.rfind("FMSGTEXT ", 0) == 0 && currentMessage != nullptr) {
            currentMessage->text = line.substr(9);
        } else if (line.rfind("NOTIF ", 0) == 0) {
            std::istringstream iss(line.substr(6));
            int kindInt = 0;
            int readInt = 0;
            NotificationRecord n;
            iss >> kindInt >> n.timestampUnixSeconds >> readInt;
            n.kind = static_cast<NotificationKind>(kindInt);
            n.read = readInt != 0;
            loaded.notifications.push_back(std::move(n));
            currentNotification = &loaded.notifications.back();
            currentMessage = nullptr;
        } else if (line.rfind("NOTIFTITLE ", 0) == 0 && currentNotification != nullptr) {
            currentNotification->title = line.substr(11);
        } else if (line.rfind("NOTIFBODY ", 0) == 0 && currentNotification != nullptr) {
            currentNotification->body = line.substr(10);
        } else if (line.rfind("NOTIFRELATED ", 0) == 0 && currentNotification != nullptr) {
            currentNotification->relatedId = line.substr(13);
        } else if (line.rfind("NAME ", 0) == 0) {
            loaded.displayName = line.substr(5);
        } else if (line == "END") {
            break;
        }
        // Any other/unrecognized line is skipped -- forward-compatible
        // with a future field addition, same convention as SceneFile/
        // AnimationClip.
    }

    *this = loaded;
    return true;
}

LocalProfile loadOrCreateProfile(const std::string& path) {
    LocalProfile profile;
    bool loaded = profile.loadFromFile(path);
    if (!loaded) {
        profile = LocalProfile{};
        profile.profileId = generateProfileId();
        profile.createdAtUnixSeconds =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
    }
    // Kronos ("Creator Identity + Marketplace Publishing Pipeline"): real
    // backfill for a real, pre-existing (older) profile file saved before
    // creatorId existed -- derives the same real, stable id a fresh
    // profile would get, from the same real, already-assigned profileId,
    // rather than leaving an old profile with no real creator identity at
    // all.
    bool needsSave = !loaded;
    if (profile.creatorId.empty()) {
        profile.creatorId = "creator_" + std::to_string(profile.profileId);
        needsSave = true;
    }
    // A real, honest best-effort save -- if it fails (unwritable
    // directory, disk full), the caller still gets a real, usable
    // in-memory profile for this session; there's nothing actionable to
    // do with the failure here beyond that.
    if (needsSave) (void)profile.saveToFile(path);
    return profile;
}

} // namespace engine::core
