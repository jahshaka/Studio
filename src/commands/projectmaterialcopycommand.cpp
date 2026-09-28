/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/


#include "commands/projectmaterialcopycommand.h"

#include <QJsonDocument>
#include <QObject>
#include <QSqlDatabase>

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "services/assetdelete.h"
#include "services/materialbundle.h"
#include "services/materialmembers.h"
#include "services/presetedit.h"
#include "services/projectassets.h"

ProjectMaterialCopyCommand::ProjectMaterialCopyCommand(Database *db, Project *project,
                                                       const QString &source, bool pristine)
    : QUndoCommand(pristine ? QObject::tr("Add material from library")
                            : QObject::tr("Duplicate material")),
      mDb(db), mProject(project),
      mProjectGuid(project ? project->getProjectGuid() : QString()), mSource(source)
{
    if (!mDb || mSource.isEmpty()) { mError = QObject::tr("no material"); return; }
    if (mProjectGuid.isEmpty()) { mError = QObject::tr("no project is open"); return; }
    const AssetRecord row = mDb->fetchAsset(mSource);
    const QString sourceName = row.name.isEmpty() ? MaterialBundle::shippedPresetName(mSource)
                                                  : row.name;
    mMaster = presetedit::masterOf(mDb, mSource);
    mName = materialmembers::projectCopyName(mDb, mProjectGuid, sourceName);
    mDefinition = materialmembers::copyDefinition(mDb, mProject, mSource, pristine);
    mThumbnail = row.thumbnail;
    if (!mName.isEmpty() && !mDefinition.isEmpty()) {
        mDefinition[QStringLiteral("name")] = mName;
        if (mDefinition.contains(QStringLiteral("shadergraph"))) {
            QJsonObject graph = mDefinition.value(QStringLiteral("shadergraph")).toObject();
            QJsonObject settings = graph.value(QStringLiteral("settings")).toObject();
            settings[QStringLiteral("name")] = mName;
            graph[QStringLiteral("settings")] = settings;
            mDefinition[QStringLiteral("shadergraph")] = graph;
        }
    }
    if (mDefinition.isEmpty()) mError = QObject::tr("'%1' has no definition to copy").arg(sourceName);
}

void ProjectMaterialCopyCommand::redo()
{
    mError.clear();
    if (!mDb || mSource.isEmpty() || mProjectGuid.isEmpty()) {
        mError = QObject::tr("no project is open");
        return;
    }
    if (mDefinition.isEmpty()) {
        mError = QObject::tr("'%1' has no definition to copy").arg(mSource);
        return;
    }

    QSqlDatabase conn = QSqlDatabase::database();
    DbTransaction tx(conn);
    const QString mintedBefore = mCopyGuid;
    const QString guid = mCopyGuid.isEmpty() ? GUIDManager::generateGUID() : mCopyGuid;
    QString error;
    // The project-owned mint with a caller-supplied guid (the redo contract).
    const QString made = MaterialBundle::createPresetCopy(mDb, guid, mProjectGuid, mName,
                                                          mDefinition, mThumbnail, &error);
    if (made.isEmpty()) {
        mError = error.isEmpty() ? QObject::tr("the catalog refused the copy") : error;
        return;
    }
    mCopyGuid = made;
    if (!mMaster.isEmpty()) {
        QJsonObject props = QJsonDocument::fromJson(mDb->fetchAsset(mCopyGuid).properties).object();
        props.insert(MaterialBundle::kPresetMasterKey, mMaster);
        mDb->updateAssetProperties(mCopyGuid, QJsonDocument(props).toJson());
    }
    ProjectAssets::addToProject(mCopyGuid, mDb, mProject, ProjectAssets::AddKind::Direct);
    if (!tx.commit()) {
        mError = QObject::tr("the catalog refused the copy");
        mCopyGuid = mintedBefore;
    }
}

void ProjectMaterialCopyCommand::undo()
{
    if (!mDb || mCopyGuid.isEmpty() || mProjectGuid.isEmpty()) return;
    QSqlDatabase conn = QSqlDatabase::database();
    DbTransaction tx(conn);
    assetdelete::removeFromProject(mDb, mCopyGuid, mProjectGuid);
    assetdelete::remove(mDb, mCopyGuid, /*keepShared*/ true, /*force*/ true);
    tx.commit();
}
