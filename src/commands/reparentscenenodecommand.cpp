/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/reparentscenenodecommand.h"

#include "commands/structuralundo.h"
#include "services/services.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"

ReparentSceneNodeCommand::ReparentSceneNodeCommand(iris::SceneNodePtr sceneNode,
                                                   iris::SceneNodePtr newParent)
{
    this->sceneNode = sceneNode;
    this->oldParent = sceneNode ? sceneNode->getParent() : iris::SceneNodePtr();
    this->newParent = newParent;
    // Captured BEFORE anything moves — this is the only moment the old slot
    // exists (audit F5).
    this->oldIndex = sceneNode ? sceneNode->siblingIndex() : -1;
    setText(QObject::tr("Reparent %1").arg(sceneNode ? sceneNode->getName() : QString()));
}

void ReparentSceneNodeCommand::undo()
{
    if (!sceneNode || !oldParent) return;
    // Where the redo left it, so a second redo lands in the same slot rather
    // than appending. Learned here rather than at the end of redo() because a
    // sibling may have moved in between.
    const int actual = sceneNode->siblingIndex();
    if (actual >= 0) newIndex = actual;
    if (staticAtNew.isEmpty()) staticAtNew = structuralundo::captureStatic(sceneNode);
    // keepTransform: world pose preserved, exactly as the redo does.
    oldParent->insertChild(oldIndex, sceneNode, true);
    structuralundo::restoreStatic(sceneNode, staticAtOld);
    if (services && services->sceneEdit) services->sceneEdit->notifyHierarchyChanged();
    if (services && services->selection) services->selection->select(sceneNode);
}

void ReparentSceneNodeCommand::redo()
{
    if (!sceneNode || !newParent) return;
    if (staticAtOld.isEmpty()) staticAtOld = structuralundo::captureStatic(sceneNode);
    // -1 on the FIRST redo (a drop appends, which is what the user saw);
    // the remembered slot on every redo after an undo.
    newParent->insertChild(newIndex, sceneNode, true);
    structuralundo::restoreStatic(sceneNode, staticAtNew);
    if (services && services->sceneEdit) services->sceneEdit->notifyHierarchyChanged();
    if (services && services->selection) services->selection->select(sceneNode);
}
