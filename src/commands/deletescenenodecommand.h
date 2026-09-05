/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef DELETESCENENODECOMMAND_H
#define DELETESCENENODECOMMAND_H

#include "commands/structuralundo.h"
#include "commands/studiocommand.h"
#include "io/sceneformat.h"
#include "irisgl/irisglfwd.h"

class Database;

class DeleteSceneNodeCommand : public StudioCommand
{
    iris::SceneNodePtr parentNode;
    iris::SceneNodePtr sceneNode;
    int position;
    /// What was deleted, serialized (undo v1.5 — src/io/sceneformat.h and
    /// commands/structuralundo.h). Captured at PUSH time, before the node
    /// leaves the tree, so the command owns a description of the deletion that
    /// does not depend on the live objects surviving.
    SceneFragment snapshot;
    /// The subtree's SCENE_STATIC classification at the moment it was deleted,
    /// so undo gives back the scene that was there and not a re-derivation of
    /// it (scripting audit F3).
    structuralundo::StaticState staticState;
    // Deferred asset-row deletion (SCRIPTING_SPEC §1.2 node.remove): deleting the
    // DB row at push time made undo resurrect a node whose asset row was gone.
    // The row is deleted only when the command is destroyed while still in the
    // "deleted" state (stack cleared/truncated/destroyed) — undo keeps it alive.
    Database *db = nullptr;
    QString assetGuid;
    bool nodeDeleted = false;
public:
    DeleteSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode,
                           Database *db = nullptr, const QString &assetGuid = QString());
    ~DeleteSceneNodeCommand() override;

    void undo() override;
    void redo() override;
};


#endif // DELETESCENENODECOMMAND_H
