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

#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QTemporaryDir>

#include <memory>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/primitives.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/logger.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimportservice.h"
#include "services/meshbakestore.h"
#include "services/uithreadwait.h"

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
    // .qrc files its target lists. The product always has the resource; a suite that links
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

namespace {

/// THE SEED'S IMPORT, HALF ONE: everything ensureSeeded checks and stages before
/// the pipeline runs. `Plan::guid` non-empty and `request` empty = the row is
/// already seeded (`staleJobs` then carries its rebuild when its bake is stale);
/// `error` set = this seed cannot run.
struct Plan
{
    const primitives::Def *def = nullptr;
    QString guid;
    QString error;
    bool seeded = false;                       ///< the row and its bytes were already there
    QVector<MeshBakeStore::BakeJob> staleJobs;  ///< a seeded row's stale bake, to rebuild
    std::shared_ptr<QTemporaryDir> staging;    ///< owns a ":/" seed's copy until the commit
    ImportRequest request;
    PreparedImport prepared;                   ///< the worker's half (prepare)
    qint64 prepareMs = 0;                      ///< its wall, for the import record
    QVector<MeshBakeStore::BakeResult> rebuilt;
};

Plan planSeed(const primitives::Def &def, Database *db)
{
    Plan plan;
    plan.def = &def;
    if (!def.guid || !def.mesh) { plan.error = QStringLiteral("the seed row is incomplete"); return plan; }
    if (!db) { plan.error = QStringLiteral("no library"); return plan; }
    plan.guid = QString::fromLatin1(def.guid);
    QSqlDatabase conn = QSqlDatabase::database();
    if (!conn.isValid() || !conn.isOpen()) {
        plan.error = QStringLiteral("no library connection on this thread");
        return plan;
    }
    const QString root = AssetStorePaths::root();

    // ALREADY SEEDED. The bake is re-checked rather than assumed: a format bump
    // (kFormatVersion, the BAKE KEY) leaves the row and its source in place with
    // a bake this build cannot read, and the seed is the one place that knows
    // how to rebuild it without waiting for a sweep.
    const QString have = storedSource(conn, root, db, plan.guid);
    if (!have.isEmpty()) {
        plan.seeded = true;
        if (!MeshBakeStore::isFresh(conn, root, have, plan.guid)) {
            plan.staleJobs = MeshBakeStore::staleJobsFor(conn, root, { have });
            // A FAILED RE-BAKE IS A FAILURE, not a seeded row with a warning
            // attached: a stale bake with no job to rebuild it (its source is
            // not a store object this build can key) is reported as one.
            if (plan.staleJobs.isEmpty())
                plan.error = QStringLiteral("the bake could not be rebuilt for this build");
        }
        return plan;
    }

    // A ROW WITH NO BYTES is not a seed, it is a leftover: a half-done seed, or
    // a store that moved without its objects. Nothing is owed to it (the crud
    // law) and the reserved guid must be free for the import below to take.
    const AssetRecord stale = db->fetchAsset(plan.guid);
    if (!stale.guid.isEmpty() || !stale.name.isEmpty()) db->deleteAsset(plan.guid, /*force=*/true);

    plan.staging = std::make_shared<QTemporaryDir>();
    const QString source = shippedFile(def, *plan.staging, &plan.error);
    if (source.isEmpty()) return plan;

    plan.request.sourcePath = source;
    plan.request.reservedGuid = plan.guid;
    // NOT THE USER'S GESTURE (IMPORT-INTENT-1). Nobody asked for this import:
    // the platform needs the geometry it ships in order to draw a floor or a
    // cube, so it may not take a member stamp off a row somebody else owns.
    plan.request.intent = ImportRequest::Intent::Material;
    // THE PLATFORM'S (ASSETS-HOME-1). A primitive belongs to every project at
    // once and to nobody's storage: never an Assets tile, re-seeded after a
    // reset, and no project stamp.
    plan.request.home = assethome::platform();
    return plan;
}

/// HALF TWO, the worker's: the import's CPU half (the parse and the BAKE) or the
/// stale bake's rebuild. No database (AssetImportService::prepare's contract).
void runSeed(Plan &plan, Database *db)
{
    if (!plan.error.isEmpty()) return;
    if (!plan.request.sourcePath.isEmpty()) {
        QElapsedTimer clock;
        clock.start();
        AssetImportService importer(db, nullptr);
        plan.prepared = importer.prepare(plan.request);
        plan.prepareMs = clock.elapsed();
        return;
    }
    for (const MeshBakeStore::BakeJob &job : std::as_const(plan.staleJobs))
        plan.rebuilt.append(MeshBakeStore::runBake(job));
}

/// HALF THREE, the database thread's: the import's commit (or the rebuild's) and
/// the platform marks. The guid on success, empty with `errorOut` set otherwise.
QString commitSeed(Plan &plan, Database *db, QString *errorOut)
{
    const auto failed = [&](const QString &why) {
        if (errorOut) *errorOut = why;
        return QString();
    };
    if (!plan.error.isEmpty()) return failed(plan.error);
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    if (plan.seeded) {
        for (MeshBakeStore::BakeResult &result : plan.rebuilt) {
            QString bakeError;
            if (!MeshBakeStore::commitBake(conn, root, result, &bakeError))
                return failed(bakeError.isEmpty()
                                  ? QStringLiteral("the bake could not be rebuilt for this build")
                                  : bakeError);
        }
        return plan.guid;
    }

    AssetImportService importer(db, nullptr);
    const ImportResult result = plan.prepared.ok() ? importer.commit(plan.prepared)
                                                   : plan.prepared.result;
    AssetImportService::logImportRecord(plan.request, result, plan.prepareMs);
    if (!result.ok())
        return failed(result.error.isEmpty() ? QStringLiteral("import failed") : result.error);
    if (result.assetGuid != plan.guid) {
        // The pipeline answered with a DIFFERENT row (the re-listing path: these
        // bytes were already an unlisted library row). The reserved guid is the
        // identity a favourite and a drop payload name, so a seed that cannot
        // have it is a defect, not a fallback.
        return failed(QStringLiteral("the seed for '%1' landed on row %2, not its reserved guid")
                          .arg(QString::fromLatin1(plan.def->name), result.assetGuid));
    }

    // PLATFORM FURNITURE, MARKED (the marker the default floor's checker
    // carries — services/assettray.h rule 4; the home kept it out of every
    // listing already). A primitive is offered by its TILE and by
    // `scene.addPrimitive`; it is not one of the user's imports.
    const AssetRecord row = db->fetchAsset(plan.guid);
    QJsonObject props = QJsonDocument::fromJson(row.properties).object();
    props.insert(QStringLiteral("type"), QStringLiteral("platform"));
    db->updateAssetProperties(plan.guid, QJsonDocument(props).toJson());
    return plan.guid;
}

}   // namespace

