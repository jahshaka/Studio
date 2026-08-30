/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/addscenenodecommand.h"
#include "shell/uimanager.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "shell/mainwindow.h"
#include "ui/panels/scenehierarchywidget.h"


AddSceneNodeCommand::AddSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
}

// The UiManager statics are null-checked (like ReparentSceneNodeCommand, the
// headless-safe template): scripts push these commands with no UI docks built.
void AddSceneNodeCommand::undo()
{
    sceneNode->removeFromParent();
    if (UiManager::sceneHierarchyWidget) UiManager::sceneHierarchyWidget->removeChild(sceneNode);
    if (UiManager::mainWindow) UiManager::mainWindow->sceneNodeSelected(iris::SceneNodePtr());
}

void AddSceneNodeCommand::redo()
{
    parentNode->addChild(sceneNode, false);
    if (UiManager::sceneHierarchyWidget) UiManager::sceneHierarchyWidget->insertChild(sceneNode);
    if (UiManager::mainWindow) UiManager::mainWindow->sceneNodeSelected(sceneNode);
}
