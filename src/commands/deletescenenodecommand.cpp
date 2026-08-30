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
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/services.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"

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

// `services` is null-checked (the headless-safe contract): scripts push these
// commands with no UI wired, and the notifications simply have no listeners.
void DeleteSceneNodeCommand::undo()
{
    nodeDeleted = false;
    parentNode->insertChild(position, sceneNode, false);
    if (services && services->sceneEdit) services->sceneEdit->notifyNodeInserted(sceneNode);
    if (services && services->selection) services->selection->select(sceneNode);
}

void DeleteSceneNodeCommand::redo()
{
    nodeDeleted = true;
    if (services && services->sceneEdit) services->sceneEdit->notifyNodeRemoved(sceneNode);
    sceneNode->removeFromParent();// important that this is done after!
    if (services && services->selection) services->selection->select(iris::SceneNodePtr());
}
