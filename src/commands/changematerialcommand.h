/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CHANGEMATERIALCOMMAND_H
#define CHANGEMATERIALCOMMAND_H

#include <QUndoCommand>
#include "irisgl/irisglfwd.h"

// Swaps a mesh node's whole material (preset/asset applies). Before this
// command existed, applying a preset replaced the material with no undo entry;
// with recursive applies (a preset dropped on a model root repaints every
// descendant mesh) a mis-click must be reversible.
class ChangeMaterialCommand : public QUndoCommand
{
    iris::MeshNodePtr meshNode;
    iris::MaterialPtr oldMaterial;
    iris::MaterialPtr newMaterial;

public:
    ChangeMaterialCommand(iris::MeshNodePtr meshNode, iris::MaterialPtr newMaterial);

    void undo() override;
    void redo() override;
};

#endif // CHANGEMATERIALCOMMAND_H
