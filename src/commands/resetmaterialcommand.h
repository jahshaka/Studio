/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef RESETMATERIALCOMMAND_H
#define RESETMATERIALCOMMAND_H

#include <QStringList>
#include <QUndoCommand>
#include <QVector>

#include "data/project.h"
#include "irisgl/irisglfwd.h"

class Database;

/// ONE UNDO STEP for "reset the node's material to its own default" (owner,
/// 2026-09-12 — services/materialdefaults.h).
///
/// Redo puts the node's default material back and clears what the node USED:
/// its dependency edges to Material, Shader and Texture assets (an applied
/// material asset, a graph material picked in the panel, textures bound to its
/// slots) are dropped — edges of any other kind are never touched — and the
/// edges to the default's own textures are ensured. Undo puts the user's
/// material back, re-creates exactly the edges redo dropped and deletes the
/// default-texture edges redo CREATED (not the ones that were already there).
///
/// PROJECT MEMBERSHIP IS NOT TOUCHED (lead decision, 2026-09-12): an applied
/// material stays pinned in the project, exactly as when the material on any
/// other mesh is replaced. The ONE membership effect is the default's own:
/// building it can pin the floor's checker when the project had no pin on it
/// (ShippedAssets::pinTexture). Those pins (`newlyPinned`) are the command's —
/// undo takes them back (the pin row only), redo puts them back.
class ResetMaterialCommand : public QUndoCommand
{
public:
    ResetMaterialCommand(Database *db, Project *project, iris::MeshNodePtr meshNode,
                         iris::MaterialPtr defaultMaterial, const QStringList &defaultTextures,
                         const QStringList &newlyPinned);

    void redo() override;
    void undo() override;

private:
    Database *db;
    Project *project;
    iris::MeshNodePtr meshNode;
    iris::MaterialPtr oldMaterial;
    iris::MaterialPtr defaultMaterial;
    QStringList defaultTextures;          ///< what the default uses (the floor's checker)
    QStringList newlyPinned;              ///< of those, pinned by building the default
    bool newlyPinnedLive = true;          ///< the construction already pinned them

    QVector<DependencyRecord> droppedEdges;   ///< the node's edges redo removed
    QStringList createdDefaultEdges;          ///< default-texture edges redo created
};

#endif // RESETMATERIALCOMMAND_H
