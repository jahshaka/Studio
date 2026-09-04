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

#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"
#include "irisgl/document/scenegraph/scenenode.h"

/// Moves a node under a new parent (the hierarchy panel's drag-and-drop),
/// keeping its WORLD pose — addChild(keepTransform = true) recomputes the local
/// transform against the new parent. Undo restores the original parent the same
/// way, so the node ends up exactly where it visually was throughout.
class ReparentSceneNodeCommand : public StudioCommand
{
    iris::SceneNodePtr sceneNode;
    iris::SceneNodePtr oldParent;
    iris::SceneNodePtr newParent;

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
