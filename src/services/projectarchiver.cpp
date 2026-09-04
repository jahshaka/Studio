/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/projectarchiver.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QtConcurrent>

#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/project.h"
#include "export/exportmanifest.h"
#include "io/ziphelper.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"

using exportformat::ExportManifest;
using exportformat::ManifestAsset;
using exportformat::ManifestFile;

QVector<ProjectArchiver *> ProjectArchiver::sLive;

namespace {

// assets.* verb type vocabulary (mirrors AssetsApi::typeNameOf).
QString typeNameOf(int type)
{
    switch (static_cast<ModelTypes>(type)) {
    case ModelTypes::Shader: return "shader";
    case ModelTypes::Material: return "material";
    case ModelTypes::Texture: return "texture";
    case ModelTypes::Video: return "video";
    case ModelTypes::Music: return "audio";
    case ModelTypes::Object: return "object";
    case ModelTypes::Mesh: return "mesh";
    case ModelTypes::Sky: return "sky";
    case ModelTypes::ParticleSystem: return "particle_system";
    case ModelTypes::File: return "file";
    default: return "asset";
    }
}

}   // namespace

ProjectArchiver::ProjectArchiver(Database *db, Project *project, QObject *parent)
    : QObject(parent), db(db), project(project)
{
    sLive.append(this);
}

ProjectArchiver::~ProjectArchiver()
{
    // A live worker writes into this object's members — it must be gone before
    // we are. It never waits on the UI thread, so this join is bounded by one
    // zip entry / one file copy. A BLOCKING join and not a pump: pumping the
    // event loop from a destructor is the exact re-entrancy
    // ProgressDialog::setPumpsEventLoop documents (~SceneOpenRunner does the
    // same thing for the same reason).
    mCanceled.store(true);
    if (mFuture.isValid() && !mFuture.isFinished()) mFuture.waitForFinished();
    mRunning.store(false);
    sLive.removeAll(this);
    delete mStage;
}

void ProjectArchiver::emitProgress(int percent, const QString &text)
{
    if (mThreaded && QThread::currentThread() != thread()) {
        // From the worker: through the event loop, never blocking. `this` is
        // the context object, so a dead archiver simply drops the call.
        QMetaObject::invokeMethod(this, [this, percent, text]() {
            emit progress(percent, text);
        }, Qt::QueuedConnection);
        return;
    }
    emit progress(percent, text);
}

void ProjectArchiver::finish(bool canceled)
{
    mResult.canceled = canceled;
    if (canceled && mResult.error.isEmpty())
        mResult.error = QStringLiteral("cancelled");
    // The staging directory goes here, at the ONE place every path ends: an
    // extracted Showroom is ~50 MB of /tmp, and a session archiver that keeps
    // one alive until the next import is a leak with a long fuse. Result::path
    // for an import has always named this directory and has always been stale
    // by the time a caller reads it — nothing uses it.
    delete mStage;
    mStage = nullptr;
    mRunning.store(false);
    if (mThreaded) emit finished(canceled);
}

// ===========================================================================
//  EXPORT
// ===========================================================================

