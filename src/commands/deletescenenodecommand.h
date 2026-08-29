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

#include <QUndoCommand>
#include "irisglfwd.h"

class MainWindow;
class SceneHierarchyWidget;
class Database;

class DeleteSceneNodeCommand : public QUndoCommand
{
    iris::SceneNodePtr parentNode;
    iris::SceneNodePtr sceneNode;
    int position;
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
