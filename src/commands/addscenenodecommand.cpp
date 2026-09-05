/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/addscenenodecommand.h"

#include "commands/structuralundo.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/services.h"
#include "services/sceneeditservice.h"
#include "services/selectionservice.h"

AddSceneNodeCommand::AddSceneNodeCommand(iris::SceneNodePtr parentNode, iris::SceneNodePtr sceneNode,
                                         int position)
{
    this->parentNode = parentNode;
    this->sceneNode = sceneNode;
    this->position = position;
}

// `services` is null-checked (the headless-safe contract): scripts push these
// commands with no UI wired, and the notifications simply have no listeners.
void AddSceneNodeCommand::undo()
{
    // The slot is captured on the way OUT, not on the way in: an add whose
    // caller said "append" only learns which index that turned out to be once
    // the node is actually in the tree, and anything that happened since (a
    // sibling deleted, a reparent) may have moved it. Redo has to put it back
    // where it IS, not where it was asked to go.
    if (sceneNode) {
        const int actual = sceneNode->siblingIndex();
        if (actual >= 0) position = actual;
        if (services && services->sceneEdit && snapshot.isNull())
            snapshot = services->sceneEdit->captureFragment(sceneNode);
        staticState = structuralundo::captureStatic(sceneNode);
    }
    sceneNode->removeFromParent();
    if (services && services->sceneEdit) services->sceneEdit->notifyNodeRemoved(sceneNode);
    if (services && services->selection) services->selection->select(iris::SceneNodePtr());
}

void AddSceneNodeCommand::redo()
{
    // structuralundo::reinstate, not addChild: it honours the sibling index and
    // falls back to rebuilding the snapshot if the live node has become
    // unusable (commands/structuralundo.h). On the FIRST redo there is no
    // snapshot yet and the live node is the only copy — which is correct, that
    // is the node the caller just built.
    auto restored = structuralundo::reinstate(services, parentNode, sceneNode, snapshot, position);
    if (!restored) return;
    sceneNode = restored;
    // SCENE_STATIC (SCENEGRAPH_SPEC §6): a subtree that has just joined the
    // tree is at rest and its parent chain is known, which is the only moment
    // the default can be applied. THIS is the funnel every add/import goes
    // through, and it runs on redo too — an undone-then-redone add gets the
    // same classification as the original — or, once an undo has recorded what
    // the subtree actually had, exactly that (scripting audit F3).
    if (staticState.isEmpty()) sceneNode->applyStaticDefaults();
    else structuralundo::restoreStatic(sceneNode, staticState);
    if (services && services->sceneEdit) services->sceneEdit->notifyNodeInserted(sceneNode);
    if (services && services->selection) services->selection->select(sceneNode);
}
