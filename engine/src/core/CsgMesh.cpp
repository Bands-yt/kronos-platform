#include "core/CsgMesh.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace engine::core {

namespace {

constexpr float kCsgEpsilon = 1e-5f;

struct Plane {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float w = 0.0f;

    void flip() {
        normal = -normal;
        w = -w;
    }
};

struct Polygon {
    std::vector<glm::vec3> points;
    Plane plane;
};

enum class PointSide : uint8_t { Coplanar = 0, Front = 1, Back = 2 };

// Real Sutherland-Hodgman-style clip of `polygon` against `plane` -- the
// classic csg.js/BSP split step. Coplanar polygons are sorted into
// `coplanarFront`/`coplanarBack` by their own facing direction relative
// to `plane`; strictly front/back polygons go to `front`/`back`
// unmodified; a SPANNING polygon is cut into a real front piece and a
// real back piece, each still convex (clipping a convex polygon against
// a half-space always yields a convex result), so fan triangulation of
// the final output later is always valid.
void splitPolygon(const Plane& plane, const Polygon& polygon, std::vector<Polygon>& coplanarFront,
                   std::vector<Polygon>& coplanarBack, std::vector<Polygon>& front, std::vector<Polygon>& back) {
    std::vector<uint8_t> types;
    types.reserve(polygon.points.size());
    uint8_t polygonType = 0;
    for (const glm::vec3& p : polygon.points) {
        float t = glm::dot(plane.normal, p) - plane.w;
        uint8_t type = (t < -kCsgEpsilon) ? static_cast<uint8_t>(PointSide::Back)
                       : (t > kCsgEpsilon) ? static_cast<uint8_t>(PointSide::Front)
                                           : static_cast<uint8_t>(PointSide::Coplanar);
        polygonType = static_cast<uint8_t>(polygonType | type);
        types.push_back(type);
    }

    if (polygonType == static_cast<uint8_t>(PointSide::Coplanar)) {
        (glm::dot(plane.normal, polygon.plane.normal) > 0.0f ? coplanarFront : coplanarBack).push_back(polygon);
    } else if (polygonType == static_cast<uint8_t>(PointSide::Front)) {
        front.push_back(polygon);
    } else if (polygonType == static_cast<uint8_t>(PointSide::Back)) {
        back.push_back(polygon);
    } else {
        // SPANNING (both Front and Back bits set).
        std::vector<glm::vec3> f, b;
        size_t n = polygon.points.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            uint8_t ti = types[i], tj = types[j];
            const glm::vec3& vi = polygon.points[i];
            const glm::vec3& vj = polygon.points[j];
            if (ti != static_cast<uint8_t>(PointSide::Back)) f.push_back(vi);
            if (ti != static_cast<uint8_t>(PointSide::Front)) b.push_back(vi);
            if ((ti | tj) == 3) { // this edge crosses the plane
                float t = (plane.w - glm::dot(plane.normal, vi)) / glm::dot(plane.normal, vj - vi);
                glm::vec3 v = glm::mix(vi, vj, t);
                f.push_back(v);
                b.push_back(v);
            }
        }
        if (f.size() >= 3) front.push_back(Polygon{f, polygon.plane});
        if (b.size() >= 3) back.push_back(Polygon{b, polygon.plane});
    }
}

// Real BSP tree node -- one node per real splitting plane, exactly the
// structure the classic algorithm builds. front_/back_ are null for a
// leaf; polygons_ holds every polygon coplanar with this node's own
// plane (both facing directions).
class BspNode {
public:
    void build(const std::vector<Polygon>& polygons) {
        if (polygons.empty()) return;
        if (!hasPlane_) {
            plane_ = polygons[0].plane;
            hasPlane_ = true;
        }
        std::vector<Polygon> frontList, backList;
        for (const Polygon& poly : polygons) {
            splitPolygon(plane_, poly, polygons_, polygons_, frontList, backList);
        }
        if (!frontList.empty()) {
            if (!front_) front_ = std::make_unique<BspNode>();
            front_->build(frontList);
        }
        if (!backList.empty()) {
            if (!back_) back_ = std::make_unique<BspNode>();
            back_->build(backList);
        }
    }

    // Real "remove every polygon (or part of a polygon) that's inside
    // this tree" -- both the direct clipPolygons() query and clipTo()'s
    // recursive self-mutation below share this.
    [[nodiscard]] std::vector<Polygon> clipPolygons(const std::vector<Polygon>& polygons) const {
        if (!hasPlane_) return polygons;
        std::vector<Polygon> front, back;
        for (const Polygon& poly : polygons) {
            splitPolygon(plane_, poly, front, back, front, back);
        }
        if (front_) front = front_->clipPolygons(front);
        if (back_) back = back_->clipPolygons(back);
        else back.clear(); // no back child = solid interior here, discarded
        front.insert(front.end(), back.begin(), back.end());
        return front;
    }

    void clipTo(const BspNode& other) {
        polygons_ = other.clipPolygons(polygons_);
        if (front_) front_->clipTo(other);
        if (back_) back_->clipTo(other);
    }

