/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "reparentscenenodecommand.h"
#include "../uimanager.h"
#include "../mainwindow.h"
#include "../widgets/scenehierarchywidget.h"

ReparentSceneNodeCommand::ReparentSceneNodeCommand(iris::SceneNodePtr sceneNode,
                                                   iris::SceneNodePtr newParent)
{
    this->sceneNode = sceneNode;
    this->oldParent = sceneNode ? sceneNode->parent : iris::SceneNodePtr();
    this->newParent = newParent;
    setText(QObject::tr("Reparent %1").arg(sceneNode ? sceneNode->getName() : QString()));
}

void ReparentSceneNodeCommand::undo()
{
    if (!sceneNode || !oldParent) return;
    oldParent->addChild(sceneNode, true);      // keepTransform: world pose preserved
    if (UiManager::sceneHierarchyWidget) UiManager::sceneHierarchyWidget->repopulateTree();
    if (UiManager::mainWindow) UiManager::mainWindow->sceneNodeSelected(sceneNode);
}

void ReparentSceneNodeCommand::redo()
{
    if (!sceneNode || !newParent) return;
    newParent->addChild(sceneNode, true);
    if (UiManager::sceneHierarchyWidget) UiManager::sceneHierarchyWidget->repopulateTree();
    if (UiManager::mainWindow) UiManager::mainWindow->sceneNodeSelected(sceneNode);
}
