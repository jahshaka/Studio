/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/sunlink.h"

#include "commands/sunlightlinkcommand.h"
#include "services/services.h"
#include "services/undoservice.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"

namespace sunlink
{

QString setDriven(const iris::ScenePtr &scene, StudioServices *services, bool on)
{
    if (!scene) return QString();
    if (on) {
        // Nothing to steer is a refusal, not a silently-on switch: the caller
        // puts its checkbox back. (A scene with no directional light is normal
        // and is not an error — it simply has no sun for the sky to aim.)
        if (!scene->sunLight()) return QString();
    } else if (!scene->skyDrivesSun) {
        return QString();                       // already off
    }

    auto *cmd = new SunLightLinkCommand(on ? QStringLiteral("Sky Steers the Sun")
                                           : QStringLiteral("Sky Stops Steering the Sun"),
                                        scene, on);
    if (services && services->undo) services->undo->push(cmd);
    else { cmd->redo(); delete cmd; }

    if (!scene->skyDrivesSun) return QString();
    auto sun = scene->sunLight();
    return sun ? sun->getGUID() : QString();
}

}
