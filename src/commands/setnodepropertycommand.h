/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SETNODEPROPERTYCOMMAND_H
#define SETNODEPROPERTYCOMMAND_H

// SetNodePropertyCommand — one reflected node property write, undoable
// (AI_SURFACE_AUDIT F5).
//
// node.setProperty was a direct document write documented "not undoable yet",
// while every skill and every tool description promises that one script run is
// one undo step. It is also the ONLY path to light parameters and to all the
// particle-emitter scalars, so the promise was broken for most of what an
// assistant actually edits.
//
// The command is deliberately generic: it holds the key and the two QVariants
// and replays them through SceneNode::setPropertyValue — the same reflection
// entry point the write itself uses — so it covers every present and future
// property without a per-field command class. A property the node refuses on
// undo (it cannot happen for a value it just accepted) is simply not written.

#include <QString>
#include <QVariant>

#include "commands/staticstate.h"
#include "commands/studiocommand.h"
#include "irisgl/irisglfwd.h"

class SetNodePropertyCommand : public StudioCommand
{
public:
    SetNodePropertyCommand(iris::SceneNodePtr node, const QString &key,
                           const QVariant &oldValue, const QVariant &newValue);

    void undo() override;
    void redo() override;

private:
    void apply(const QVariant &value);
    bool movesTheNode() const;

    iris::SceneNodePtr sceneNode;
    QString propertyKey;
    QVariant oldValue;
    QVariant newValue;
    /// position/rotation/scale are reflected properties, so this command is a
    /// transform write too — and a transform write DEMOTES a static subtree
    /// (SCENEGRAPH_SPEC §6 rule 4). Same capture/restore pair as
    /// TransformSceneNodeCommand, for the same reason (scripting audit F3).
    /// Both stay empty for the overwhelming majority of properties, which move
    /// nothing.
    structuralundo::StaticState staticBefore;
    structuralundo::StaticState staticAfter;
};

#endif // SETNODEPROPERTYCOMMAND_H
