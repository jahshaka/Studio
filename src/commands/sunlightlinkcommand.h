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

// One undo step for "the sky's sun drives this light" / "it doesn't any more"
// (VISUAL_PARITY re-audit F5).
//
// Linking is a two-part edit: the scene gains `sunLightGuid`, and the light it
// names is immediately swung onto the sun's direction by Scene::applySunCoupling.
// Undo therefore has to put BOTH back — the guid and the rotation the light had
// before it was driven — or "unlink restores manual control" would restore the
// control and leave the light pointing wherever the sun last put it.
//
// Snapshot, not inverse: the state is two guids and two quaternions.

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
    SunLightLinkCommand(const QString &text, const iris::ScenePtr &scene,
                        const QString &newSunLightGuid);

    void undo() override;
    void redo() override;

private:
    void restoreRotation(const QString &guid, const iris::Quat &rot);

    iris::SceneWPtr mScene;
    QString mBefore;
    QString mAfter;
    iris::Quat mBeforeRot;    ///< rotation of the light being linked, pre-link
    iris::Quat mOutgoingRot;  ///< rotation of the light being unlinked, as driven
    bool mHaveBeforeRot = false;
    bool mHaveOutgoingRot = false;
};

#endif // SUNLIGHTLINKCOMMAND_H