QString ensureSeeded(const primitives::Def &def, Database *db, QString *errorOut)
{
    if (errorOut) errorOut->clear();
    Plan plan = planSeed(def, db);
    // ONE SEED, ON DEMAND (a reset library, a test): the worker half runs off the
    // UI thread while it pumps, like every synchronous import (uithreadwait.h).
    if (plan.error.isEmpty() && (!plan.request.sourcePath.isEmpty() || !plan.staleJobs.isEmpty()))
        UiThreadWait::run([&]() { runSeed(plan, db); });
    return commitSeed(plan, db, errorOut);
}

int seedAll(Database *db, QStringList *errors)
{
    // THE WHOLE LIST IN THREE PASSES (VERB-IMPORT-OFF-UI-1): plan every row on the
    // database thread, run every import's parse + BAKE (and every stale rebuild)
    // on ONE worker in list order, then commit every row back here in the same
    // order. The UI thread bakes nothing — a fresh library's 18 builds were
    // ~1.34 s of it (SHIPPED-BAKES-1's census).
    //
    // THE ORDERING CONTRACT IS UNCHANGED (header: "seeded once per library,
    // synchronously, where the library is opened"): this returns only when every
    // row is committed, so no page, dock, document or verb ever sees a library
    // with half its seeds. The wait is a JOIN, not a pump (UiThreadWait::Pump::
    // None): the shell calls this from inside MainWindow's constructor, where an
    // event delivered to the half-built window could reach a part that does not
    // exist yet — so what the UI shows until the seed completes is exactly what
    // it showed before: the splash, still. One worker, not one per seed: the bake
    // is already four threads wide (MeshBake::setBakeThreads), and every suite
    // boots a fresh library, so a parallel seed would be a CPU spike per boot.
    QVector<Plan> plans;
    for (const primitives::Def &def : primitives::all()) {
        if (!def.guid) continue;
        plans.append(planSeed(def, db));
    }
    QElapsedTimer took;
    took.start();
    UiThreadWait::run([&]() {
        for (Plan &plan : plans) runSeed(plan, db);
    }, UiThreadWait::Pump::None);
    const double workerMs = double(took.nsecsElapsed()) / 1.0e6;

    int created = 0;
    for (Plan &plan : plans) {
        QString error;
        if (commitSeed(plan, db, &error).isEmpty()) {
            if (errors)
                *errors << QStringLiteral("%1: %2").arg(QString::fromLatin1(plan.def->name), error);
            continue;
        }
        if (!plan.seeded) ++created;
    }
    // THE FIRST BOOT'S COST, a reading and not a guess (SHIPPED-BAKES-1): the
    // worker's wall for the whole list; the UI thread waited for it and built none.
    if (created > 0)
        irisLog(QStringLiteral("seed: %1 shipped meshes imported and baked on a worker in %2 ms")
                    .arg(created)
                    .arg(workerMs, 0, 'f', 1));
    return created;
}


}   // namespace PrimitiveAssets
