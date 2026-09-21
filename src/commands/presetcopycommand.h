/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PRESETCOPYCOMMAND_H
#define PRESETCOPYCOMMAND_H

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QUndoCommand>
#include <functional>

class Database;
class Project;

// THE FIRST EDIT OF A PRESET, AS ONE UNDO STEP (PRESET-EDIT-1; the mechanism
// is described in services/presetedit.h).
//
// Four things happen together or not at all, which is why they are one
// command rather than four service calls around an edit:
//
//   1. the COPY — a library material bundle carrying the preset's definition,
//      its members and its NAME (`MaterialBundle::createPresetCopy`);
//   2. the PIN MOVE — the project pins the copy and lets go of the master
//      (the library row and every other project are untouched);
//   3. the USE EDGES — every node that wore the master now wears the copy, so
//      the tray, the closure walkers and "used by" all answer about the thing
//      the scene is actually wearing;
//   4. the RE-DRESS — the meshes take the copy's material (the same picture,
//      by construction: the definition is the master's until the edit that
//      follows lands).
//
// THE GUID SURVIVES AN UNDO. A redo must re-make the copy EXACTLY — same
// guid — or the document, the pins and the edges would point at a material
// that no longer exists. The guid is minted on the first redo and kept; every
// later redo re-creates the row under it from the definition this command
// holds.
//
// WHAT AN UNDO DOES NOT DO: touch the master's row or its bytes (it was never
// changed), and take back the SEED of the preset bundle (seeding is an
// import, and imports are not undoable — the house rule).
class PresetCopyCommand : public QUndoCommand
{
public:
    /// `master` must be a seeded shipped preset and `project` must be open;
    /// `presetedit::forEdit` is the one caller and asks both questions first.
    /// `redress` re-dresses the meshes wearing a material (the caller's
    /// SceneEditService — a CALLBACK so this command knows nothing about the
    /// scene layer, which is also what lets it be tested against a database
    /// alone). It may be empty.
    using Redress = std::function<void(const QString &materialGuid)>;
    PresetCopyCommand(Database *db, Project *project, Redress redress,
                      const QString &master);

    void undo() override;
    void redo() override;

    /// The copy's guid — valid after the first redo, empty when the mint
    /// failed (`error()` says why).
    QString copyGuid() const { return mCopyGuid; }
    QString error() const { return mError; }

private:
    void moveUseEdges(const QString &from, const QString &to);

    Database *mDb = nullptr;
    Project  *mProject = nullptr;
    Redress   mRedress;

    QString     mMaster;
    QString     mProjectGuid;     ///< captured at construction: the project may close
    QString     mName;            ///< the preset's name, which the copy keeps
    QJsonObject mDefinition;      ///< the master's definition, read once
    QByteArray  mThumbnail;       ///< the master's tile, until the copy is drawn

    QString     mCopyGuid;        ///< minted on the first redo, then kept
    QString     mError;
    bool        mMasterWasPinned = false;
    QStringList mMovedNodes;      ///< the nodes whose use edge this command moved
};

#endif // PRESETCOPYCOMMAND_H
