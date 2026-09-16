/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MESHBAKESTORE_H
#define MESHBAKESTORE_H

// MeshBakeStore — the catalog half of the mesh bake (MESH_BAKE_SPEC phase 1).
//
// irisgl/import/meshbake.h knows how to BUILD and READ a bake; it knows
// nothing about the store. This is the piece that answers "where is the bake
// for this model file, and what fingerprint must it carry" out of the CAS, and
// that writes one for an asset that has none (the lazy re-bake of an existing
// library, and the `assets.bakeAll` verb behind it).
//
// TWO CONSUMERS, ONE RESULT. A world open used to parse every model TWICE —
// once for the document (SceneReader::getMesh) and once for the library's
// session entry (ProjectAssets::registerSessionAsset). While a SCOPE is open,
// this store hands both of them the same deserialized model, so the double
// build is gone as well as the double parse.
//
// THREADING. Resolution touches the database, and QSqlDatabase connections are
// per-thread, so `planFor`/`load` are UI-thread calls. The prewarm WORKER only
// ever receives an already-resolved iris::PrewarmItem (path + bake path +
// fingerprint) and reads files. The scope cache is mutex-guarded regardless.

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <QVector>

#include "irisgl/import/meshprewarm.h"
#include "irisgl/import/importsettings.h"

class Database;

