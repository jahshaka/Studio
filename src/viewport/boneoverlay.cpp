#include "irisgl/core/math/mat4.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "viewport/boneoverlay.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <vector>

#include "irisgl/mirror/scenemirror.h"

using namespace jahshaka::engine;

namespace {

// ---- the shape (see the header) -------------------------------------------
// A bone octahedron is authored as the UNIT bone: base apex at the parent joint
// (0,0,0), point at the child joint (0,1,0), a square ring of radius 0.5 at
// kRingAt along the way. The per-bone transform scales it to
// (girth, length, girth), so the authored radius 0.5 means "girth = the full
// width of the bone".
constexpr float kRingAt = 0.15f;

// Girth as a fraction of the bone's own length, clamped into a band around the
// rig's LIMB scale so one rule works for a 2-unit synthetic rig and a 179-unit
// Mixamo character alike: long bones do not swallow the mesh, finger bones stay
// thick enough to be a pixel target.
//
// The scale reference is the 75th PERCENTILE bone length, not the median: half
// of a Mixamo humanoid's bones are hand bones (Ely: 30 of 66 bones are fingers,
// 1.5-4 units, against 7-22 unit limbs), so a median-based ceiling clamps every
// limb bone down to finger width and the skeleton renders as thin lines again.
constexpr float kGirthRatio   = 0.14f;
constexpr float kGirthMinFrac = 0.05f;
constexpr float kGirthMaxFrac = 0.22f;

// A leaf bone's stub: a short bone past the last joint, in the leaf's own axis.
// Deliberately short — a Mixamo rig already ends every chain in an `_End` bone,
// so the stub is a tip marker, not another limb.
constexpr float kStubRatio   = 0.25f;   ///< of the bone that arrived at the leaf
constexpr float kStubMaxFrac = 0.30f;   ///< ... clamped against the rig's limb scale

/// Appends one triangle with a flat normal taken from the winding — the same
/// convention the rest of the tree uses (outward normal = cross(b-a, c-a)).
void addTriangle(MeshData &m, const iris::Vec3 &a, const iris::Vec3 &b, const iris::Vec3 &c)
{
    iris::Vec3 n = iris::Vec3::crossProduct(b - a, c - a);
    if (n.lengthSquared() > 1e-12f) n.normalize();
    const iris::Vec3 v[3] = { a, b, c };
    const unsigned base = unsigned(m.positions.size() / 3);
    for (const iris::Vec3 &p : v) {
        m.positions.insert(m.positions.end(), { p.x(), p.y(), p.z() });
        m.normals.insert(m.normals.end(), { n.x(), n.y(), n.z() });
        m.uvs.insert(m.uvs.end(), { 0.0f, 0.0f });
    }
    m.indices.insert(m.indices.end(), { base, base + 1u, base + 2u });
}

/// A double pyramid about the +Y axis: apex at `baseY`, a square ring of radius
/// `radius` at `ringY`, apex at `tipY`. Eight triangles, flat-shaded (the
/// material is unlit, so the normals are documentation more than shading).
MeshData octahedron(float baseY, float ringY, float tipY, float radius)
{
    MeshData m;
    const iris::Vec3 base(0, baseY, 0), tip(0, tipY, 0);
    const iris::Vec3 ring[4] = { iris::Vec3(radius, ringY, 0), iris::Vec3(0, ringY, radius),
                                iris::Vec3(-radius, ringY, 0), iris::Vec3(0, ringY, -radius) };
    for (int i = 0; i < 4; ++i) {
        const iris::Vec3 &r0 = ring[i], &r1 = ring[(i + 1) % 4];
        addTriangle(m, base, r0, r1);     // the wide end
        addTriangle(m, tip, r1, r0);      // the point
    }
    return m;
}

/// The rig's LIMB scale: the 75th-percentile bone length. Every derived size
/// (girth band, marker, stub cap) is expressed in it — see kGirthRatio for why
/// it is not the median.
float limbScale(const QVector<BoneOverlaySegment> &segments)
{
    std::vector<float> lengths;
    lengths.reserve(size_t(segments.size()));
    for (const auto &s : segments) {
        const float len = (s.to - s.from).length();
        if (len > 1e-6f) lengths.push_back(len);
    }
    if (lengths.empty()) return 0.0f;
    const size_t k = (lengths.size() * 3) / 4 < lengths.size() ? (lengths.size() * 3) / 4
                                                               : lengths.size() - 1;
    std::nth_element(lengths.begin(), lengths.begin() + k, lengths.end());
    return lengths[k];
}

/// translate(from) * rotate(+Y -> dir) * scale(girth, length, girth).
iris::Mat4 boneTransform(const iris::Vec3 &from, const iris::Vec3 &dir, float length, float girth)
{
    iris::Mat4 xf;
    xf.translate(from);
    if (length > 1e-6f) xf.rotate(iris::Quat::rotationTo(iris::Vec3(0, 1, 0), dir));
    xf.scale(girth, length > 1e-6f ? length : 1e-6f, girth);
    return xf;
}

} // namespace

