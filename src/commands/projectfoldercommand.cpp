/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/projectfoldercommand.h"

#include <QObject>

#include "data/guidmanager.h"
#include "services/projectfolders.h"

CreateProjectFolderCommand::CreateProjectFolderCommand(Database *db, const QString &projectGuid,
                                                       const QString &name, const QString &parent)
    : QUndoCommand(QObject::tr("New folder")),
      mDb(db), mProjectGuid(projectGuid), mName(name.trimmed()), mParent(parent),
      mGuid(GUIDManager::generateGUID())
{
}

void CreateProjectFolderCommand::redo()
{
    const projectfolders::Result result =
        projectfolders::create(mDb, mProjectGuid, mName, mParent, mGuid);
    mCreated = result.ok;
    mError = result.error;
}

void CreateProjectFolderCommand::undo()
{
    if (!mCreated) return;
    // Anything filed into it in the meantime moves up rather than leaving the
    // project: an undo of "New Folder" is about the folder, never its contents.
    projectfolders::remove(mDb, mProjectGuid, mGuid, true);
    mCreated = false;
}

MoveToProjectFolderCommand::MoveToProjectFolderCommand(Database *db, const QString &projectGuid,
                                                       const QStringList &guids,
                                                       const QString &folderGuid)
    : QUndoCommand(QObject::tr("Move to folder")),
      mDb(db), mProjectGuid(projectGuid), mGuids(guids), mFolderGuid(folderGuid)
{
}

void MoveToProjectFolderCommand::redo()
{
    // WHERE EACH ROW WAS, read BEFORE the first move and kept: a redo files
    // them forward again from wherever the undo put them, which is the same
    // set, so the capture is taken once.
    if (!mCaptured) {
        for (const QString &guid : mGuids)
            mWas.insert(guid, projectfolders::folderOf(mDb, mProjectGuid, guid));
        mCaptured = true;
    }
    const projectfolders::Result result =
        projectfolders::moveTo(mDb, mProjectGuid, mGuids, mFolderGuid);
    mMoved = result.moved;
    mError = result.error;
}

void MoveToProjectFolderCommand::undo()
{
    if (!mCaptured) return;
    bool any = false;
    for (auto it = mWas.constBegin(); it != mWas.constEnd(); ++it)
        any = projectfolders::file(mDb, mProjectGuid, it.key(), it.value()) || any;
    if (any) projectfolders::announce(mProjectGuid);
}