bool ProjectArchiver::planExport(const QString &destZipPath)
{
    // UI THREAD. Every line below is a database read (or resolves a CAS path
    // from one) — this is the phase that cannot move, and the reason there is
    // no per-thread QSqlDatabase connection anywhere in this file.
    mResult = Result();
    mDestZip = destZipPath;
    mCopies.clear();
    mManifest = ExportManifest();

    if (!db || !project || project->getProjectGuid().isEmpty()) {
        mResult.error = QStringLiteral("no open project");
        return false;
    }
    const QString projectGuid = project->getProjectGuid();

    delete mStage;
    mStage = new QTemporaryDir();
    if (!mStage->isValid()) {
        mResult.error = QStringLiteral("cannot create a staging directory");
        return false;
    }

    emitProgress(5, QStringLiteral("Reading the catalog…"));

    // 1. The catalog snapshot (pin-aware since phase 4).
    db->createExportScene(mStage->path(), projectGuid);
    if (!QFileInfo::exists(QDir(mStage->path()).filePath(projectGuid + ".db"))) {
        mResult.error = QStringLiteral("could not write the catalog snapshot");
        return false;
    }

    // 2. Membership: project rows + pinned library assets.
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();

    QSqlQuery members(conn);
    members.prepare(
        "SELECT guid, name, type FROM assets "
        "WHERE project_guid = ? "
        "   OR guid IN (SELECT asset_guid FROM project_assets WHERE project_guid = ?)");
    members.addBindValue(projectGuid);
    members.addBindValue(projectGuid);
    members.exec();

    mManifest.kind = QStringLiteral("project");
    mManifest.generator = QStringLiteral("Jahshaka");
    mManifest.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    const QString objectsDir = QDir(mStage->path()).filePath(QStringLiteral("objects"));
    QSet<QString> written;

    while (members.next()) {
        ManifestAsset asset;
        asset.guid = members.value(0).toString();
        asset.name = members.value(1).toString();
        asset.typeId = members.value(2).toInt();
        asset.type = typeNameOf(asset.typeId);

        // Outgoing dependency edges.
        QSqlQuery deps(conn);
        deps.prepare("SELECT dependee FROM dependencies WHERE depender = ?");
        deps.addBindValue(asset.guid);
        if (deps.exec())
            while (deps.next()) asset.dependencies.append(deps.value(0).toString());

        // The asset's files: every recorded object, with the SOURCE role
        // materialized at the project's pinned content (I3 — the archive
        // carries the bytes the project renders with).
        const QString pin = AssetCas::pinnedOid(conn, projectGuid, asset.guid);

        QSqlQuery files(conn);
        files.prepare("SELECT AF.role, AF.name, AF.oid, F.size, F.ext FROM asset_files AF "
                      "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.asset_guid = ? "
                      "ORDER BY CASE AF.role WHEN 'source' THEN 0 ELSE 1 END, AF.name");
        files.addBindValue(asset.guid);
        files.exec();
        bool first = true;
        while (files.next()) {
            ManifestFile file;
            file.role = files.value(0).toString();
            file.name = files.value(1).toString();
            file.oid = files.value(2).toString();
            file.size = files.value(3).toLongLong();
            QString ext = files.value(4).toString();

            if (first && file.role == QStringLiteral("source") && !pin.isEmpty() && pin != file.oid) {
                // The project pinned different content than the library's
                // current source — the archive ships the PIN.
                QSqlQuery pinExt(conn);
                pinExt.prepare("SELECT size, ext FROM files WHERE oid = ?");
                pinExt.addBindValue(pin);
                if (pinExt.exec() && pinExt.next()) {
                    file.oid = pin;
                    file.size = pinExt.value(0).toLongLong();
                    ext = pinExt.value(1).toString();
                }
            }
            first = false;

            // The COPY is planned here (a CAS path resolution is a catalog
            // read) and performed on the worker.
            const QString objPath = AssetStorePaths::objectPathIn(root, file.oid, ext);
            if (!file.oid.isEmpty() && QFileInfo::exists(objPath) && !written.contains(file.oid)) {
                mCopies.append({ objPath, QDir(objectsDir).filePath(file.oid + "." + ext) });
                written.insert(file.oid);
                ++mResult.objects;
            }
            asset.files.append(file);
        }

        mManifest.assets.append(asset);
        ++mResult.assets;
    }
    return true;
}

