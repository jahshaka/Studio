/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENESTATS_H
#define SCENESTATS_H

// THE SCENE'S OWN TRIANGLES (owner review 2026-09-18, Q3/R4a).
//
// The owner opened an empty new world, read "4,611 triangles" and asked what
// they were. The honest answer was that the readout was Ogre's per-frame
// `mFaceCount` — every triangle the renderer handed the GPU across EVERY pass
// in that frame: the ground drawn once for the picture and a second time for
// the SSR depth pre-pass, the horizon plane twice, the light icons, the sky
// quad, the sun disc, one full-screen quad per post step and the HUD's own
// glyphs. A true number, an honest engineering number, and NOT the answer to
// "what is in my scene".
//
// So there are two numbers now, and this file is the first of them:
//
//   sceneTriangles       WHAT IS IN THE SCENE. Counted HERE, on the DOCUMENT,
//                        once per visible mesh node — never by subtracting
//                        helpers from the GPU figure, which would be a guess
//                        that drifts the moment a helper changes.
//   submittedTriangles   what the renderer drew last frame, every pass
//                        included (Engine::renderStats — unchanged, relabelled).
//
// WHAT IT COUNTS, exactly:
//   * MESH NODES ONLY, and only the ones that are EFFECTIVELY visible
//     (SceneNode::isVisibleInScene — a hidden ancestor hides its subtree).
//   * ONE COUNT PER NODE. Two nodes sharing a MeshPtr (the load cache hands
//     the same mesh to every Cube in the world) are two objects in the scene
//     and count twice: that is what the user sees and what the renderer draws.
//   * TRIANGLES ONLY. A mesh whose PrimitiveMode is Lines / LineLoop /
//     LineStrip contributes ZERO, which is also what Ogre's face metric does
//     with a line primitive (R4a's measurement) — the two numbers agree about
//     what a triangle is.
//   * NOTHING THE MIRROR OWNS. The grid, the light icons and their range
//     wires, the sun disc, the 2 km horizon plane, the gizmos, the selection
//     outline and the VR helpers are engine-side objects that exist in no
//     document, so a document walk excludes them by construction rather than
//     by a list of names somebody has to maintain.
//
// THE AUTHORED (LOD 0) COUNT, DELIBERATELY. ATOM stage 1 bakes an LOD chain
// into every imported mesh and the renderer picks a level per draw by
// screen-space error, so the drawn triangle count moves when the CAMERA moves
// and nothing else changed. The owner's question is "what is in my scene", and
// the answer to that must not change when he walks backwards — so this counts
// `Mesh::numFaces`, the authored level-0 geometry, and the level actually
// drawn shows up in `submittedTriangles` where a moving number belongs.
//
// COST: one walk of the scene graph and no engine call. It is not free — the
// document's `children()` builds a QList of shared pointers per node, because
// the hierarchy lives in the engine's tree and the document holds no copy of
// it — so it is read at ~5 Hz by the F3 readout (which is OFF by default) and
// on demand by app.renderStats(), never per frame. If a caller ever wants it
// per frame, the answer is to cache it against the mirror's dirty set, not to
// walk faster.

#include <QtGlobal>

#include "irisgl/irisglfwd.h"

namespace scenestats {

/// What one walk found.
struct SceneGeometry
{
    /// Authored (LOD 0) triangles of every effectively-visible mesh node.
    quint64 triangles = 0;
    /// How many mesh nodes contributed them.
    quint64 meshNodes = 0;
    /// What the HIDDEN mesh nodes would have added — the other half of "what
    /// is in my scene", and the reason a user who hides the ground and sees
    /// the count drop to zero can tell that from an empty world.
    quint64 hiddenTriangles = 0;
    /// How many mesh nodes were skipped as hidden.
    quint64 hiddenMeshNodes = 0;
};

/// Walks `root` and everything under it. A null pointer answers a zeroed
/// struct; a hidden node's whole subtree is charged to the hidden columns.
SceneGeometry geometryUnder(const iris::SceneNodePtr &root);

/// The same for a scene (its root node). Null-safe.
SceneGeometry sceneGeometry(const iris::ScenePtr &scene);

}   // namespace scenestats

#endif // SCENESTATS_H