namespace MeshBakeStore
{
/// THE IMPORT SETTINGS BEHIND A RESOLVED SOURCE PATH (IMPORT-1,
/// SPECS/IMPORT_DIALOG_SPEC.md §4.4). A store object's bake — and every parse
/// that stands in for one — is keyed on the asset's import settings, and the
/// only thing a path carries is its content id. This is the inverse lookup:
/// path -> oid -> the asset row(s) that name that content -> the record at
/// `properties.import.settings`. An absent or `{}` record is IDENTITY, which
/// is what every row imported before the import dialog carries.
///
/// `transformFor` additionally hands the file's own declared unit scale
/// (`properties.metadata.unitScale`) to the transform, so a UNIT OVERRIDE
/// costs no probe parse on a row the library has already measured.
///
/// A PATH IS NOT A COMPLETE IDENTITY ANY MORE, which is why `assetGuid` is
/// here: the store is content-addressed, so two library rows imported from the
/// SAME FILE with DIFFERENT settings share one store object and one path, and
/// they are two different geometries. A caller that knows which ROW it is
/// asking about — the scene reader (the node carries its mesh guid), the
/// session registration, the reimport — passes it and gets the right answer.
/// A caller that only has a path (the open prewarm's plan, assets.bakeAll)
/// gets the DEFAULT variant for that content: deterministic (the first row
/// found, newest-first), and the reader re-resolves per node anyway.
///
/// UI-THREAD/DB-THREAD, like planFor: it reads the catalog on the caller's
/// per-thread connection. Memoised per (path, guid) for the life of a scope.
QString settingsHashFor(QSqlDatabase conn, const QString &root, const QString &sourcePath,
                        const QString &assetGuid = QString());
QString settingsHashFor(const QString &sourcePath, const QString &assetGuid = QString());
iris::ImportTransform transformFor(QSqlDatabase conn, const QString &root,
                                   const QString &sourcePath,
                                   const QString &assetGuid = QString());
iris::ImportTransform transformFor(const QString &sourcePath,
                                   const QString &assetGuid = QString());

/// Where the bake for a resolved model file should be, and the fingerprint it
/// must carry. `bakePath` empty = this build has no bake for that content
/// (never baked, or the file is not a store object).
iris::PrewarmItem planFor(QSqlDatabase conn, const QString &root, const QString &sourcePath,
                          const QString &assetGuid = QString());

/// Same, on the default connection and the active store root.
iris::PrewarmItem planFor(const QString &sourcePath, const QString &assetGuid = QString());

/// Resolve + read. Null when there is no usable bake — every failure mode
/// (absent, stale fingerprint, wrong version, truncated, corrupt) is a null
/// return and the caller parses as it always did.
///
/// Cached while a scope is open, so the scene reader and the session
/// registration share ONE deserialized model.
iris::BakedModelPtr load(const QString &sourcePath, const QString &assetGuid = QString());

/// Refcounted cache window. An open brackets itself with these; outside a
/// scope `load` still works, it just does not retain.
void beginScope();
void endScope();
void clear();

/// True when this build can read the bake recorded for `sourcePath`.
bool isFresh(QSqlDatabase conn, const QString &root, const QString &sourcePath,
             const QString &assetGuid = QString());

/// Build (or rebuild) the bake for ONE Object/Mesh asset and record it in the
/// CAS under `guid` and its mesh member. Returns false with `errorOut` set on
/// a real failure; a `dryRun` reports what WOULD be baked and writes nothing.
/// `bakedOut` (optional) receives true when a bake was written/needed.
bool bakeAsset(Database *db, QSqlDatabase conn, const QString &root, const QString &guid,
               bool dryRun, bool *neededOut, QString *errorOut);

/// Every MODEL FILE in the store that has no fresh bake, as absolute object
/// paths.
///
/// CONTENT-FIRST, deliberately. The obvious sweep — "every Object asset row,
/// resolveSource, check the extension" — MISSES EVERY ARCHIVE-IMPORTED
/// ASSET, which is all five sample worlds and every project a user ever
/// received: a .jaf ingest names its Object row after the model's BASE name
/// ("StanfordDragon") and files the model under the MESH member row, so the
/// Object row has no source-role file at all and the sweep reported "nothing
/// to bake" over a library with no bakes in it (found by this lane's pixel
/// run, 2026-09-04). Asking the FILES which of them are models has neither
/// failure mode.
/// A bake to build: the model FILE, and the library ROW whose import settings
/// key it. Since the import dialog the pair is the unit, not the path — the
/// store is content-addressed, so two rows over one object with DIFFERENT
/// settings are two different bakes, and a sweep that deduplicated by content
/// alone would build one of them and leave the other parsing on every open,
/// forever (the second read's F3).
struct BakeTarget
{
    QString path;
    QString assetGuid;
};

/// Every (model file, owning row) pair in the store with no fresh bake, one
/// entry per DISTINCT set of import settings over one object.
QVector<BakeTarget> modelBakesNeeded(QSqlDatabase conn, const QString &root);

/// The paths of the above, for a caller that only wants to know how much work
/// there is. Kept because the shape reads better at a call site that reports.
QStringList modelSourcesNeedingBake(QSqlDatabase conn, const QString &root);

/// Bake ONE model FILE (a resolved store object) and record it under every
/// asset row that names that content. Parses the file — this is the expensive
/// direction, for the lazy re-bake and `assets.bakeAll`.
bool bakeSource(QSqlDatabase conn, const QString &root, const QString &sourcePath,
                QString *errorOut, const QString &assetGuid = QString());

// --- Lazy re-bake (MESH_BAKE_SPEC phase 1, "existing libraries") -----------
//
// A library that predates the bake — every project imported from an archive,
// which is how all five sample worlds arrive — has none. Rather than make the
// user find a button, the first open of such a world queues the bake: the
// parse and the serialize happen on a worker AFTER the world is on screen,
// and the catalog write is one small UI-thread step per model. The open that
// pays for it is the one that was already going to parse; every open after it
// is a load.

/// Queue the bakes in `targets` that are not fresh. Returns how many were
/// queued. Safe to call with targets that are already baked or queued.
int scheduleBakes(const QVector<BakeTarget> &targets);

/// Path-only form, for the OPEN-TIME warm-up: the scene's model paths with no
/// row attached, so each gets that content's DEFAULT settings variant. That is
/// the right answer there — an open warms what it just read — and
/// `assets.bakeAll` is the sweep that covers every row.
int scheduleBakes(const QStringList &paths);

/// How many bakes are queued or in flight (tests, and the honest answer for a
/// status line).
int pendingBakes();

/// Drop the queue and stop scheduling. Called at shutdown; a bake in flight
/// finishes into its own temp dir and is simply discarded.
void cancelPendingBakes();
}   // namespace MeshBakeStore

#endif   // MESHBAKESTORE_H
