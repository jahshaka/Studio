/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef STRUCTURALUNDO_H
#define STRUCTURALUNDO_H

#include "commands/staticstate.h"
#include "io/sceneformat.h"
#include "irisgl/irisglfwd.h"

struct StudioServices;

// -----------------------------------------------------------------------------
// UNDO v1.5 — the structural commands' shared half (SPECS/SCENEGRAPH_SPEC.md §3,
// "v1.5 — undo redesign"; audit F5).
//
// A structural edit is a node LEAVING a place and, on undo, going back to it.
// "The place" is a parent and a DOCUMENT SIBLING INDEX, and getting the index
// wrong is not cosmetic: the hierarchy panel shows sibling order, the serializer
// writes it, and a delete-then-undo that appended silently re-ordered the user's
// scene — which is exactly what every structural command did before this
// (`addChild` appends; only DeleteSceneNodeCommand even captured an index, and
// `ReparentSceneNodeCommand` threw one away on both sides, audit F5).
//
// THE CAPTURE is a SceneFragment (src/io/sceneformat.h): the subtree serialized
// through the same writer the scene file uses, plus the anchor (parent guid,
// sibling index) and the subtree's session node ids. Two things ride it:
//
//   * the RECORD. A command that has captured a fragment knows what it removed
//     independently of the live objects — which is what lets `reinstate` verify
//     that the node it is about to put back is still the node it took out, and
//     what a future cross-session or clipboard path would be built on
//     (`node.serialize` / `node.deserialize` are that path's first users).
//   * the REBUILD. When the live subtree is no longer usable, the fragment is
//     re-read into a new one — same guids, same names, same materials, same
//     session node ids — and put back in the same slot.
//
// LIVE FIRST, REBUILD SECOND, and the order is deliberate. A rebuild goes back
// through SceneReader, which re-resolves every mesh guid through the asset store
// and re-parses any model file it does not already have cached: on a 200-node
// subtree of the Showroom sample that is seconds, and an undo that stalls for
// seconds is a worse defect than the one this fixes. Re-attaching the live
// subtree is O(subtree) pointer work with identity, materials, meshes, physics
// bodies and engine state all exactly as they were. The fragment is not
// redundant — it is the thing that makes the fast path CHECKABLE and the slow
// path POSSIBLE — but it is not the common case, and pretending otherwise would
// have cost the owner interactive undo.
// -----------------------------------------------------------------------------
namespace structuralundo
{

/// Puts a subtree back under `parent` at DOCUMENT sibling index `index`
/// (-1 = append), returning the node that ended up there.
///
/// `live` is the node object the command has been holding since it removed it.
/// It is used when it is still consistent with `snapshot` — same node, not
/// already attached somewhere, still carrying a graph handle; otherwise the
/// snapshot is rebuilt through `services->sceneEdit`. A null return means both
/// paths failed (no parent, or no scene-edit service to rebuild with), and the
/// caller must leave the document alone rather than half-restore it.
iris::SceneNodePtr reinstate(StudioServices *services,
                             const iris::SceneNodePtr &parent,
                             const iris::SceneNodePtr &live,
                             const SceneFragment &snapshot,
                             int index);

/// True when `live` can be re-attached as-is. Exposed for the commands' own
/// bookkeeping (a command that is about to drop its live pointer wants to know
/// whether the snapshot is now the only copy).
bool liveIsUsable(const iris::SceneNodePtr &live, const SceneFragment &snapshot);

} // namespace structuralundo

#endif // STRUCTURALUNDO_H
