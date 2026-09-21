/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROJECTFOLDERCOMMAND_H
#define PROJECTFOLDERCOMMAND_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QUndoCommand>

class Database;

// ORGANISING THE DRAWER IS UNDOABLE (DRAWERS-1).
//
// The house rule is that ASSET mutations are not undoable (SCRIPTING_SPEC
// §1.6.5) — an import, a rename, a delete are permanent, because they are
// changes to the library the document does not own. Filing is not one of
// those: a folder and a move change WHERE a row is listed in this project and
// nothing else — no pin moves, no bytes change, no other project sees it — and
// a drag of a dozen tiles onto the wrong folder is exactly the gesture a user
// expects one Ctrl+Z to take back. So New Folder and a move are commands, and
// a move of a MULTI-SELECTION is ONE command carrying every row: one gesture,
// one step.
//
// Rename and Delete stay outside the stack, and that is deliberate rather than
// lazy: a delete without `keepContents` takes rows out of the project through
// `assetdelete::removeFromProject`, which is the same permanent door the
// tray's own Delete uses.

/// New Folder. The guid is minted once, at construction, so a redo re-creates
/// the SAME folder — anything filed into it between the undo and the redo
/// therefore still points at something real.
class CreateProjectFolderCommand : public QUndoCommand
{
public:
    CreateProjectFolderCommand(Database *db, const QString &projectGuid,
                               const QString &name, const QString &parent);

    void undo() override;
    void redo() override;

    /// The folder's guid (minted at construction, valid before the push).
    QString guid() const { return mGuid; }
    /// Empty when the create succeeded.
    QString error() const { return mError; }

private:
    Database *mDb = nullptr;
    QString   mProjectGuid;
    QString   mName;
    QString   mParent;
    QString   mGuid;
    QString   mError;
    bool      mCreated = false;
};

/// A move of one or many rows into one folder (empty = the project root).
class MoveToProjectFolderCommand : public QUndoCommand
{
public:
    MoveToProjectFolderCommand(Database *db, const QString &projectGuid,
                               const QStringList &guids, const QString &folderGuid);

    void undo() override;
    void redo() override;

    /// How many rows the last redo re-filed.
    int moved() const { return mMoved; }
    /// Empty when the move was accepted (every row is judged before any moves).
    QString error() const { return mError; }

private:
    Database *mDb = nullptr;
    QString   mProjectGuid;
    QStringList mGuids;
    QString   mFolderGuid;
    QHash<QString, QString> mWas;   ///< guid -> the folder it was filed in
    int       mMoved = 0;
    QString   mError;
    bool      mCaptured = false;
};

#endif   // PROJECTFOLDERCOMMAND_H