bool ProjectArchiver::workExport()
{
    // WORKER THREAD (or inline for the synchronous verb). File work only.
    const QString objectsDir = QDir(mStage->path()).filePath(QStringLiteral("objects"));
    if (!mCopies.isEmpty()) QDir().mkpath(objectsDir);

    const int totalCopies = mCopies.size();
    for (int i = 0; i < totalCopies; ++i) {
        if (mCanceled.load()) return false;
        QFile::copy(mCopies.at(i).src, mCopies.at(i).dst);
        if ((i % 8) == 0 || i + 1 == totalCopies)
            emitProgress(10 + (40 * (i + 1)) / qMax(1, totalCopies),
                         QStringLiteral("Collecting content (%1 of %2)…")
                             .arg(i + 1).arg(totalCopies));
    }
    if (mCanceled.load()) return false;

    if (!mManifest.write(QDir(mStage->path()).filePath(exportformat::manifestFileName()),
                         &mResult.error))
        return false;

    // Legacy .manifest marker so pre-v2 validators recognise the archive.
    {
        QFile marker(QDir(mStage->path()).filePath(QStringLiteral(".manifest")));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
            marker.write("project\n");
    }

    emitProgress(55, QStringLiteral("Compressing…"));
    const bool zipped = ZipHelper::zipDirectory(
        mStage->path(), mDestZip, &mResult.error,
        [this](const QString &, int index, int total) {
            if (mCanceled.load()) return false;
            if (total > 0 && ((index % 8) == 0 || index == total))
                emitProgress(55 + (40 * index) / total,
                             QStringLiteral("Compressing (%1 of %2)…").arg(index).arg(total));
            return true;
        });
    return zipped && !mCanceled.load();
}

void ProjectArchiver::installExport()
{
    // UI THREAD. An export writes nothing to the catalog, so there is no
    // chunked commit here — only publishing the result.
    mResult.path = mDestZip;
    emitProgress(100, QStringLiteral("Exported."));
}

ProjectArchiver::Result ProjectArchiver::exportArchive(const QString &destZipPath)
{
    mThreaded = false;
    mExporting = true;
    mCanceled.store(false);
    mRunning.store(true);
    if (planExport(destZipPath) && workExport()) installExport();
    const bool canceled = mCanceled.load();
    finish(canceled);
    return mResult;
}

// ===========================================================================
//  IMPORT
// ===========================================================================

bool ProjectArchiver::planImport(const QString &zipPath)
{
    // UI THREAD. There is nothing to read from the catalog before the archive
    // is open, so this phase is short by nature — the staging directory and
    // the source check. (It stays a phase so both directions read the same.)
    mResult = Result();
    mSourceZip = zipPath;
    mIngest.clear();
    mNextIngest = 0;
    mGuidMap.clear();
    mBlobDbBase.clear();

    if (!db) { mResult.error = QStringLiteral("no database"); return false; }
    if (!QFileInfo::exists(zipPath)) {
        mResult.error = QStringLiteral("no such archive %1").arg(zipPath);
        return false;
    }

    delete mStage;
    mStage = new QTemporaryDir();
    if (!mStage->isValid()) {
        mResult.error = QStringLiteral("cannot create a staging directory");
        return false;
    }
    return true;
}

bool ProjectArchiver::workImport()
{
    // WORKER THREAD (or inline). Extraction + the manifest read + matching the
    // manifest's oids to files on disk. No database: everything below produces
    // a PLAN the install slices commit.
    emitProgress(5, QStringLiteral("Extracting…"));
    if (!ZipHelper::extract(mSourceZip, mStage->path(), &mResult.error,
                            [this](const QString &, int index, int total) {
                                if (mCanceled.load()) return false;
                                if (total > 0 && ((index % 16) == 0 || index == total))
                                    emitProgress(5 + (45 * index) / total,
                                                 QStringLiteral("Extracting (%1 of %2)…")
                                                     .arg(index).arg(total));
                                return true;
                            }))
        return false;
    if (mCanceled.load()) return false;

    // The catalog snapshot: <guid>.db.
    for (const QFileInfo &entry : QDir(mStage->path()).entryInfoList(QDir::Files)) {
        if (entry.suffix() == QStringLiteral("db")) { mBlobDbBase = entry.completeBaseName(); break; }
    }
    if (mBlobDbBase.isEmpty()) {
        mResult.error = QStringLiteral("not a Jahshaka project archive (no catalog snapshot)");
        return false;
    }

    QString manifestError;
    const ExportManifest manifest = ExportManifest::fromFile(
        QDir(mStage->path()).filePath(exportformat::manifestFileName()), &manifestError);

    // Objects + asset_files + pins, from the manifest (v2 archives; a legacy
    // archive without one imports rows only — its flat files are ignored,
    // there is no flat-folder world to put them in).
    if (manifest.isValid() && manifest.version >= 2) {
        const QDir objectsDir(QDir(mStage->path()).filePath(QStringLiteral("objects")));
        for (const ManifestAsset &asset : manifest.assets) {
            IngestAsset plan;
            plan.archiveGuid = asset.guid;
            for (const ManifestFile &file : asset.files) {
                // objects/<oid>.<ext> — find by oid prefix (ext recorded in name).
                const auto candidates =
                    objectsDir.entryInfoList({ file.oid + ".*", file.oid }, QDir::Files);
                if (candidates.isEmpty()) continue;
                plan.files.append({ candidates.first().absoluteFilePath(), file.role, file.name });
            }
            mIngest.append(plan);
        }
    }
    emitProgress(55, QStringLiteral("Importing the catalog…"));
    return !mCanceled.load();
}

