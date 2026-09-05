/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/scenefoldercommand.h"

#include "irisgl/document/scenegraph/scene.h"
#include "ui/panels/scenehierarchywidget.h"

SceneFolderCommand::SceneFolderCommand(const QString &text, const iris::ScenePtr &scene,
                                       const scenefolders::Snapshot &before)
    : mScene(scene), mBefore(before)
{
    setText(text);
    mAfter = scenefolders::snapshot(scene);
}

void SceneFolderCommand::setPanel(SceneHierarchyWidget *panel)
{
    mPanel = panel;
}

void SceneFolderCommand::apply(const scenefolders::Snapshot &snap)
{
    auto scene = mScene.lock();
    if (!scene) return;
    scenefolders::restore(scene, snap);
    if (mPanel) mPanel->repopulateTree();
}

void SceneFolderCommand::undo()
{
    apply(mBefore);
}

void SceneFolderCommand::redo()
{
    // The first redo is QUndoStack::push replaying an edit the caller already
    // applied — restoring the same snapshot on top of itself is a no-op, but
    // skipping it also skips a panel rebuild the caller has usually already
    // done, so it is cheaper AND avoids rebuilding the tree twice per gesture.
    if (mFirstRedo) { mFirstRedo = false; return; }
    apply(mAfter);
}
