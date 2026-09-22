/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// THE SEED HALF of services/primitiveassets.h — the part that CREATES a
// primitive's library asset by running the one import pipeline over a shipped
// mesh file.
//
// ITS OWN TRANSLATION UNIT, measured rather than tidied: `PrimitiveAssets::mesh`
// is called from the scene reader, the default floor and the preview docks, and
// four preview-only test binaries link one of those with six sources and no
// catalog at all. Keeping the import pipeline on the other side of this file's
// boundary is what lets them resolve a seed (or honestly fail to) without
// depending on the importer, the store's ingest and the thumbnail stack.
//
// The shell seeds the whole list once when it opens a library (MainWindow), so
// the resolve half always finds its rows.

#include "services/primitiveassets.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QTemporaryDir>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/primitives.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/logger.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/meshbakestore.h"

namespace PrimitiveAssets
{

namespace {

/// THE SHIPPED FILE, AS A FILE THE IMPORTER CAN OPEN.
///
/// The pipeline's contract is a path on the filesystem — it sniffs by
/// extension, scans the .obj for its `mtllib` sidecars, hashes the bytes for the
/// content id and stages them — and assimp cannot open a Qt resource at all
/// (`Mesh::loadMesh` only ever worked because it read resources into memory with
/// an extension hint). So a ":/" seed is COPIED ONCE into a private temporary
/// directory under its own file name and imported from there. The bytes decide
/// the object id, so where the copy sat is invisible afterwards; the name decides
/// the row's name, which is why it is preserved.
///
/// An "app/..." seed is already a file and is imported in place.
///
/// `holder` owns the copy and must outlive the import.
QString shippedFile(const primitives::Def &def, QTemporaryDir &holder, QString *errorOut)
{
    const QString seed = QString::fromLatin1(def.mesh);
    // TWO PLACES, ONE FILE. A shipped mesh is compiled into the app's resources
    // AND copied beside the binary (the app folder IS the resource tree: ":/x/y"
    // ships as "app/x/y"), and which of them a given binary has depends on the
    // .qrc files its target lists — the same fact bridge/previewmesh.h states for
    // the preview docks. The product always has the resource; a suite that links
    // no .qrc reads the same bytes from the app folder, and the bytes are what the
    // object id is made of, so the two routes cannot produce different content.
    const QString onDisk = IrisUtils::getAbsoluteAssetPath(
        seed.startsWith(QLatin1Char(':')) ? QStringLiteral("app") + seed.mid(1) : seed);
    if (!seed.startsWith(QLatin1Char(':'))) {
        if (QFileInfo(onDisk).isFile()) return onDisk;
        if (errorOut) *errorOut = QStringLiteral("the shipped file '%1' is not there").arg(onDisk);
        return QString();
    }
    if (QFileInfo::exists(seed)) {
        // A Qt resource cannot be handed to assimp (the pipeline's contract is a
        // FILE: it sniffs by extension, scans the .obj for its `mtllib` sidecars
        // and hashes the bytes), so it is copied ONCE into a private temporary
        // directory under its own file name and imported from there. Where the
        // copy sat is invisible afterwards; the NAME is preserved because it names
        // the row.
        if (!holder.isValid()) {
            if (errorOut) *errorOut = QStringLiteral("cannot create a staging directory");
            return QString();
        }
        const QString name = seed.mid(seed.lastIndexOf(QLatin1Char('/')) + 1);
        const QString out = QDir(holder.path()).filePath(name);
        if (QFile::copy(seed, out)) return out;
        if (errorOut) *errorOut = QStringLiteral("cannot extract the resource '%1'").arg(seed);
        return QString();
    }
    if (QFileInfo(onDisk).isFile()) return onDisk;
    if (errorOut)
        *errorOut = QStringLiteral("neither the resource '%1' nor the file '%2' is there")
                        .arg(seed, onDisk);
    return QString();
}

/// The row exists and its source bytes are in the store: the path to them.
QString storedSource(QSqlDatabase conn, const QString &root, Database *db, const QString &guid)
{
    if (!db) return QString();
    const AssetRecord row = db->fetchAsset(guid);
    if (row.guid.isEmpty() && row.name.isEmpty()) return QString();
    return AssetCas::resolveSource(conn, root, guid);
}

}   // namespace

QString ensureSeeded(const primitives::Def &def, Database *db, QString *errorOut)
{
    if (errorOut) errorOut->clear();
    if (!def.guid || !def.mesh) {
        if (errorOut) *errorOut = QStringLiteral("the seed row is incomplete");
        return QString();
    }
    if (!db) {
        if (errorOut) *errorOut = QStringLiteral("no library");
        return QString();
    }
    const QString guid = QString::fromLatin1(def.guid);
    QSqlDatabase conn = QSqlDatabase::database();
    if (!conn.isValid() || !conn.isOpen()) {
        if (errorOut) *errorOut = QStringLiteral("no library connection on this thread");
        return QString();
    }
    const QString root = AssetStorePaths::root();

    // ALREADY SEEDED. The bake is re-checked rather than assumed: a format bump
    // (kFormatVersion, the BAKE KEY) leaves the row and its source in place with
    // a bake this build cannot read, and the seed is the one place that knows
    // how to rebuild it without waiting for a sweep.
    const QString have = storedSource(conn, root, db, guid);
    if (!have.isEmpty()) {
        if (!MeshBakeStore::isFresh(conn, root, have, guid)) {
            // A FAILED RE-BAKE IS A FAILURE, not a seeded row with a warning
            // attached. This used to set errorOut and still answer with the guid,
            // so `seedAll` — which reports only an EMPTY return — counted the row
            // as seeded and said nothing, and the caller then asked a mesh of an
            // asset whose bake this build cannot read.
            QString bakeError;
            if (!MeshBakeStore::bakeSource(conn, root, have, &bakeError, guid)) {
                if (errorOut)
                    *errorOut = bakeError.isEmpty()
                                    ? QStringLiteral("the bake could not be rebuilt for this build")
                                    : bakeError;
                return QString();
            }
        }
        return guid;
    }

    // A ROW WITH NO BYTES is not a seed, it is a leftover: a half-done seed, or
    // a store that moved without its objects. Nothing is owed to it (the crud
    // law) and the reserved guid must be free for the import below to take.
    const AssetRecord stale = db->fetchAsset(guid);
    if (!stale.guid.isEmpty() || !stale.name.isEmpty()) db->deleteAsset(guid, /*force=*/true);

    QTemporaryDir staging;
    QString fileError;
    const QString source = shippedFile(def, staging, &fileError);
    if (source.isEmpty()) {
        if (errorOut) *errorOut = fileError;
        return QString();
    }

    ImportRequest request;
    request.sourcePath = source;
    request.reservedGuid = guid;
    // NOT THE USER'S GESTURE (IMPORT-INTENT-1). Nobody asked for this import:
    // the platform needs the geometry it ships in order to draw a floor or a
    // cube, so it may not take a member stamp off a row somebody else owns.
    request.intent = ImportRequest::Intent::Material;
    // NO PROJECT. A primitive belongs to the LIBRARY and to every project at
    // once; stamping it with whichever project happened to be open would make
    // app furniture look like that project's content.
    AssetImportService importer(db, nullptr);
    const ImportResult result = importer.import(request);
    if (!result.ok()) {
        if (errorOut)
            *errorOut = result.error.isEmpty() ? QStringLiteral("import failed") : result.error;
        return QString();
    }
    if (result.assetGuid != guid) {
        // The pipeline answered with a DIFFERENT row (the re-listing path: these
        // bytes were already an unlisted library row). The reserved guid is the
        // identity a favourite and a drop payload name, so a seed that cannot
        // have it is a defect, not a fallback.
        if (errorOut)
            *errorOut = QStringLiteral("the seed for '%1' landed on row %2, not its reserved guid")
                            .arg(QString::fromLatin1(def.name), result.assetGuid);
        return QString();
    }

    // PLATFORM FURNITURE, MARKED AND OUT OF THE LIBRARY LISTINGS (the same two
    // marks the default floor's checker carries — services/shippedassets.h,
    // services/assettray.h rule 4). A primitive is offered by its TILE and by
    // `scene.addPrimitive`; it is not one of the user's imports, and the Assets
    // page listing is theirs.
    const AssetRecord row = db->fetchAsset(guid);
    QJsonObject props = QJsonDocument::fromJson(row.properties).object();
    props.insert(QStringLiteral("type"), QStringLiteral("platform"));
    db->updateAssetProperties(guid, QJsonDocument(props).toJson());
    db->updateAssetViewFilter(guid, static_cast<int>(AssetViewFilter::Editor));
    return guid;
}

int seedAll(Database *db, QStringList *errors)
{
    int created = 0;
    for (const primitives::Def &def : primitives::all()) {
        if (!def.guid) continue;
        QSqlDatabase conn = QSqlDatabase::database();
        const bool had = !storedSource(conn, AssetStorePaths::root(), db,
                                      QString::fromLatin1(def.guid)).isEmpty();
        QString error;
        if (ensureSeeded(def, db, &error).isEmpty()) {
            if (errors)
                *errors << QStringLiteral("%1: %2").arg(QString::fromLatin1(def.name), error);
            continue;
        }
        if (!had) ++created;
    }
    return created;
}


}   // namespace PrimitiveAssets
