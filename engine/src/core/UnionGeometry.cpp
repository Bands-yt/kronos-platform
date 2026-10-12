#include "core/UnionGeometry.hpp"

#include <algorithm>
#include <sstream>

#include "core/Components.hpp"
#include "core/InstanceTree.hpp"

namespace engine::core::unions {
namespace {

struct UnionHelpers {
    std::string source;
    std::vector<EntityId> pieces;
    std::vector<Piece> layout;
};

const std::string* piecesText(ECS& ecs, EntityId e) {
    const auto* info = ecs.tryGetComponent<InstanceInfo>(e);
    if (info == nullptr || info->className != "UnionOperation") return nullptr;
    const auto it = info->properties.find(kPiecesProperty);
    if (it == info->properties.end() || it->second.type != InstanceValue::Type::String || it->second.text.empty()) {
        return nullptr;
    }
    return &it->second.text;
}

uint32_t meshFor(const InstanceMeshes& meshes, int shape) {
    if (shape == 0 && meshes.ball != ~0u) return meshes.ball;
    if (shape == 2 && meshes.cylinder != ~0u) return meshes.cylinder;
    if (shape == 3 && meshes.wedge != ~0u) return meshes.wedge;
    return meshes.box;
}

void destroyHelpers(ECS& ecs, UnionHelpers& helpers) {
    for (EntityId piece : helpers.pieces) {
        if (!ecs.raw().valid(piece)) continue;
        ecs.destroyEntity(piece);
    }
    helpers.pieces.clear();
}

void build(ECS& ecs, EntityId unionEntity, const std::string& text, const InstanceMeshes& meshes) {
    auto& helpers = ecs.raw().get_or_emplace<UnionHelpers>(unionEntity);
    destroyHelpers(ecs, helpers);
    helpers.source = text;
    helpers.layout = decodePieces(text);
    for (const Piece& piece : helpers.layout) {
        const EntityId e = ecs.createEntity("UnionPiece");
        ecs.addComponent<UnionPiece>(e, UnionPiece{unionEntity, piece.color, piece.shape});
        auto& r = ecs.addComponent<Renderable>(e);
        r.meshHandle = meshFor(meshes, piece.shape);
        r.baseColor = glm::vec4(piece.color, 1.0f);
        r.roughness = 0.85f;
        r.metallic = 0.0f;
        helpers.pieces.push_back(e);
    }
}

} // namespace

std::string encodePieces(const std::vector<Piece>& pieces) {
    std::ostringstream out;
    out.precision(7);
    for (const Piece& p : pieces) {
        out << p.shape << ' ' << p.position.x << ' ' << p.position.y << ' ' << p.position.z << ' ' << p.rotation.w
            << ' ' << p.rotation.x << ' ' << p.rotation.y << ' ' << p.rotation.z << ' ' << p.size.x << ' ' << p.size.y
            << ' ' << p.size.z << ' ' << p.color.r << ' ' << p.color.g << ' ' << p.color.b << ';';
    }
    return out.str();
}

std::vector<Piece> decodePieces(const std::string& text) {
    std::vector<Piece> pieces;
    std::istringstream in(text);
    std::string record;
    while (std::getline(in, record, ';') && pieces.size() < 4096) {
        std::istringstream fields(record);
        Piece p;
        if (fields >> p.shape >> p.position.x >> p.position.y >> p.position.z >> p.rotation.w >> p.rotation.x >>
            p.rotation.y >> p.rotation.z >> p.size.x >> p.size.y >> p.size.z >> p.color.r >> p.color.g >> p.color.b) {
            p.rotation = glm::normalize(p.rotation);
            pieces.push_back(p);
        }
    }
    return pieces;
}

bool hasPieces(ECS& ecs, EntityId e) { return piecesText(ecs, e) != nullptr; }

bool isPiece(ECS& ecs, EntityId e) { return ecs.raw().valid(e) && ecs.tryGetComponent<UnionPiece>(e) != nullptr; }

EntityId owner(ECS& ecs, EntityId e) {
    if (!ecs.raw().valid(e)) return e;
    const auto* piece = ecs.tryGetComponent<UnionPiece>(e);
    return piece != nullptr ? piece->owner : e;
}

void sync(ECS& ecs) {
    std::vector<EntityId> stale;
    for (auto [e, piece] : ecs.raw().view<UnionPiece>().each()) {
        const auto* helpers = ecs.raw().valid(piece.owner) ? ecs.tryGetComponent<UnionHelpers>(piece.owner) : nullptr;
        if (helpers == nullptr || std::find(helpers->pieces.begin(), helpers->pieces.end(), e) == helpers->pieces.end()) {
            stale.push_back(e);
        }
    }
    for (EntityId e : stale) ecs.destroyEntity(e);

    std::vector<EntityId> unionsWithHelpers;
    for (EntityId e : ecs.raw().view<UnionHelpers>()) unionsWithHelpers.push_back(e);
    for (EntityId e : unionsWithHelpers) {
        if (piecesText(ecs, e) == nullptr) {
            destroyHelpers(ecs, ecs.raw().get<UnionHelpers>(e));
            ecs.raw().remove<UnionHelpers>(e);
        }
    }

    const auto* meshes = ecs.raw().ctx().find<InstanceMeshes>();
    std::vector<EntityId> unionsToSync;
    for (auto [e, info] : ecs.raw().view<InstanceInfo>().each()) {
        if (info.className == "UnionOperation") unionsToSync.push_back(e);
    }
    for (EntityId e : unionsToSync) {
        const std::string* text = piecesText(ecs, e);
        auto* r = ecs.tryGetComponent<Renderable>(e);
        if (text == nullptr || r == nullptr) continue;
        auto* helpers = ecs.tryGetComponent<UnionHelpers>(e);
        const bool intact = helpers != nullptr && helpers->source == *text &&
                            std::all_of(helpers->pieces.begin(), helpers->pieces.end(),
                                        [&](EntityId p) { return isPiece(ecs, p); });
        if (!intact) {
            if (meshes == nullptr || meshes->box == ~0u) continue;
            build(ecs, e, *text, *meshes);
            helpers = ecs.tryGetComponent<UnionHelpers>(e);
            r = ecs.tryGetComponent<Renderable>(e);
        }
        // The pieces draw the union; its own box doesn't.
        r->meshHandle = Renderable::kInvalidHandle;
        // World space, so a rotated piece isn't sheared by the union's uneven size.
        const instances::Pose pose = instances::worldPose(ecs, e);
        for (size_t i = 0; i < helpers->pieces.size() && i < helpers->layout.size(); ++i) {
            const EntityId p = helpers->pieces[i];
            const Piece& piece = helpers->layout[i];
            if (auto* t = ecs.tryGetComponent<Transform>(p)) {
                t->position = pose.position + pose.rotation * (piece.position * pose.scale);
                t->rotation = pose.rotation * piece.rotation;
                t->scale = piece.size * pose.scale;
            }
            auto* pr = ecs.tryGetComponent<Renderable>(p);
            if (pr == nullptr) continue;
            pr->baseColor.a = r->baseColor.a;
            pr->visible = r->visible && r->baseColor.a > 0.001f;
            pr->castsShadow = r->castsShadow;
        }
    }
}

} // namespace engine::core::unions
