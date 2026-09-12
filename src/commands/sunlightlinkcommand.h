/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SUNLIGHTLINKCOMMAND_H
#define SUNLIGHTLINKCOMMAND_H

// One undo step for the two SUN decisions (SUN_AND_LIGHT_DEFAULTS Q1):
//   * WHICH directional light is the sun — `scene->sunLightGuid`, the author's
//     pin; empty means "automatic", i.e. the lowest Forward Shading Priority;
//   * whether the realistic sky's dials STEER it — `scene->skyDrivesSun`.
//
// They used to be ONE field (a non-empty guid meant both), which is why picking
// a sun and letting the sky aim it could not be told apart.
//
// Either edit can swing a light: turning the steering on, or pinning a
// different light while it is on, immediately re-aims the new sun through
// Scene::applySunCoupling. Undo therefore has to put BOTH halves back — the
// fields and the rotation the light had before it was driven — or "turn it off
// to get manual control back" would restore the control and leave the light
// pointing wherever the sun last put it.
//
// Snapshot, not inverse: the state is two guids, a bool and two quaternions.

#include <QString>

#include "commands/studiocommand.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/irisglfwd.h"

class SunLightLinkCommand : public StudioCommand
{
public:
    /// Construct BEFORE applying: it captures the current link and the current
    /// rotation of both the outgoing and the incoming light, then redo() makes
    /// the change. Push it on the stack (which calls redo()) rather than
    /// editing the scene yourself.
    /// The PIN: which directional light is the sun ("" = automatic). The sky's
    /// steering is left exactly as it is.
    SunLightLinkCommand(const QString &text, const iris::ScenePtr &scene,
                        const QString &newSunLightGuid);
    /// The STEERING: does the realistic sky aim the sun? The pin is untouched.
    SunLightLinkCommand(const QString &text, const iris::ScenePtr &scene, bool skyDrivesSun);

    void undo() override;
    void redo() override;

private:
    void restoreRotation(const QString &guid, const iris::Quat &rot);

    /// The half both constructors share.
    void capture(const iris::ScenePtr &scene);

    iris::SceneWPtr mScene;
    QString mBefore;
    QString mAfter;
    bool mDriveBefore = false;
    bool mDriveAfter = false;
    iris::Quat mBeforeRot;    ///< rotation of the light being linked, pre-link
    iris::Quat mOutgoingRot;  ///< rotation of the light being unlinked, as driven
    QString mIncoming;        ///< the light that becomes the sun
    QString mOutgoing;        ///< the light that was the sun
    bool mHaveBeforeRot = false;
    bool mHaveOutgoingRot = false;
};

#endif // SUNLIGHTLINKCOMMAND_H
