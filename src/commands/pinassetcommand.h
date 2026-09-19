/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PINASSETCOMMAND_H
#define PINASSETCOMMAND_H

#include <QString>
#include <QUndoCommand>

class Database;
class Project;

// AN APPLY THAT PINS IS AN APPLY THAT UNPINS ON UNDO
// (MATERIAL_BUNDLE_SPEC §4, the materials audit's F5).
//
// Applying a library material to a mesh makes the project USE it, so the
// project pins it — that is the reference-with-pin law, and it is what makes
// the project render the version it was built with. It was a bare service
// call beside the undo macro, so undoing the apply left the pin behind: the
// material stayed in the project's tray and its textures stayed pinned,
// naming a version of a scene that no longer existed. With presets that was
// visible as rubbish accumulating — every apply left something the user
// could not take back.
//
// This is the pin as a COMMAND, pushed inside the apply's macro, so one
// Ctrl+Z takes back the document edit AND the membership it created. Two
// rules keep it honest:
//
//   * IT ONLY TAKES BACK WHAT IT MADE. A material the project already pinned
//     (the user added it deliberately, or an earlier apply pinned it) is left
//     alone by the undo — the command records whether IT created the pin.
//   * IT DOES NOT TOUCH THE LIBRARY. The undo is `removeFromProject`: the
//     project's pin on the asset and on the members only it uses. The row,
//     the bytes and every other project are untouched, which is the house
//     law for "remove from project" (services/assetdelete.h).
//
// The house rule that IMPORTS are not undoable is unchanged: a texture
// imported by a picker stays in the library after an undo and surfaces under
// Clean unused. A PIN is not an import — it is project membership the apply
// created, and it is the one thing the apply can hand back.
class PinAssetCommand : public QUndoCommand
{
public:
    PinAssetCommand(Database *db, Project *project, const QString &assetGuid);

    void undo() override;
    void redo() override;

private:
    Database *mDb = nullptr;
    Project  *mProject = nullptr;
    QString   mAssetGuid;
    QString   mProjectGuid;   ///< captured at construction: the project may close
    bool      mPinnedByUs = false;
};

// THE OTHER HALF OF AN APPLY'S CATALOG WORK, and it is here for the same
// reason: it has to be undoable.
//
// "This mesh uses that material" is a dependency row the apply writes so the
// tray, the closure walkers and "used by N" can see it. It was written after
// the macro closed and nothing took it back, so an undone apply left the
// catalog saying a node wears a material it does not — which is the same
// class of leftover as the pin, one level down.
//
// The redo deletes any existing edge for this (node, material) pair and
// writes one; the undo deletes the edge it wrote and stops there. It does not
// restore an edge to a PREVIOUS material, and that is correct rather than
// lazy: the apply never removed one (an edge to another material survives an
// apply today), so "before this command" is exactly "without this edge".
class MaterialUseEdgeCommand : public QUndoCommand
{
public:
    MaterialUseEdgeCommand(Database *db, const QString &projectGuid,
                           const QString &nodeGuid, const QString &materialGuid);

    void undo() override;
    void redo() override;

private:
    Database *mDb = nullptr;
    QString   mProjectGuid;
    QString   mNodeGuid;
    QString   mMaterialGuid;
    bool      mWrote = false;
};

#endif // PINASSETCOMMAND_H
