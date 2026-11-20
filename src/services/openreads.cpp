/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/openreads.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSqlQuery>
#include <QUuid>

#include "data/project.h"
#include "io/sceneformat.h"
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
    // THE SCENE BLOB, READ ONCE AND PARSED ONCE (it was read three times and parsed three times) — and
    // FIRST (STUDIO-HYGIENE-1): a scene the open refuses plans NOTHING, no session row and no model path, so
    // the runner reads, rebuilds and writes no bake for a world it will not open.
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
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(blob, &error);
        // A BLOB THAT DOES NOT PARSE IS REFUSED (STUDIO-HYGIENE-1). It used to open as an EMPTY world with
        // a log line, and the next save wrote that empty world over the row: the scene lost, silently.
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            reads.refusal = error.error != QJsonParseError::NoError
                                ? QStringLiteral("its scene does not parse (%1 at byte %2)")
                                      .arg(error.errorString()).arg(error.offset)
                                : QStringLiteral("its scene is not a JSON object");
            return;
        }
        // ...AND SO IS A NEWER BUILD'S SCENE: read as this build's format, what that build added would be
        // dropped and the next save would write the scene back without it.
        const int version = sceneformat::versionOf(doc.object());
        if (version > sceneformat::kVersion) {
            reads.refusal = QStringLiteral("its scene is format version %1, made by a newer Jahshaka; this "
                                           "build reads version %2")
                                .arg(version).arg(sceneformat::kVersion);
            return;
        }
        reads.projectObj = doc.object();
        reads.found = true;
    }

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
