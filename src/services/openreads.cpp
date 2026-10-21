/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/openreads.h"

#include <QJsonDocument>
#include <QSqlQuery>
#include <QUuid>

#include "data/project.h"
#include "io/scenereader.h"
#include "services/assetstorepaths.h"
#include "services/loadtimeline.h"

namespace openreads
{

QVector<ProjectAssets::SessionRow> Reads::sessionRows() const
{
    // The registration order: the legacy membership types first, then the pin-only ones
    // (ProjectAssets::sessionTypes) — the Object rows sit where that order puts them.
    QVector<ProjectAssets::SessionRow> out;
    out.reserve(objectRows.size() + otherRows.size());
    const int object = static_cast<int>(ModelTypes::Object);
    for (const int type : ProjectAssets::sessionTypes()) {
        const QVector<ProjectAssets::SessionRow> &from = type == object ? objectRows : otherRows;
        for (const ProjectAssets::SessionRow &row : from)
            if (row.type == type) out.append(row);
    }
    return out;
}

Reads prepare(Project *project)
{
    Reads reads;
    reads.projectGuid = project ? project->getProjectGuid() : QString();
    reads.root = AssetStorePaths::root();
    reads.dbPath = QSqlDatabase::database().databaseName();
    return reads;
}

ReadConnection::ReadConnection(const QString &dbPath)
    : mName(QStringLiteral("OpenReads-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
{
    mDb = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), mName);
    mDb.setDatabaseName(dbPath);
    mDb.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));
    mDb.open();
}

ReadConnection::~ReadConnection()
{
    // Close, drop our handle, then unregister (a live copy makes removeDatabase complain).
    if (mDb.isOpen()) mDb.close();
    mDb = QSqlDatabase();
    QSqlDatabase::removeDatabase(mName);
}

void readPlan(QSqlDatabase conn, Project *project, Reads &reads)
{
    {
        int queries = 0;
        LoadTimeline::Accumulate rows(QStringLiteral("db:sessionRows"), 0);
        reads.objectRows = ProjectAssets::readSessionRows(conn, reads.root, reads.projectGuid,
                                                          static_cast<int>(ModelTypes::Object), &queries);
        rows.stop();
        LoadTimeline::add(QStringLiteral("db:sessionRows"), 0.0, queries);
    }
    // THE PINNED MODELS ARE THE OBJECT ROWS' PATHS (the plan the registration and the reader share).
    for (const ProjectAssets::SessionRow &row : reads.objectRows)
        if (!row.path.isEmpty() && !reads.modelPaths.contains(row.path)) reads.modelPaths.append(row.path);

    // THE SCENE BLOB, READ ONCE AND PARSED ONCE (it was read three times and parsed three times).
    QByteArray blob;
    {
        LoadTimeline::Accumulate blobRead(QStringLiteral("db:sceneBlob"));
        QSqlQuery query(conn);
        query.prepare(QStringLiteral("SELECT scene FROM projects WHERE guid = ?"));
        query.addBindValue(reads.projectGuid);
        if (query.exec() && query.next()) blob = query.value(0).toByteArray();
    }
    if (!blob.isEmpty()) {
        LoadTimeline::Accumulate json(QStringLiteral("worker:sceneJson"));
        reads.projectObj = QJsonDocument::fromJson(blob).object();
        reads.found = true;
    }

    // ...and its sources, resolved on this connection exactly as the reader will resolve them.
    LoadTimeline::Accumulate plan(QStringLiteral("worker:plan"));
    SceneReader reader;
    reader.setProject(project);
    reader.setConnection(conn);
    if (reads.found) {
        for (const QString &path : reader.collectMeshSources(reads.projectObj))
            if (!reads.modelPaths.contains(path)) reads.modelPaths.append(path);
        reads.clipPaths = reader.collectClipSources(reads.projectObj);
    }
}

void readSession(QSqlDatabase conn, Reads &reads)
{
    int queries = 0;
    LoadTimeline::Accumulate rows(QStringLiteral("db:sessionRows"), 0);
    const int object = static_cast<int>(ModelTypes::Object);
    for (const int type : ProjectAssets::sessionTypes()) {
        if (type == object) continue;   // readPlan's
        reads.otherRows += ProjectAssets::readSessionRows(conn, reads.root, reads.projectGuid, type, &queries);
    }
    rows.stop();
    LoadTimeline::add(QStringLiteral("db:sessionRows"), 0.0, queries);
}

}   // namespace openreads