void ProjectArchiver::beginInstallImport()
{
    // UI THREAD. The catalog half, and the only place this class writes to the
    // database.
    if (!db->checkIfProjectVersionSupported(QDir(mStage->path()).filePath(mBlobDbBase + ".db"))) {
        mResult.error = QStringLiteral("this scene was made with an unsupported version of Jahshaka");
        return;
    }

    // Catalog rows (fresh guids; scene blob remapped inside importProject).
    const QString newProjectGuid = GUIDManager::generateGUID();
    if (!db->importProject(QDir(mStage->path()).filePath(mBlobDbBase), newProjectGuid,
                           mResult.worldName, mGuidMap)) {
        mResult.error = QStringLiteral("the archive's catalog could not be imported");
        return;
    }
    mResult.projectGuid = newProjectGuid;
    mResult.path = mStage->path();
    mNextIngest = 0;
}

void ProjectArchiver::installImportSlice()
{
    // UI THREAD, one asset per event-loop turn. A slice is bounded by one
    // asset's files, which is what keeps the heartbeat gap inside its budget:
    // AssetCas::ingestFile hashes and copies, so a single huge texture is the
    // worst slice this operation can produce and there is nothing smaller to
    // cut without reaching into the CAS.
    if (mCanceled.load() || !mResult.error.isEmpty() || mNextIngest >= mIngest.size()) {
        if (mCanceled.load() && !mResult.projectGuid.isEmpty()) {
            // Roll the catalog back: a cancelled import must not leave a
            // half-populated project behind (Lane 4's "zero orphans").
            db->deleteProject(mResult.projectGuid);
            mResult.projectGuid.clear();
        }
        if (!mThreaded) return;
        if (!mCanceled.load() && mResult.error.isEmpty())
            emitProgress(100, QStringLiteral("Imported."));
        finish(mCanceled.load());
        return;
    }

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();

    const IngestAsset &asset = mIngest.at(mNextIngest++);
    const QString localGuid = mGuidMap.value(asset.archiveGuid, asset.archiveGuid);
    QString sourceOid;
    for (const IngestFile &file : asset.files) {
        QString oid;
        if (!AssetCas::ingestFile(conn, root, file.path, localGuid,
                                  file.role, file.name, &oid, &mResult.error)) {
            // Same as the pre-threading behaviour: the first ingest failure
            // ends the import, and no pin is written for a half-ingested
            // asset.
            if (mThreaded) finish(false);
            return;
        }
        if (sourceOid.isEmpty()) sourceOid = oid;
        ++mResult.objects;
    }
    AssetCas::writePin(conn, mResult.projectGuid, localGuid, sourceOid);
    ++mResult.assets;

    emitProgress(55 + (40 * mNextIngest) / qMax(1, mIngest.size()),
                 QStringLiteral("Storing content (%1 of %2)…")
                     .arg(mNextIngest).arg(mIngest.size()));

    if (!mThreaded) return;   // the synchronous path drives the loop itself

    // ONE millisecond, not zero: a chain of zero-timers is always "due" and
    // Qt's dispatcher keeps picking it over timers that are merely due NOW,
    // including the render tick (measured in the open lane). The lambda's
    // context object is `this`, so Qt drops it if the archiver dies first.
    QTimer::singleShot(1, this, [this]() { installImportSlice(); });
}

