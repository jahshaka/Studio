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

#include "irisgl/irisglfwd.h"

class Database;
class Project;

/// ONE UNDO STEP for "reset the node's material to its own default" (owner,
/// 2026-09-12 — services/materialdefaults.h). Redo puts the node's default
/// material back and CLEARS what the user had applied: the node's use edges to
/// an applied material asset (and to the textures its slots were bound to) go,
/// and an applied material nothing else in the project uses any more leaves
/// the project — its pin is released, so the tray stops listing it; the
/// LIBRARY row is never touched. Undo puts the user's material back and
/// restores exactly the edges and pins redo took away.
class ResetMaterialCommand : public QUndoCommand
{
public:
    ResetMaterialCommand(Database *db, Project *project, iris::MeshNodePtr meshNode,
                         iris::MaterialPtr defaultMaterial, const QStringList &defaultTextures);

    void redo() override;
    void undo() override;

private:
    struct Edge { int dependerType; int dependeeType; QString dependee; };

    Database *db;
    Project *project;
    iris::MeshNodePtr meshNode;
    iris::MaterialPtr oldMaterial;
    iris::MaterialPtr defaultMaterial;
    QStringList defaultTextures;   ///< what the default uses (the floor's checker)

    QList<Edge> droppedEdges;      ///< the node's edges redo removed
    QStringList releasedPins;      ///< applied assets redo took out of the project
};

#endif // RESETMATERIALCOMMAND_H
