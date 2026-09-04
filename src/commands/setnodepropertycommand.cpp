/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/setnodepropertycommand.h"

#include "irisgl/document/scenegraph/scenenode.h"
#include "services/sceneeditservice.h"
#include "services/services.h"

SetNodePropertyCommand::SetNodePropertyCommand(iris::SceneNodePtr node, const QString &key,
                                               const QVariant &oldValue, const QVariant &newValue)
    : sceneNode(node), propertyKey(key), oldValue(oldValue), newValue(newValue)
{
    setText(QStringLiteral("set %1").arg(key));
}

// position/rotation/scale are reflected properties too, so a write through this
// command can move the node — the transform notification is what makes the
// gizmo and the transform panel follow. Raised ONLY for those three: a script
// setting two dozen particle scalars would otherwise fire two dozen viewport
// refreshes for edits no gizmo cares about. `services` is null in headless runs
// (the StudioCommand contract).
void SetNodePropertyCommand::apply(const QVariant &value)
{
    if (!sceneNode) return;
    sceneNode->setPropertyValue(propertyKey, value);
    const bool moved = propertyKey == QLatin1String("position")
                       || propertyKey == QLatin1String("rotation")
                       || propertyKey == QLatin1String("scale");
    if (moved && services && services->sceneEdit) services->sceneEdit->notifyTransformChanged();
}

void SetNodePropertyCommand::undo() { apply(oldValue); }

void SetNodePropertyCommand::redo() { apply(newValue); }
