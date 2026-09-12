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
#include "irisgl/document/scenegraph/lightnode.h"

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
    mDriveBefore = mDriveAfter = scene->skyDrivesSun;   // the pin edit leaves it alone
    capture(scene);
}

SunLightLinkCommand::SunLightLinkCommand(const QString &text, const iris::ScenePtr &scene,
                                         bool skyDrivesSun)
    : mScene(scene)
{
    setText(text);
    if (!scene) return;
    mAfter = scene->sunLightGuid;                       // the steering edit leaves it alone
    mDriveBefore = scene->skyDrivesSun;
    mDriveAfter = skyDrivesSun;
    capture(scene);
}

// The rotations both edits may disturb, captured through the ONE resolver: the
// light that IS the sun right now, and the light that will be the sun after.
// (Before the sun lane these were the two guids, which was the same thing only
// while the pin was the only way to have a sun.)
void SunLightLinkCommand::capture(const iris::ScenePtr &scene)
{
    mBefore = scene->sunLightGuid;
    auto outgoing = scene->sunLight();
    const QString outgoingGuid = outgoing ? outgoing->getGUID() : QString();
    // The incoming sun, resolved against the state this command is about to
    // write, without writing it: a pin names its light outright, and an unpin
    // falls back to the priority order.
    QString incomingGuid = mAfter;
    if (incomingGuid.isEmpty()) {
        const auto dirs = scene->directionalLights();
        if (!dirs.isEmpty()) incomingGuid = dirs.first()->getGUID();
    }
    // The incoming light's own rotation, as the user left it — what turning the
    // steering off (or undoing it) has to give back.
    mHaveBeforeRot = rotationOf(scene, incomingGuid, mBeforeRot);
    mIncoming = incomingGuid;
    // The outgoing light's rotation is wherever the sun last put it; redoing an
    // unlink must leave it exactly there, not re-derive it.
    mHaveOutgoingRot = rotationOf(scene, outgoingGuid, mOutgoingRot);
    mOutgoing = outgoingGuid;
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
    scene->skyDrivesSun = mDriveAfter;
    // Steering switched OFF: the light stays where the sun left it (no
    // snap-back surprise). Switched ON (or re-pinned while on):
    // applySunCoupling swings it, and calling it here means a headless caller
    // sees the result without stepping a frame.
    if (!mDriveAfter && mDriveBefore && mHaveOutgoingRot) restoreRotation(mOutgoing, mOutgoingRot);
    scene->applySunCoupling();
}

void SunLightLinkCommand::undo()
{
    auto scene = mScene.lock();
    if (!scene) return;
    scene->sunLightGuid = mBefore;
    scene->skyDrivesSun = mDriveBefore;
    // Give the light that WAS being driven its manual rotation back — the half
    // of "turning it off restores manual control" the fields alone cannot do.
    if (mHaveBeforeRot) restoreRotation(mIncoming, mBeforeRot);
    scene->applySunCoupling();
}
