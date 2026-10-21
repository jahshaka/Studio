/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef OPENREADS_H
#define OPENREADS_H

// OpenReads — EVERY CATALOG READ OF A PROJECT OPEN, OFF THE UI THREAD (OPEN-PATH-1 stage 2).
//
// An open used to read the catalog on the UI thread in four places before its first world frame:
// the plan (the pinned models' paths, the scene blob read and parsed twice for its mesh and clip
// sources, the bake lookups), the session registration (three queries per asset — measured on Bistro
// Exterior, 539 assets in 68 slices of 8: 2.4 s), and the scene blob once more for the reader. All of
// it is read here instead, on the open runner's worker, each half with a read-only connection of its
// own to the library file (QSqlDatabase connections are per thread):
//   * readPlan    — the Object rows of the session set (their paths ARE the pinned models' plan), the
//                   scene blob (read ONCE, parsed ONCE) and its mesh and clip sources;
//   * readSession — the session set's other types, one query per type, beside the bake read.
// What stays on the UI thread is what must: the AssetManager entries built from the rows
// (ProjectAssets::registerSessionRow) and the document built from the parsed blob (SceneReader).

#include <QJsonObject>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

#include "services/projectassets.h"

class Project;

namespace openreads
{

/// What an open read, filled by the worker and handed to the UI thread's slices after it is done.
struct Reads
{
    QString projectGuid;
    QString root;           ///< the asset store root (AssetStorePaths::root(), read on the UI thread)
    QString dbPath;         ///< the library file every worker connection opens
    /// The session set, by type (ProjectAssets::sessionTypes() order once merged): the Object rows from
    /// readPlan, the rest from readSession.
    QVector<ProjectAssets::SessionRow> objectRows;
    QVector<ProjectAssets::SessionRow> otherRows;
    /// The scene blob, parsed (`found` false = no project row or an empty blob).
    QJsonObject projectObj;
    bool found = false;
    QStringList modelPaths;   ///< the pinned models + the blob's mesh sources: the bake read's plan
    QStringList clipPaths;    ///< the blob's clip files: their stale clip bakes are rebuilt first

    /// The session set in registration order.
    QVector<ProjectAssets::SessionRow> sessionRows() const;
};

/// The UI-thread half of the setup: the three values a worker needs (the project, the store root, the
/// library file). Call on the thread that owns the default connection.
Reads prepare(Project *project);

/// A read-only connection of the CALLING thread to the library file, removed with this object.
class ReadConnection
{
public:
    explicit ReadConnection(const QString &dbPath);
    ~ReadConnection();
    ReadConnection(const ReadConnection &) = delete;
    ReadConnection &operator=(const ReadConnection &) = delete;
    QSqlDatabase &db() { return mDb; }
    bool isOpen() const { return mDb.isOpen(); }

private:
    QString mName;
    QSqlDatabase mDb;
};

/// THE PLAN HALF (any thread; `conn` of that thread): fills objectRows, projectObj/found, modelPaths and
/// clipPaths. Counted in the open's ledger: db:sessionRows (its one query), db:sceneBlob, worker:plan.
void readPlan(QSqlDatabase conn, Project *project, Reads &reads);

/// THE SESSION HALF (any thread): fills otherRows — one query per remaining session type.
void readSession(QSqlDatabase conn, Reads &reads);

}   // namespace openreads

#endif   // OPENREADS_H
