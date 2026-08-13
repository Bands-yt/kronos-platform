#include "core/Skeleton.hpp"

#include <fstream>
#include <sstream>
#include <unordered_set>

#include <glm/gtc/matrix_transform.hpp>

namespace engine::core {

int Skeleton::addJoint(Joint joint) {
    joints.push_back(std::move(joint));
    return static_cast<int>(joints.size()) - 1;
}

int Skeleton::findJointIndex(const std::string& jointName) const {
    for (size_t i = 0; i < joints.size(); ++i) {
        if (joints[i].name == jointName) return static_cast<int>(i);
    }
    return -1;
}

std::vector<glm::mat4> Skeleton::bindPoseMatrices() const {
    std::vector<glm::mat4> world(joints.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < joints.size(); ++i) {
        const Joint& joint = joints[i];
        glm::mat4 local = glm::translate(glm::mat4(1.0f), joint.localPosition) * glm::mat4_cast(joint.localRotation) *
                           glm::scale(glm::mat4(1.0f), joint.localScale);
        // Relies on the parent-before-child invariant validate() checks
        // -- joint.parentIndex, if set, is always < i here, so
        // world[joint.parentIndex] was already computed this same pass.
        world[i] = (joint.parentIndex >= 0) ? world[static_cast<size_t>(joint.parentIndex)] * local : local;
    }
    return world;
}

std::vector<glm::mat4> Skeleton::inverseBindMatrices() const {
    std::vector<glm::mat4> bind = bindPoseMatrices();
    std::vector<glm::mat4> inverse(bind.size());
    for (size_t i = 0; i < bind.size(); ++i) inverse[i] = glm::inverse(bind[i]);
    return inverse;
}

bool Skeleton::validate(std::string& outError) const {
    if (joints.empty()) {
        outError = "skeleton has no joints";
        return false;
    }

    std::unordered_set<std::string> seenNames;
    for (size_t i = 0; i < joints.size(); ++i) {
        const Joint& joint = joints[i];
        if (joint.name.empty()) {
            outError = "joint " + std::to_string(i) + " has an empty name";
            return false;
        }
        if (!seenNames.insert(joint.name).second) {
            outError = "duplicate joint name \"" + joint.name + "\" -- joint names must be unique for animation tracks to resolve unambiguously";
            return false;
        }
        if (joint.parentIndex < -1 || joint.parentIndex >= static_cast<int>(i)) {
            outError = "joint \"" + joint.name + "\" has an invalid parentIndex (" + std::to_string(joint.parentIndex) +
                        ") -- must be -1 (root) or an earlier joint's index, never itself or a later one";
            return false;
        }
    }
    return true;
}

bool Skeleton::saveToFile(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out.is_open()) return false;

    out << "SKELETON 1\n";
    out << "NAME " << name << "\n";
    for (const auto& joint : joints) {
        out << "JOINT " << joint.name << ' ' << joint.parentIndex << ' ' << joint.localPosition.x << ' '
            << joint.localPosition.y << ' ' << joint.localPosition.z << ' ' << joint.localRotation.x << ' '
            << joint.localRotation.y << ' ' << joint.localRotation.z << ' ' << joint.localRotation.w << ' '
            << joint.localScale.x << ' ' << joint.localScale.y << ' ' << joint.localScale.z << "\n";
    }
    out << "END\n";
    return out.good();
}

bool Skeleton::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;

    std::string header;
    if (!std::getline(in, header) || header.rfind("SKELETON", 0) != 0) return false;

    Skeleton loaded;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("NAME ", 0) == 0) {
            loaded.name = line.substr(5);
        } else if (line.rfind("JOINT ", 0) == 0) {
            std::istringstream iss(line.substr(6));
            Joint joint;
            iss >> joint.name >> joint.parentIndex >> joint.localPosition.x >> joint.localPosition.y >>
                joint.localPosition.z >> joint.localRotation.x >> joint.localRotation.y >> joint.localRotation.z >>
                joint.localRotation.w >> joint.localScale.x >> joint.localScale.y >> joint.localScale.z;
            if (!iss.fail()) loaded.joints.push_back(std::move(joint));
        } else if (line == "END") {
            break;
        }
        // Any other/unrecognized line is skipped -- forward-compatible
        // with a future field addition, same convention as AnimationClip.
    }

    *this = std::move(loaded);
    return true;
}

} // namespace engine::core