BoneOverlay::BoneOverlay(Scene *target) : mTarget(target) {}

BoneOverlay::~BoneOverlay()
{
    // The engine scene may already be gone; owners call clear() first.
}

void BoneOverlay::setColour(const QColor &colour)
{
    mColour = colour;
    if (mMaterial)
        mTarget->setUnlitMaterial(mMaterial, Colour(float(colour.redF()), float(colour.greenF()),
                                                    float(colour.blueF()), 1.0f));
}

bool BoneOverlay::ensureAssets()
{
    if (!mTarget) return false;
    // The two authored unit meshes. Everything else is a transform.
    if (!mBoneMesh) mBoneMesh = mTarget->createMesh(octahedron(0.0f, kRingAt, 1.0f, 0.5f));
    if (!mMarkerMesh) mMarkerMesh = mTarget->createMesh(octahedron(-0.5f, 0.0f, 0.5f, 0.5f));
    if (!mMaterial) {
        // X-RAY (depth test off): the skeleton's job is to be seen THROUGH
        // the mesh — with depth on, mesh+skeleton mode shows 82 stray pixels
        // of fingertips (lead call at merge, 2026-09-03). The cost is that
        // overlapping bones merge into one silhouette; a real X-ray/occluded
        // two-tone mode stays future work.
        mMaterial = mTarget->createUnlitMaterial(
            Colour(float(mColour.redF()), float(mColour.greenF()), float(mColour.blueF()), 1.0f), false);
    }
    return mBoneMesh && mMaterial;
}

NodeId BoneOverlay::slot(QVector<NodeId> &pool, int index, MeshId mesh)
{
    while (pool.size() <= index) pool.append(NodeId(0));
    if (!pool[index]) {
        const NodeId node = mTarget->createNode();
        if (node) mTarget->attachMesh(node, mesh, mMaterial);
        pool[index] = node;
    }
    return pool[index];
}

void BoneOverlay::hideFrom(Scene *scene, const QVector<NodeId> &pool, int first)
{
    for (int i = first; i < pool.size(); ++i)
        if (pool[i]) scene->setNodeVisible(pool[i], false);
}

