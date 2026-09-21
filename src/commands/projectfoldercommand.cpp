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

namespace {
/// A command that did nothing marks itself obsolete INSIDE redo(), which is
/// where QUndoStack::push looks: it then deletes the command instead of
/// keeping a step that undoes nothing. Callers judge with the model first
/// (services/projectfolders.h `judgeCreate`/`judgeMove`), so reaching this is
/// a race with another writer, not the ordinary path.
void report(const FolderOutcomePtr &outcome, const QString &error, int moved)
{
    if (!outcome) return;
    outcome->error = error;
    outcome->moved = moved;
}
}   // namespace

CreateProjectFolderCommand::CreateProjectFolderCommand(Database *db, const QString &projectGuid,
                                                       const QString &name, const QString &parent,
                                                       FolderOutcomePtr outcome)
    : QUndoCommand(QObject::tr("New folder")),
      mDb(db), mProjectGuid(projectGuid), mName(name.trimmed()), mParent(parent),
      mGuid(GUIDManager::generateGUID()), mOutcome(std::move(outcome))
{
    if (mOutcome) mOutcome->guid = mGuid;
}

void CreateProjectFolderCommand::redo()
{
    const projectfolders::Result result =
        projectfolders::create(mDb, mProjectGuid, mName, mParent, mGuid);
    mCreated = result.ok;
    report(mOutcome, result.error, result.ok ? 1 : 0);
    if (!result.ok) setObsolete(true);
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
                                                       const QString &folderGuid,
                                                       FolderOutcomePtr outcome)
    : QUndoCommand(QObject::tr("Move to folder")),
      mDb(db), mProjectGuid(projectGuid), mGuids(guids), mFolderGuid(folderGuid),
      mOutcome(std::move(outcome))
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
    report(mOutcome, result.error, result.moved);
    // A move that moved NOTHING is not a step: the rows were already there, or
    // somebody else took them somewhere else between the judgement and here.
    if (!result.ok || result.moved == 0) setObsolete(true);
}

void MoveToProjectFolderCommand::undo()
{
    if (!mCaptured) return;
    bool any = false;
    for (auto it = mWas.constBegin(); it != mWas.constEnd(); ++it)
        any = projectfolders::file(mDb, mProjectGuid, it.key(), it.value()) || any;
    if (any) projectfolders::announce(mProjectGuid);
}
