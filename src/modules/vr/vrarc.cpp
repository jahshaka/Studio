/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "modules/vr/vrarc.h"

#include <algorithm>
#include <cmath>

#include "irisgl/core/math/quat.h"

using namespace jahshaka::engine;

namespace {

/// THE TWO COLOURS, and they are the only thing the wearer has to read: GREEN
/// says "you will stand there", RED says "you will not". Nothing else about the
/// arc changes between the two states — the same curve, drawn to the same
/// place — because the question the wearer is asking is binary and the answer
/// should be too.
inline Colour okColour(float alpha) { return Colour(0.30f, 0.95f, 0.45f, alpha); }
inline Colour noColour(float alpha) { return Colour(0.95f, 0.32f, 0.25f, alpha); }

/// A ring on the ground with a short mast, one metre wide and drawn ON TOP —
/// the same reasoning the controller ray's hit marker carries: the marker sits
/// exactly ON the surface it marks and Vulkan applies NO depth bias to line
/// primitives at all (GIZMO-2), so the only cures are a lift off the thing it
/// is pointing at or a queue. The queue it is.
std::vector<Vec3> markerGeometry()
{
    std::vector<Vec3> pts;
    const float r = 0.35f;
    const int steps = 24;
    for (int i = 0; i < steps; ++i) {
        const float a0 = float(i) / float(steps) * 6.283185307179586f;
        const float a1 = float(i + 1) / float(steps) * 6.283185307179586f;
        pts.push_back(Vec3(std::cos(a0) * r, 0.0f, std::sin(a0) * r));
        pts.push_back(Vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r));
    }
    // The mast: a landing on a floor you are looking down at is a circle seen
    // edge-on, and a 40 cm upright is what makes it legible from a distance.
    pts.push_back(Vec3(0, 0, 0));
    pts.push_back(Vec3(0, 0.4f, 0));
    return pts;
}

}   // namespace

bool VrArc::ensureBuilt()
{
    if (!mTarget) return false;
    if (!mSegments.empty()) return true;

    // THE SEGMENT'S GEOMETRY IS A UNIT LINE DOWN -Z, exactly like the
    // controller ray's (SceneMirror): every piece of the curve is that one mesh
    // rotated onto its own direction and scaled along its own Z, so an arc that
    // changes shape every frame rebuilds no geometry at all. A line has no
    // thickness to distort under the non-uniform scale.
    const std::vector<Vec3> unit = { Vec3(0, 0, 0), Vec3(0, 0, -1) };
    mSegmentMesh = mTarget->createLineMesh(unit, false);
    mMarkerMesh = mTarget->createLineMesh(markerGeometry(), false);
    if (!mSegmentMesh || !mMarkerMesh) return false;
    // DEPTH-TESTED FOR THE CURVE, ON TOP FOR THE MARKER — the ray's own pair of
    // materials and the same two reasons: an arc that passed through a wall
    // would promise a landing behind it, and a marker lying on the floor it
    // marks cannot be depth-tested against it.
    mMaterial = mTarget->createUnlitMaterial(okColour(0.9f), true);
    mMarkerMaterial = mTarget->createUnlitMaterial(okColour(0.95f), false);
    if (!mMaterial || !mMarkerMaterial) return false;

    mSegments.reserve(size_t(kMaxSegments));
    for (int i = 0; i < kMaxSegments; ++i) {
        const NodeId n = mTarget->createNode();
        if (!n) break;
        // BOTH HELPER CHANNELS (the wands' and the ray's pair): kHelperBit puts
        // it in the desktop editor's picture, kVrHelperBit in every VR eye of
        // both hosts — and out of every probe capture, shadow map, GI input and
        // user-grade screenshot.
        mTarget->setNodeHelper(n, true);
        mTarget->setNodeVrHelper(n, true);
        mTarget->attachMesh(n, mSegmentMesh, mMaterial);
        mTarget->setNodeVisible(n, false);
        mSegments.push_back(n);
    }
    mMarker = mTarget->createNode();
    if (mMarker) {
        mTarget->setNodeHelper(mMarker, true);
        mTarget->setNodeVrHelper(mMarker, true);
        mTarget->attachMesh(mMarker, mMarkerMesh, mMarkerMaterial);
        mTarget->setNodeVisible(mMarker, false);
    }
    return !mSegments.empty();
}

