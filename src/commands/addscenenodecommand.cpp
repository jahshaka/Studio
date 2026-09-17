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
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/scenegraph/scene.h"
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
    // THE FIRST CAMERA ADDED TAKES THE SHOT (PLAYER-SPAWN-1 rule 2, owner
    // 2026-09-17: "unless a camera node is present"). The rule itself is the
    // document's — Scene::armCameraIfNoneActive, which refuses when anything is
    // already armed — and it is applied HERE, on the one funnel every add,
    // duplicate and paste of a node goes through, for two reasons:
    //
    //   * it is the ADD half of the rule and nothing else: a project LOAD
    //     builds its graph through the reader, which never pushes a command,
    //     so a saved file's silence about the active camera keeps meaning the
    //     free viewer;
    //   * undo and redo stay symmetric. Undoing the add removes the node, and
    //     Scene::removeNode clears the choice when the removed camera was the
    //     armed one; redoing it arms the same camera again, by the same rule,
    //     rather than leaving a scene with one camera and no shot.
    if (sceneNode->sceneNodeType == iris::SceneNodeType::Camera)
        if (auto scene = sceneNode->getScene())
            scene->armCameraIfNoneActive(sceneNode.staticCast<iris::CameraNode>());
    if (services && services->sceneEdit) services->sceneEdit->notifyNodeInserted(sceneNode);
    if (services && services->selection) services->selection->select(sceneNode);
}
