/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef REPARENTSCENENODECOMMAND_H
#define REPARENTSCENENODECOMMAND_H

#include "commands/structuralundo.h"
#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scenenode.h"

/// Moves a node under a new parent (the hierarchy panel's drag-and-drop),
/// keeping its WORLD pose — insertChild(keepTransform = true) recomputes the
/// local transform against the new parent. Undo restores the original parent
/// the same way, so the node ends up exactly where it visually was throughout.
///
/// AUDIT F5, CLOSED (undo v1.5). This command used to call `addChild` on both
/// sides, which APPENDS: undoing a reparent put the node back under its old
/// parent as the LAST child whatever position it had held, and redoing it put
/// it back under the new parent as the last child whatever position the first
/// redo had given it. Sibling order is real semantics — the outliner shows it
/// and the serializer writes it — so an undo that reorders the scene is a
/// silent edit the user did not make. Both indices are captured and both are
/// restored.
class ReparentSceneNodeCommand : public StudioCommand
{
    iris::SceneNodePtr sceneNode;
    iris::SceneNodePtr oldParent;
    iris::SceneNodePtr newParent;
    /// The DOCUMENT sibling index the node held under `oldParent`, captured at
    /// construction (before anything moves).
    int oldIndex = -1;
    /// The index it ends up at under `newParent`, learned on the first redo.
    /// -1 until then, which means "append" — the natural place for a drop.
    int newIndex = -1;
    /// The subtree's SCENE_STATIC classification on each side of the move.
    /// A reparent preserves the WORLD pose, which means it writes a transform,
    /// which demotes a static subtree (SCENEGRAPH_SPEC §6 rule 4) — and rule 2
    /// may legitimately change the answer anyway, since the new parent may not
    /// be static. Both sides are captured and both are restored, so an
    /// undo/redo cycle is a no-op for the classification too (audit F3).
    structuralundo::StaticState staticAtOld;
    structuralundo::StaticState staticAtNew;

public:
    ReparentSceneNodeCommand(iris::SceneNodePtr sceneNode, iris::SceneNodePtr newParent);

    void undo() override;
    void redo() override;

    /// True when making `node` a child of `target` would corrupt the graph:
    /// missing nodes, self-parenting, or `target` being `node` itself or one of
    /// its descendants (which would create a parent cycle — infinite recursion
    /// in getGlobalTransform). Header-only so document tests can assert on it.
    static bool wouldCreateCycle(const iris::SceneNodePtr &node,
                                 const iris::SceneNodePtr &target)
    {
        if (!node || !target) return true;
        for (iris::SceneNodePtr p = target; !!p; p = p->getParent())
            if (p == node) return true;
        return false;
    }
};

#endif // REPARENTSCENENODECOMMAND_H
