/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/addscenenodecommand.h"

#include "irisgl/document/scenegraph/scenenode.h"
#include "services/services.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"

AddSceneNodeCommand::AddSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode)
{
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
}

// `services` is null-checked (the headless-safe contract): scripts push these
// commands with no UI wired, and the notifications simply have no listeners.
void AddSceneNodeCommand::undo()
{
    sceneNode->removeFromParent();
    if (services && services->sceneEdit) services->sceneEdit->notifyNodeRemoved(sceneNode);
    if (services && services->selection) services->selection->select(iris::SceneNodePtr());
}

void AddSceneNodeCommand::redo()
{
    parentNode->addChild(sceneNode, false);
    // SCENE_STATIC (SCENEGRAPH_SPEC §6): a subtree that has just joined the
    // tree is at rest and its parent chain is known, which is the only moment
    // the default can be applied. THIS is the funnel every add/import goes
    // through, and it runs on redo too — an undone-then-redone add gets the
    // same classification as the original.
    sceneNode->applyStaticDefaults();
    if (services && services->sceneEdit) services->sceneEdit->notifyNodeInserted(sceneNode);
    if (services && services->selection) services->selection->select(sceneNode);
}