    void invert() {
        for (Polygon& poly : polygons_) {
            std::reverse(poly.points.begin(), poly.points.end());
            poly.plane.flip();
        }
        plane_.flip();
        if (front_) front_->invert();
        if (back_) back_->invert();
        std::swap(front_, back_);
    }

    [[nodiscard]] std::vector<Polygon> allPolygons() const {
        std::vector<Polygon> result = polygons_;
        if (front_) {
            auto fp = front_->allPolygons();
            result.insert(result.end(), fp.begin(), fp.end());
        }
        if (back_) {
            auto bp = back_->allPolygons();
            result.insert(result.end(), bp.begin(), bp.end());
        }
        return result;
    }

private:
    bool hasPlane_ = false;
    Plane plane_;
    std::vector<Polygon> polygons_;
    std::unique_ptr<BspNode> front_;
    std::unique_ptr<BspNode> back_;
};

std::vector<Polygon> editableMeshToPolygons(const EditableMesh& mesh) {
    std::vector<Polygon> polygons;
    polygons.reserve(mesh.faceCount());
    for (size_t f = 0; f < mesh.faceCount(); ++f) {
        auto [ia, ib, ic] = mesh.faceVertexIndices(f);
        Polygon poly;
        poly.points = {mesh.vertices()[ia].position, mesh.vertices()[ib].position, mesh.vertices()[ic].position};
        // Real outward normal, sourced from the mesh's own stored
        // per-vertex normal -- NOT re-derived via cross(edge1, edge2).
        // An earlier version of this function did that and silently
        // built an INSIDE-OUT BSP tree: this engine's actual triangle
        // winding (createBox() et al.) doesn't match the "CCW from
        // outside" convention that cross-product formula assumes, so
        // every boolean op computed a normal pointing the wrong way.
        // Found via a real regression this file's own tests caught (two
        // fully disjoint boxes; union incorrectly returned empty). The
        // stored vertex normal is the one value the renderer/lighting
        // path already trusts as authoritative, so it's also the one
        // trusted here -- sidesteps the winding question entirely
        // instead of guessing at the right cross-product order.
        poly.plane.normal = mesh.vertices()[ia].normal;
        poly.plane.w = glm::dot(poly.plane.normal, poly.points[0]);
        polygons.push_back(std::move(poly));
    }
    return polygons;
}

EditableMesh polygonsToEditableMesh(const std::vector<Polygon>& polygons) {
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    for (const Polygon& poly : polygons) {
        if (poly.points.size() < 3) continue; // degenerate sliver, dropped rather than emitting a zero-area face
        uint32_t base = static_cast<uint32_t>(vertices.size());
        for (const glm::vec3& p : poly.points) {
            vertices.push_back(Vertex{p, poly.plane.normal, glm::vec2(0.0f)});
        }
        // Real fan triangulation -- valid because every polygon reaching
        // here is convex (see this file's own splitPolygon() comment).
        for (uint32_t i = 1; i + 1 < poly.points.size(); ++i) {
            indices.insert(indices.end(), {base, base + i, base + i + 1});
        }
    }
    return EditableMesh::fromVertexData(std::move(vertices), std::move(indices));
}

} // namespace

EditableMesh booleanOp(const EditableMesh& a, const EditableMesh& b, CsgOperation op) {
    BspNode A, B;
    A.build(editableMeshToPolygons(a));
    B.build(editableMeshToPolygons(b));

    // The exact classic csg.js union/subtract/intersect sequences
    // (Evan Wallace's public-domain BSP-CSG implementation) -- a direct
    // port, not a re-derivation, since getting the invert()/clipTo()
    // ordering subtly wrong silently produces an inside-out or
    // partially-hollow result rather than a compile/runtime error.
    switch (op) {
        case CsgOperation::Union:
            A.clipTo(B);
            B.clipTo(A);
            B.invert();
            B.clipTo(A);
            B.invert();
            A.build(B.allPolygons());
            break;
        case CsgOperation::Subtract:
            A.invert();
            A.clipTo(B);
            B.clipTo(A);
            B.invert();
            B.clipTo(A);
            B.invert();
            A.build(B.allPolygons());
            A.invert();
            break;
        case CsgOperation::Intersect:
            A.invert();
            B.clipTo(A);
            B.invert();
            A.clipTo(B);
            B.clipTo(A);
            A.build(B.allPolygons());
            A.invert();
            break;
    }
    return polygonsToEditableMesh(A.allPolygons());
}

float signedVolume(const EditableMesh& mesh) {
    float volume = 0.0f;
    for (size_t f = 0; f < mesh.faceCount(); ++f) {
        auto [ia, ib, ic] = mesh.faceVertexIndices(f);
        const glm::vec3& a = mesh.vertices()[ia].position;
        const glm::vec3& b = mesh.vertices()[ib].position;
        const glm::vec3& c = mesh.vertices()[ic].position;
        volume += glm::dot(a, glm::cross(b, c)) / 6.0f;
    }
    return volume;
}

} // namespace engine::core