void BoneOverlay::update(const QVector<BoneOverlaySegment> &segments, bool visible)
{
    mVisibleSegments = mVisibleStubs = mVisibleJoints = 0;
    if (!mTarget) return;
    const bool draw = visible && !segments.isEmpty();
    if (draw && !ensureAssets()) return;
    if (!draw) {
        hideFrom(mTarget, mBoneNodes, 0);
        hideFrom(mTarget, mJointNodes, 0);
        return;
    }

    const float scale = limbScale(segments);
    const float girthMin = scale * kGirthMinFrac;
    const float girthMax = scale * kGirthMaxFrac;
    const float stubMax  = scale * kStubMaxFrac;

    // Joints are deduplicated by POSITION: a parent with three children is the
    // `from` of three segments and must still get exactly one marker. The cell
    // is a thousandth of the rig's limb scale, far below anything a rig resolves.
    const float cell = std::max(scale * 1e-3f, 1e-6f);
    QSet<qint64> seenJoints;
    QVector<iris::Vec3> joints;
    auto addJoint = [&](const iris::Vec3 &p) {
        const qint64 key = (qint64(std::llround(double(p.x() / cell))) * 73856093)
                         ^ (qint64(std::llround(double(p.y() / cell))) * 19349663)
                         ^ (qint64(std::llround(double(p.z() / cell))) * 83492791);
        if (seenJoints.contains(key)) return;
        seenJoints.insert(key);
        joints.append(p);
    };

    int bone = 0;
    for (const auto &seg : segments) {
        const iris::Vec3 delta = seg.to - seg.from;
        const float len = delta.length();
        const iris::Vec3 dir = len > 1e-6f ? delta / len : iris::Vec3(0, 1, 0);
        const float girth = qBound(girthMin, len * kGirthRatio, girthMax);

        if (const NodeId node = slot(mBoneNodes, bone, mBoneMesh)) {
            SceneMirror::pushTransform(mTarget, node, boneTransform(seg.from, dir, len, girth));
            mTarget->setNodeVisible(node, true);
            ++bone;
            ++mVisibleSegments;
        }
        addJoint(seg.from);
        addJoint(seg.to);
    }

    // Leaf stubs, drawn in the LEAF's own bone axis (measured: Mixamo bones run
    // along local +Y; the incoming direction is a decent but not exact stand-in,
    // so it is only the fallback).
    for (const auto &seg : segments) {
        if (!seg.tipIsLeaf) continue;
        const iris::Vec3 delta = seg.to - seg.from;
        const float len = delta.length();
        iris::Vec3 dir = seg.tipAxis;
        if (dir.lengthSquared() > 1e-12f) dir.normalize();
        else if (len > 1e-6f) dir = delta / len;
        else continue;
        const float stub = qMin(len * kStubRatio, stubMax);
        if (stub <= 1e-6f) continue;
        const float girth = qBound(girthMin, stub * kGirthRatio, girthMax);
        if (const NodeId node = slot(mBoneNodes, bone, mBoneMesh)) {
            SceneMirror::pushTransform(mTarget, node, boneTransform(seg.to, dir, stub, girth));
            mTarget->setNodeVisible(node, true);
            ++bone;
            ++mVisibleStubs;
        }
    }
    hideFrom(mTarget, mBoneNodes, bone);

    const float marker = mMarkerScale > 0.0f && mMarkerMesh ? scale * mMarkerScale : 0.0f;
    int drawn = 0;
    if (marker > 0.0f) {
        for (const iris::Vec3 &joint : joints) {
            const NodeId node = slot(mJointNodes, drawn, mMarkerMesh);
            if (!node) continue;
            iris::Mat4 m;
            m.translate(joint);
            m.scale(marker, marker, marker);
            SceneMirror::pushTransform(mTarget, node, m);
            mTarget->setNodeVisible(node, true);
            ++drawn;
        }
    }
    mVisibleJoints = drawn;
    hideFrom(mTarget, mJointNodes, drawn);
}

void BoneOverlay::clear()
{
    if (!mTarget) {
        mBoneNodes.clear();
        mJointNodes.clear();
        mVisibleSegments = mVisibleStubs = mVisibleJoints = 0;
        return;
    }
    for (NodeId n : mBoneNodes) if (n) mTarget->removeNode(n);
    for (NodeId n : mJointNodes) if (n) mTarget->removeNode(n);
    mBoneNodes.clear();
    mJointNodes.clear();
    if (mBoneMesh) { mTarget->destroyMesh(mBoneMesh); mBoneMesh = 0; }
    if (mMarkerMesh) { mTarget->destroyMesh(mMarkerMesh); mMarkerMesh = 0; }
    if (mMaterial) { mTarget->destroyMaterial(mMaterial); mMaterial = 0; }
    mVisibleSegments = mVisibleStubs = mVisibleJoints = 0;
}
