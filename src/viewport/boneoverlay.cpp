#include "viewport/boneoverlay.h"

#include <QMatrix4x4>
#include <QQuaternion>
#include <algorithm>
#include <cmath>

#include "irisgl/mirror/scenemirror.h"

using namespace jahshaka::engine;

namespace {

/// A unit cube centred on the origin — the joint marker. Small, but a solid
/// pixel target where a 1-px line is not (the light-wire suite counts > 10
/// coloured pixels at 96x96; a thin humanoid rig has fewer than a ring does).
MeshData unitCube()
{
    MeshData m;
    const float h = 0.5f;
    const float p[8][3] = { { -h, -h, -h }, { h, -h, -h }, { h, h, -h }, { -h, h, -h },
                            { -h, -h,  h }, { h, -h,  h }, { h, h,  h }, { -h, h,  h } };
    const int faces[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 },
                              { 3, 7, 6, 2 }, { 0, 4, 7, 3 }, { 1, 2, 6, 5 } };
    const float n[6][3] = { { 0, 0, -1 }, { 0, 0, 1 }, { 0, -1, 0 },
                            { 0, 1, 0 }, { -1, 0, 0 }, { 1, 0, 0 } };
    for (int f = 0; f < 6; ++f) {
        const unsigned base = unsigned(m.positions.size() / 3);
        for (int v = 0; v < 4; ++v) {
            const int idx = faces[f][v];
            m.positions.insert(m.positions.end(), { p[idx][0], p[idx][1], p[idx][2] });
            m.normals.insert(m.normals.end(), { n[f][0], n[f][1], n[f][2] });
            m.uvs.insert(m.uvs.end(), { 0.0f, 0.0f });
        }
        m.indices.insert(m.indices.end(), { base, base + 1u, base + 2u, base, base + 2u, base + 3u });
    }
    return m;
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
    if (!mLineMesh) {
        // The one authored segment: +Y, unit length. Every bone is this mesh
        // under a translate * align * scale(1, length, 1) transform.
        mLineMesh = mTarget->createLineMesh({ Vec3(0, 0, 0), Vec3(0, 1, 0) }, false);
    }
    if (!mMarkerMesh) mMarkerMesh = mTarget->createMesh(unitCube());
    if (!mMaterial) {
        // depthTest = false: the gizmo/wire convention — the skeleton reads
        // through the skinned mesh when both toggles are on (§0.7).
        mMaterial = mTarget->createUnlitMaterial(
            Colour(float(mColour.redF()), float(mColour.greenF()), float(mColour.blueF()), 1.0f), false);
    }
    return mLineMesh && mMaterial;
}

void BoneOverlay::update(const QVector<BoneOverlaySegment> &segments, bool visible)
{
    mVisibleSegments = 0;
    if (!mTarget) return;
    const int wanted = visible ? segments.size() : 0;
    if (wanted > 0 && !ensureAssets()) return;

    // Marker size from the MEDIAN bone length, so one scale works for a
    // 2-unit test rig and a 179-unit Mixamo character alike (R0.7).
    float marker = 0.0f;
    if (wanted > 0 && mMarkerScale > 0.0f && mMarkerMesh) {
        std::vector<float> lengths;
        lengths.reserve(size_t(segments.size()));
        for (const auto &s : segments) lengths.push_back((s.to - s.from).length());
        std::nth_element(lengths.begin(), lengths.begin() + lengths.size() / 2, lengths.end());
        marker = lengths[lengths.size() / 2] * mMarkerScale;
    }

    for (int i = 0; i < wanted; ++i) {
        if (i >= mSlots.size()) mSlots.append(Slot());
        Slot &slot = mSlots[i];
        if (!slot.line) {
            slot.line = mTarget->createNode();
            if (slot.line) mTarget->attachMesh(slot.line, mLineMesh, mMaterial);
        }
        if (!slot.line) continue;

        const QVector3D from = segments[i].from;
        const QVector3D dir = segments[i].to - from;
        const float len = dir.length();
        QMatrix4x4 xf;
        xf.translate(from);
        if (len > 1e-6f)
            xf.rotate(QQuaternion::rotationTo(QVector3D(0, 1, 0), dir / len));
        xf.scale(1.0f, len > 1e-6f ? len : 1e-6f, 1.0f);
        SceneMirror::pushTransform(mTarget, slot.line, xf);
        mTarget->setNodeVisible(slot.line, true);

        if (marker > 0.0f) {
            if (!slot.marker) {
                slot.marker = mTarget->createNode();
                if (slot.marker) mTarget->attachMesh(slot.marker, mMarkerMesh, mMaterial);
            }
            if (slot.marker) {
                QMatrix4x4 m;
                m.translate(segments[i].to);
                m.scale(marker, marker, marker);
                SceneMirror::pushTransform(mTarget, slot.marker, m);
                mTarget->setNodeVisible(slot.marker, true);
            }
        } else if (slot.marker) {
            mTarget->setNodeVisible(slot.marker, false);
        }

        slot.shown = true;
        ++mVisibleSegments;
    }

    for (int i = wanted; i < mSlots.size(); ++i) {
        if (!mSlots[i].shown) continue;
        if (mSlots[i].line) mTarget->setNodeVisible(mSlots[i].line, false);
        if (mSlots[i].marker) mTarget->setNodeVisible(mSlots[i].marker, false);
        mSlots[i].shown = false;
    }
}

void BoneOverlay::clear()
{
    if (!mTarget) { mSlots.clear(); mVisibleSegments = 0; return; }
    for (Slot &s : mSlots) {
        if (s.line) mTarget->removeNode(s.line);
        if (s.marker) mTarget->removeNode(s.marker);
    }
    mSlots.clear();
    if (mLineMesh) { mTarget->destroyMesh(mLineMesh); mLineMesh = 0; }
    if (mMarkerMesh) { mTarget->destroyMesh(mMarkerMesh); mMarkerMesh = 0; }
    if (mMaterial) { mTarget->destroyMaterial(mMaterial); mMaterial = 0; }
    mVisibleSegments = 0;
}
