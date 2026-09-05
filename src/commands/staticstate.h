/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef STATICSTATE_H
#define STATICSTATE_H

#include <QVector>

#include "irisgl/irisglfwd.h"

namespace structuralundo
{

// ---- SCENE_STATIC across an undo (scripting audit F3) ----------------------
//
// MOVING A STATIC NODE DEMOTES IT — rule 4 of SCENEGRAPH_SPEC §6, and it is the
// right rule: a node that is being dragged must not pay a static-subtree
// migration a frame. But UNDOING that move puts the world back in the state it
// was in before, and in that state the node was static. Nothing put it back:
// `TransformSceneNodeCommand::undo` writes the old TRS, which is itself a
// transform write, which demotes again. So one accidental nudge-then-undo left
// the ground plane, the architecture and every imported prop permanently in the
// engine's per-frame transform and bounds passes, for the rest of the session
// AND — since the override is what the serializer writes — across saves.
//
// The fix is undo's job, not the graph's: capture the classification of the
// whole subtree before the write, restore it after the undo. Both halves are
// pre-order walks, which is also the order rule 2 needs (a node may only be
// made static once its parent already is).
struct StaticState
{
    /// Per node, in pre-order: the derived hint and the user's override
    /// (iris::StaticOverride, as its underlying integer so this header does not
    /// have to drag scenenode.h in).
    QVector<bool> hints;
    QVector<quint8> overrides;

    bool isEmpty() const { return hints.isEmpty(); }
};

/// The subtree's SCENE_STATIC classification, right now.
StaticState captureStatic(const iris::SceneNodePtr &node);

/// Puts it back. A no-op for an empty state, and tolerant of a subtree that has
/// changed shape since the capture (it restores as far as the two walks agree)
/// — an undo must never be the thing that throws.
void restoreStatic(const iris::SceneNodePtr &node, const StaticState &state);

} // namespace structuralundo

#endif // STATICSTATE_H
