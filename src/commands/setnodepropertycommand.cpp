/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/setnodepropertycommand.h"

#include "commands/staticstate.h"
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

/// True for the three keys that are transform writes in disguise. Everything
/// else this command carries — a light's intensity, an emitter's speed — cannot
/// change a SCENE_STATIC classification, and must not pay a subtree walk.
bool SetNodePropertyCommand::movesTheNode() const
{
    return propertyKey == QLatin1String("position")
           || propertyKey == QLatin1String("rotation")
           || propertyKey == QLatin1String("scale");
}

void SetNodePropertyCommand::undo()
{
    if (movesTheNode() && staticAfter.isEmpty())
        staticAfter = structuralundo::captureStatic(sceneNode);
    apply(oldValue);
    if (movesTheNode()) structuralundo::restoreStatic(sceneNode, staticBefore);
}

void SetNodePropertyCommand::redo()
{
    if (movesTheNode() && staticBefore.isEmpty())
        staticBefore = structuralundo::captureStatic(sceneNode);
    apply(newValue);
    if (movesTheNode()) structuralundo::restoreStatic(sceneNode, staticAfter);
}
