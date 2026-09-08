/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/sunlightlinkcommand.h"

#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace {

bool rotationOf(const iris::ScenePtr &scene, const QString &guid, iris::Quat &out)
{
    if (!scene || guid.isEmpty()) return false;
    auto node = scene->nodes.value(guid);
    if (!node) return false;
    out = node->getGlobalRotation();
    return true;
}

}

SunLightLinkCommand::SunLightLinkCommand(const QString &text, const iris::ScenePtr &scene,
                                         const QString &newSunLightGuid)
    : mScene(scene), mAfter(newSunLightGuid)
{
    setText(text);
    if (!scene) return;
    mBefore = scene->sunLightGuid;
    // The incoming light's own rotation, as the user left it — what unlinking
    // (or undoing the link) has to give back.
    mHaveBeforeRot = rotationOf(scene, mAfter, mBeforeRot);
    // The outgoing light's rotation is wherever the sun last put it; redoing an
    // unlink must leave it exactly there, not re-derive it.
    mHaveOutgoingRot = rotationOf(scene, mBefore, mOutgoingRot);
}

void SunLightLinkCommand::restoreRotation(const QString &guid, const iris::Quat &rot)
{
    auto scene = mScene.lock();
    if (!scene || guid.isEmpty()) return;
    auto node = scene->nodes.value(guid);
    if (node) node->setGlobalRot(rot);
}

void SunLightLinkCommand::redo()
{
    auto scene = mScene.lock();
    if (!scene) return;
    scene->sunLightGuid = mAfter;
    // Unlinking: the light stays where the sun left it (no snap-back surprise).
    // Linking: applySunCoupling swings it on the next update, and calling it
    // here means a headless caller sees the result without stepping a frame.
    if (mAfter.isEmpty() && mHaveOutgoingRot) restoreRotation(mBefore, mOutgoingRot);
    scene->applySunCoupling();
}

void SunLightLinkCommand::undo()
{
    auto scene = mScene.lock();
    if (!scene) return;
    scene->sunLightGuid = mBefore;
    // Give the light that WAS being driven its manual rotation back — the half
    // of "unlinking restores manual control" that the guid alone cannot do.
    if (mHaveBeforeRot) restoreRotation(mAfter, mBeforeRot);
    scene->applySunCoupling();
}