void VrArc::showSegment(int index, const iris::Vec3 &from, const iris::Vec3 &to)
{
    if (index < 0 || index >= int(mSegments.size())) return;
    const NodeId node = mSegments[size_t(index)];
    if (!node) return;
    iris::Vec3 delta = to - from;
    const float length = delta.length();
    if (!(length > 1e-5f) || !std::isfinite(length)) {
        mTarget->setNodeVisible(node, false);
        return;
    }
    delta = delta / length;
    const iris::Quat rot = iris::Quat::rotationTo(iris::Vec3(0, 0, -1), delta).normalized();
    mTarget->setNodeTransform(node, Vec3(from.x(), from.y(), from.z()),
                              Quat(rot.x(), rot.y(), rot.z(), rot.scalar()),
                              Vec3(1.0f, 1.0f, length));
    mTarget->setNodeVisible(node, true);
}

void VrArc::update(const QVector<iris::Vec3> &points, bool valid, bool landing)
{
    if (!mTarget) return;
    if (points.size() < 2) { hide(); return; }
    if (!ensureBuilt()) return;

    // THE COLOUR, ON CHANGE ONLY (GizmoOverlay's rule, fps audit F14):
    // setUnlitMaterial schedules a constant-buffer write, and the only thing
    // that ever changes this one is the landing becoming legal or illegal.
    if (valid != mMaterialValid) {
        mTarget->setUnlitMaterial(mMaterial, valid ? okColour(0.9f) : noColour(0.9f));
        mTarget->setUnlitMaterial(mMarkerMaterial, valid ? okColour(0.95f) : noColour(0.95f));
        mMaterialValid = valid;
    }

    const int wanted = std::min(int(points.size()) - 1, int(mSegments.size()));
    for (int i = 0; i < wanted; ++i) showSegment(i, points[i], points[i + 1]);
    for (int i = wanted; i < int(mSegments.size()); ++i)
        if (mSegments[size_t(i)]) mTarget->setNodeVisible(mSegments[size_t(i)], false);
    mShown = wanted;

    // THE MARKER STANDS WHERE THE ARC ENDS, and only when the arc ENDED on
    // something: a curve that ran out of flight time has no landing to mark,
    // and a ring drawn in mid-air would be a promise the teleport will refuse.
    mMarkerShown = landing && mMarker != 0;
    if (mMarker) {
        if (mMarkerShown) {
            const iris::Vec3 &p = points.last();
            mTarget->setNodeTransform(mMarker, Vec3(p.x(), p.y(), p.z()),
                                      Quat(0.0f, 0.0f, 0.0f, 1.0f), Vec3(1, 1, 1));
        }
        mTarget->setNodeVisible(mMarker, mMarkerShown);
    }
}

void VrArc::hide()
{
    if (!mTarget) return;
    for (NodeId n : mSegments) if (n) mTarget->setNodeVisible(n, false);
    if (mMarker) mTarget->setNodeVisible(mMarker, false);
    mShown = 0;
    mMarkerShown = false;
}

void VrArc::clear()
{
    if (mTarget) {
        for (NodeId n : mSegments) if (n) mTarget->removeNode(n);
        if (mMarker) mTarget->removeNode(mMarker);
        if (mSegmentMesh) mTarget->destroyMesh(mSegmentMesh);
        if (mMarkerMesh) mTarget->destroyMesh(mMarkerMesh);
        if (mMaterial) mTarget->destroyMaterial(mMaterial);
        if (mMarkerMaterial) mTarget->destroyMaterial(mMarkerMaterial);
    }
    forget();
}

void VrArc::forget()
{
    mSegments.clear();
    mMarker = 0;
    mSegmentMesh = mMarkerMesh = 0;
    mMaterial = mMarkerMaterial = 0;
    mShown = 0;
    mMarkerShown = false;
    mMaterialValid = true;
}
