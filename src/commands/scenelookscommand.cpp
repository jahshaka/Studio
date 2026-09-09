/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/scenelookscommand.h"

#include "irisgl/document/scenegraph/scene.h"

SceneLooksCommand::SceneLooksCommand(const QString &text, const iris::ScenePtr &scene,
                                     const QJsonArray &before)
    : mScene(scene), mBefore(before)
{
    setText(text);
    if (scene) mAfter = scene->looks;
}

void SceneLooksCommand::apply(const QJsonArray &stack)
{
    auto scene = mScene.lock();
    if (!scene) return;
    scene->looks = stack;
    if (mRefresh) mRefresh();
}

void SceneLooksCommand::undo()
{
    apply(mBefore);
}

void SceneLooksCommand::redo()
{
    // push() replays redo() immediately on an edit the caller already applied.
    if (mFirstRedo) { mFirstRedo = false; return; }
    apply(mAfter);
}
