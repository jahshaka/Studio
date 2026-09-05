/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ADDSCENENODECOMMAND_H
#define ADDSCENENODECOMMAND_H

#include "commands/structuralundo.h"
#include "commands/studiocommand.h"
#include "io/sceneformat.h"
#include "irisgl/irisglfwd.h"


/// Parents a node (every add-verb, every duplicate, every paste ends here).
///
/// UNDO v1.5: the command remembers the DOCUMENT SIBLING INDEX it put the node
/// at, so an undo/redo cycle puts it back in the same place instead of
/// appending it to the end of its parent's children — which matters most for
/// duplicate, whose whole point is landing beside its original.
class AddSceneNodeCommand : public StudioCommand
{
    //iris::ScenePtr scene;
    iris::SceneNodePtr parentNode;
    iris::SceneNodePtr sceneNode;
    /// Where under `parentNode` the node goes. -1 = append, which is what every
    /// plain add wants; duplicate and paste name a slot.
    int position = -1;
    /// What was added, serialized (src/io/sceneformat.h). Captured on the FIRST
    /// redo — before that the node has no parent chain and no sibling index to
    /// record. It is the record a redo falls back to when the live node is no
    /// longer usable; see commands/structuralundo.h for why the live object is
    /// tried first.
    SceneFragment snapshot;
    /// The subtree's SCENE_STATIC classification when it was removed, so redo
    /// restores what it had rather than re-deriving the greedy default
    /// (scripting audit F3). Empty on the first redo — there is nothing to
    /// restore yet, and applyStaticDefaults is exactly right for a brand new
    /// subtree.
    structuralundo::StaticState staticState;
public:
    AddSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode,
                        int position = -1);

    void undo() override;
    void redo() override;
};

#endif // ADDSCENENODECOMMAND_H
