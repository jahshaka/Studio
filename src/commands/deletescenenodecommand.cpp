/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/deletescenenodecommand.h"
#include "data/database/database.h"
#include "shell/uimanager.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "shell/mainwindow.h"
#include "ui/panels/scenehierarchywidget.h"

DeleteSceneNodeCommand::DeleteSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode,
                                               Database *db, const QString &assetGuid)
{
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
    this->position = parentNode->children.indexOf(sceneNode);
    this->db = db;
    this->assetGuid = assetGuid;
}

DeleteSceneNodeCommand::~DeleteSceneNodeCommand()
{
    // The delete became permanent (no undo can reach it any more): now the
    // asset row can go too.
    if (nodeDeleted && db && !assetGuid.isEmpty())
        db->deleteAsset(assetGuid);
}

// The UiManager statics are null-checked (like ReparentSceneNodeCommand, the
// headless-safe template): scripts push these commands with no UI docks built.
void DeleteSceneNodeCommand::undo()
{
    nodeDeleted = false;
    parentNode->insertChild(position, sceneNode, false);
    if (UiManager::sceneHierarchyWidget) UiManager::sceneHierarchyWidget->insertChild(sceneNode);
    if (UiManager::mainWindow) UiManager::mainWindow->sceneNodeSelected(sceneNode);
}

void DeleteSceneNodeCommand::redo()
{
    nodeDeleted = true;
    if (UiManager::sceneHierarchyWidget) UiManager::sceneHierarchyWidget->removeChild(sceneNode);
    sceneNode->removeFromParent();// important that this is done after!
    if (UiManager::mainWindow) UiManager::mainWindow->sceneNodeSelected(iris::SceneNodePtr());
}
