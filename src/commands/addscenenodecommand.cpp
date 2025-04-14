/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "addscenenodecommand.h"
#include "../uimanager.h"
#include "../irisgl/src/scenegraph/scenenode.h"
#include "../mainwindow.h"
#include "../widgets/scenehierarchywidget.h"


AddSceneNodeCommand::AddSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
}

void AddSceneNodeCommand::undo()
{
    sceneNode->removeFromParent();
    UiManager::sceneHierarchyWidget->removeChild(sceneNode);
    UiManager::mainWindow->sceneNodeSelected(iris::SceneNodePtr());
}

void AddSceneNodeCommand::redo()
{
    parentNode->addChild(sceneNode, false);
    UiManager::sceneHierarchyWidget->insertChild(sceneNode);
    UiManager::mainWindow->sceneNodeSelected(sceneNode);
}