ProjectArchiver::Result ProjectArchiver::importArchive(const QString &zipPath)
{
    mThreaded = false;
    mExporting = false;
    mCanceled.store(false);
    mRunning.store(true);
    if (planImport(zipPath) && workImport()) {
        beginInstallImport();
        while (mResult.error.isEmpty() && !mCanceled.load() && mNextIngest < mIngest.size())
            installImportSlice();
        installImportSlice();   // the terminating call (rollback on cancel)
        if (mResult.error.isEmpty()) emitProgress(100, QStringLiteral("Imported."));
    }
    const bool canceled = mCanceled.load();
    finish(canceled);
    return mResult;
}

// ===========================================================================
//  The threaded drivers
// ===========================================================================

void ProjectArchiver::runWorkerThread()
{
    const bool ok = mExporting ? workExport() : workImport();
    // Hand back through the event loop — NOT a blocking connection: a UI loop
    // that stopped pumping (the app quitting) must never be able to strand
    // this worker (the import.shutdown zombie).
    QMetaObject::invokeMethod(this, [this, ok]() {
        if (!ok || mCanceled.load()) { finish(mCanceled.load()); return; }
        if (mExporting) { installExport(); finish(false); return; }
        beginInstallImport();
        if (!mResult.error.isEmpty()) { finish(false); return; }
        installImportSlice();
    }, Qt::QueuedConnection);
}

bool ProjectArchiver::startExport(const QString &destZipPath)
{
    if (mRunning.load()) return false;
    mThreaded = true;
    mExporting = true;
    mCanceled.store(false);
    mRunning.store(true);
    if (!planExport(destZipPath)) { finish(false); return false; }
    mFuture = QtConcurrent::run([this]() { runWorkerThread(); });
    return true;
}

bool ProjectArchiver::startImport(const QString &zipPath)
{
    if (mRunning.load()) return false;
    mThreaded = true;
    mExporting = false;
    mCanceled.store(false);
    mRunning.store(true);
    if (!planImport(zipPath)) { finish(false); return false; }
    mFuture = QtConcurrent::run([this]() { runWorkerThread(); });
    return true;
}

bool ProjectArchiver::waitForDone(int msTimeout)
{
    QPointer<ProjectArchiver> self(this);
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < msTimeout) {
        if (self.isNull() || !self->mRunning.load()) return true;
        // Service the worker's completion hop and the install slices so a
        // healthy run drains through its normal path; user input stays out so
        // nothing re-enters the UI mid-shutdown.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        if (self.isNull() || !self->mRunning.load()) return true;
        QThread::msleep(5);
    }
    return self.isNull() || !self->mRunning.load();
}

bool ProjectArchiver::shutdownArchives(int msTimeout)
{
    // Step 2 of the shutdown order. Bounded in TOTAL, not per archiver: the
    // close path's budget is the sum of everything it joins, and there is
    // never more than one archive in flight in practice.
    if (sLive.isEmpty()) return true;
    QElapsedTimer timer;
    timer.start();
    // A SNAPSHOT of guarded pointers: waitForDone pumps the event loop, and an
    // archiver can be deleted (its owner page destroyed, a slice finishing)
    // while we are inside it — which would both invalidate sLive's indices and
    // leave us holding a dangling raw pointer.
    QVector<QPointer<ProjectArchiver>> live;
    for (ProjectArchiver *archiver : sLive) live.append(QPointer<ProjectArchiver>(archiver));
    for (const QPointer<ProjectArchiver> &archiver : live)
        if (archiver) archiver->requestCancel();
    bool allStopped = true;
    for (const QPointer<ProjectArchiver> &archiver : live) {
        if (archiver.isNull()) continue;
        const int left = msTimeout - int(timer.elapsed());
        if (left <= 0) { allStopped = allStopped && !archiver->isRunning(); continue; }
        allStopped &= archiver->waitForDone(left);
    }
    return allStopped;
}
