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

namespace {

iris::LightNodePtr firstDirectional(const iris::SceneNodePtr &node)
{
    if (!node) return iris::LightNodePtr();
    auto light = node.dynamicCast<iris::LightNode>();
    if (light && light->lightType == iris::LightType::Directional) return light;
    const auto children = node->children();
    for (const auto &child : children)
        if (auto hit = firstDirectional(child)) return hit;
    return iris::LightNodePtr();
}

}

namespace sunlink
{

iris::LightNodePtr resolveTarget(const iris::ScenePtr &scene, const iris::SceneNodePtr &selected)
{
    if (!scene) return iris::LightNodePtr();
    // Selection first: "drive the SELECTED directional light" is what the row
    // says, and a scene with several suns has to be steerable.
    if (selected) {
        auto light = selected.dynamicCast<iris::LightNode>();
        if (light && light->lightType == iris::LightType::Directional) return light;
    }
    return firstDirectional(scene->getRootNode());
}

QString setDriven(const iris::ScenePtr &scene, StudioServices *services, bool on,
                  const iris::SceneNodePtr &selected)
{
    if (!scene) return QString();

    QString target;
    if (on) {
        auto light = resolveTarget(scene, selected);
        if (!light) return QString();          // nothing to drive: refuse
        target = light->getGUID();
        if (target == scene->sunLightGuid) return target;
    } else if (scene->sunLightGuid.isEmpty()) {
        return QString();                       // already off
    }

    auto *cmd = new SunLightLinkCommand(on ? QStringLiteral("Link Sun Light")
                                           : QStringLiteral("Unlink Sun Light"),
                                        scene, target);
    if (services && services->undo) services->undo->push(cmd);
    else { cmd->redo(); delete cmd; }
    return scene->sunLightGuid;
}

}
