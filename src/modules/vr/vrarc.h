/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef VRARC_H
#define VRARC_H

// VrArc — THE TELEPORT ARC AND ITS LANDING MARKER, DRAWN
// (SPECS/VR_INPUT_SPEC.md §6 row L4, phase 4b's polish stage).
//
// WHAT IT IS. A curve the wearer can see, and nothing else: it owns a handful
// of engine nodes on the two helper channels, places them from a list of
// world-space points each frame, and colours them by whether the landing under
// them is one a person may stand on. It holds no state about the gesture, asks
// nothing of the document, and computes no geometry — VrInteraction traces the
// arc (the maths is vrgrab.h's, the hits are the document picker's) and this
// object turns the answer into something in the world.
//
// WHY IT IS STUDIO-SIDE, unlike the controller ray beside it. The ray is a
// straight line between two points and the ENGINE draws it inside the frame
// (Scene::setVrRayNodes + Engine::setVrRay), because a ray drawn from a pose
// the host knew before the frame began leaves the wearer's own hand — one
// segment, one re-anchor, worth the boundary crossing. An arc is twenty
// segments whose SHAPE changes every frame; pushing it through the engine's
// boundary would mean a new state struct, a new engine-side drawer and a patch
// to the session's frame for a curve that is armed for about a second at a
// time. So it is built the way the editor's other helper geometry is built
// (GizmoOverlay, src/viewport/gizmooverlay.cpp): engine nodes made by the host,
// on `kHelperBit | kVrHelperBit`, with an unlit on-top material. The price is
// stated rather than hidden — the arc is drawn from the pose the host last
// heard, so at 90 Hz it lags the wearer's hand by a frame or two, which on a
// curve a person is aiming slowly is not visible and on a flick is.
//
// THE TWO CHANNELS. kHelperBit puts it in the desktop editor's viewport (so
// somebody at the desk sees where the wearer is about to go) and kVrHelperBit
// puts it in every VR eye, in the editor's preview AND in the Player — the
// same pair the controller wands and the ray carry, and the same exclusion:
// helper nodes reach no probe capture, no shadow map, no GI and no user-grade
// screenshot.
//
// LIFETIME. The engine Scene is NOT owned and can go away first (a viewport
// taken apart under a session). The owner hands it the scene through a
// callable every frame and this object compares: a different pointer — or none
// — means the scene it built on is gone, and it FORGETS its ids rather than
// calling into freed memory. `clear()` is the ordinary destruction, called
// while the scene is still alive.

#include <QVector>
#include <vector>

#include "irisgl/core/math/vec.h"
#include "jahshaka/engine/Engine.h"
#include "modules/vr/vrgrab.h"

class VrArc
{
public:
    explicit VrArc(jahshaka::engine::Scene *scene) : mTarget(scene) {}
    /// Does NOT touch the scene: by the time an owner is destroyed the scene
    /// may be too. Call clear() while it is alive.
    ~VrArc() = default;

    /// The scene this object built its nodes on.
    jahshaka::engine::Scene *target() const { return mTarget; }

    /// DRAW THE CURVE. `points` are world-space and in order; `valid` says
    /// whether the landing is one the wearer may be put on (green) or not
    /// (red); the marker stands at the last point when `landing` is true.
    /// Fewer than two points hides everything.
    void update(const QVector<iris::Vec3> &points, bool valid, bool landing);
    /// Hide every node, keep them (the next arm re-shows them).
    void hide();
    /// Destroy the nodes, meshes and materials. The scene must still be alive.
    void clear();
    /// FORGET the ids without touching the scene — it has been destroyed.
    void forget();

    /// For the suites and `vr.teleport()`'s report: how many segment nodes are
    /// shown right now, and whether anything has been built at all.
    int segmentsShown() const { return mShown; }
    bool built() const { return !mSegments.empty(); }
    bool markerShown() const { return mMarkerShown; }

private:
    /// HOW MANY PIECES THE POOL HOLDS — the curve's own sampling
    /// (vrgrab::kTeleportSegments), named once so the drawer and the tracer
    /// cannot come to disagree about how long an arc may be.
    static constexpr int kMaxSegments = vrgrab::kTeleportSegments;

    bool ensureBuilt();
    void showSegment(int index, const iris::Vec3 &from, const iris::Vec3 &to);

    jahshaka::engine::Scene *mTarget = nullptr;
    /// One node per straight piece, pooled: the arc is re-shaped, never rebuilt.
    std::vector<jahshaka::engine::NodeId> mSegments;
    jahshaka::engine::NodeId mMarker = 0;
    jahshaka::engine::MeshId mSegmentMesh = 0;   ///< a unit segment down -Z
    jahshaka::engine::MeshId mMarkerMesh = 0;    ///< a ring on the ground + a tick
    jahshaka::engine::MaterialId mMaterial = 0;         ///< the curve, depth-tested
    jahshaka::engine::MaterialId mMarkerMaterial = 0;   ///< the landing ring, on top
    bool mMaterialValid = true;                  ///< the colour it currently carries
    int mShown = 0;
    bool mMarkerShown = false;
};

#endif   // VRARC_H
