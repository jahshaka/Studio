/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "deletescenenodecommand.h"
#include "../uimanager.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../mainwindow.h"
#include "../widgets/scenehierarchywidget.h"

DeleteSceneNodeCommand::DeleteSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
    this->position = parentNode->children.indexOf(sceneNode);
}

void DeleteSceneNodeCommand::undo()
{
    parentNode->insertChild(position, sceneNode, false);
    UiManager::sceneHierarchyWidget->insertChild(sceneNode);
    UiManager::mainWindow->sceneNodeSelected(sceneNode);
}

void DeleteSceneNodeCommand::redo()
{
    UiManager::sceneHierarchyWidget->removeChild(sceneNode);
    sceneNode->removeFromParent();// important that this is done after!
    UiManager::mainWindow->sceneNodeSelected(iris::SceneNodePtr());
}
