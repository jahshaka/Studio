/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/meshbakestore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>

#include "data/database/database.h"
#include "data/constants.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "irisgl/core/logger.h"
#include "irisgl/import/meshbake.h"

namespace
{

QMutex sLock;
int sScopeDepth = 0;
QHash<QString, iris::BakedModelPtr> sCache;

/// The sha256 a store object's file name IS. Empty for anything that is not a
/// store object (a legacy folder file, an out-of-store path) — such a source
/// has no content id we can key a bake on without re-hashing it, and the open
/// path simply parses.
QString oidFromStorePath(const QString &root, const QString &path)
{
    const QFileInfo info(path);
    // objectPathIn's own layout, asked rather than assumed: <root>/objects/xx/.
    const QString objectsDir =
        QFileInfo(AssetStorePaths::objectPathIn(root, QString(64, QLatin1Char('0')),
                                                QStringLiteral("x")))
            .absolutePath();
    const QString parent = QFileInfo(objectsDir).absolutePath();
    if (!info.absolutePath().startsWith(parent + QLatin1Char('/'))) return QString();
    const QString oid = info.completeBaseName().toLower();
    return oid.length() == 64 ? oid : QString();
}

/// Record a written bake in the catalog under EVERY asset row that names the
/// source content, so it is reachable from each of them (Object + Mesh member)
/// and dies with the last one — the same shape the source itself has, which is
/// what makes `assets.gc` reap it without knowing bakes exist.
bool recordBake(QSqlDatabase conn, const QString &root, const QString &sourceOid,
                const QString &bakePath, QString *errorOut)
{
    QStringList owners;
    QSqlQuery owner(conn);
    owner.prepare("SELECT DISTINCT asset_guid FROM asset_files WHERE oid = ?");
    owner.addBindValue(sourceOid);
    if (owner.exec())
        while (owner.next()) owners.append(owner.value(0).toString());
    if (owners.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("no asset row names the source content %1")
                            .arg(sourceOid.left(12));
        return false;
    }

    const QString bakeName = QFileInfo(bakePath).fileName();
    for (const QString &ownerGuid : owners) {
        QString oid;
        if (!AssetCas::ingestFile(conn, root, bakePath, ownerGuid,
                                  iris::MeshBake::casRole(), bakeName, &oid, errorOut))
            return false;
    }
    for (const QString &ownerGuid : owners) {
        QString casError;
        AssetCas::writeSidecar(conn, root, ownerGuid, &casError);
        if (!casError.isEmpty()) irisLog("mesh bake: " + casError);
    }
    return true;
}

}   // namespace

namespace MeshBakeStore
{

iris::PrewarmItem planFor(QSqlDatabase conn, const QString &root, const QString &sourcePath)
{
    iris::PrewarmItem item;
    item.path = sourcePath;
    if (sourcePath.isEmpty()) return item;

    const QString sourceOid = oidFromStorePath(root, sourcePath);
    if (sourceOid.isEmpty()) return item;

    // The bake's DISPLAY NAME is derived from the source content id, so one
    // indexed lookup finds it without knowing which asset guid owns the
    // source (a model file is recorded under both the Object and the Mesh
    // member row, and either may be the one that resolved).
    QSqlQuery query(conn);
    query.prepare("SELECT AF.oid, F.ext FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid "
                  "WHERE AF.role = ? AND AF.name = ? LIMIT 1");
    query.addBindValue(iris::MeshBake::casRole());
    query.addBindValue(iris::MeshBake::fileNameFor(sourceOid));
    if (!query.exec() || !query.next()) return item;

    const QString path = AssetStorePaths::objectPathIn(root, query.value(0).toString(),
                                                       query.value(1).toString());
    if (!QFileInfo::exists(path)) return item;
    item.bakePath = path;
    item.bakeFingerprint = iris::MeshBake::fingerprintFor(sourceOid);
    return item;
}

iris::PrewarmItem planFor(const QString &sourcePath)
{
    return planFor(QSqlDatabase::database(), AssetStorePaths::root(), sourcePath);
}

iris::BakedModelPtr load(const QString &sourcePath)
{
    if (sourcePath.isEmpty()) return iris::BakedModelPtr();
    {
        QMutexLocker locked(&sLock);
        const auto hit = sCache.constFind(sourcePath);
        if (hit != sCache.constEnd()) return hit.value();
    }

    const iris::PrewarmItem item = planFor(sourcePath);
    iris::BakedModelPtr result;
    if (!item.bakePath.isEmpty()) {
        iris::MeshBake::Model model = iris::MeshBake::read(item.bakePath, item.bakeFingerprint);
        if (model.valid)
            result = std::make_shared<const iris::MeshBake::Model>(std::move(model));
    }

    QMutexLocker locked(&sLock);
    // Negative results are cached too: a model with no bake must not re-query
    // the catalog for every mesh node that references it.
    if (sScopeDepth > 0) sCache.insert(sourcePath, result);
    return result;
}

void beginScope()
{
    QMutexLocker locked(&sLock);
    ++sScopeDepth;
}

void endScope()
{
    QMutexLocker locked(&sLock);
    if (sScopeDepth > 0) --sScopeDepth;
    if (sScopeDepth == 0) sCache.clear();
}

void clear()
{
    QMutexLocker locked(&sLock);
    sCache.clear();
}

bool isFresh(QSqlDatabase conn, const QString &root, const QString &sourcePath)
{
    const iris::PrewarmItem item = planFor(conn, root, sourcePath);
    if (item.bakePath.isEmpty()) return false;
    return iris::MeshBake::read(item.bakePath, item.bakeFingerprint).valid;
}

bool bakeAsset(Database *db, QSqlDatabase conn, const QString &root, const QString &guid,
               bool dryRun, bool *neededOut, QString *errorOut)
{
    if (neededOut) *neededOut = false;
    if (!db) { if (errorOut) *errorOut = QStringLiteral("no database"); return false; }

    const AssetRecord record = db->fetchAsset(guid);
    if (record.guid.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("no such asset %1").arg(guid);
        return false;
    }
    const ModelTypes type = static_cast<ModelTypes>(record.type);
    if (type != ModelTypes::Object && type != ModelTypes::Mesh) {
        // Not a model: nothing to bake, and not an error.
        return true;
    }

    // The MODEL among this asset's files, found by extension rather than by
    // role: an archive-imported Object row carries its model under a 'file'
    // role (the .jaf ingest derives the role from the file NAME, which never
    // matches an Object row named after the model's base name).
    QString sourcePath;
    {
        QSqlQuery files(conn);
        files.prepare("SELECT AF.oid, AF.name, F.ext FROM asset_files AF "
                      "LEFT JOIN files F ON AF.oid = F.oid "
                      "WHERE AF.asset_guid = ? AND AF.role <> ? "
                      "ORDER BY CASE AF.role WHEN 'source' THEN 0 ELSE 1 END, AF.name");
        files.addBindValue(guid);
        files.addBindValue(iris::MeshBake::casRole());
        if (files.exec()) {
            while (files.next()) {
                if (!Constants::MODEL_EXTS.contains(
                        QFileInfo(files.value(1).toString()).suffix().toLower()))
                    continue;
                const QString candidate = AssetStorePaths::objectPathIn(
                    root, files.value(0).toString(), files.value(2).toString());
                if (QFileInfo::exists(candidate)) { sourcePath = candidate; break; }
            }
        }
    }
    if (sourcePath.isEmpty()) {
        // A DB-only row, a non-model asset, or a store whose bytes are
        // offline. Not this lane's problem, and not a failure of the bake.
        return true;
    }

    const QString sourceOid = oidFromStorePath(root, sourcePath);
    if (sourceOid.isEmpty()) return true;   // legacy-folder bytes: no content id to key on

    if (isFresh(conn, root, sourcePath)) return true;
    if (neededOut) *neededOut = true;
    if (dryRun) return true;
    if (!bakeSource(conn, root, sourcePath, errorOut)) return false;
    clear();
    return true;
}

bool bakeSource(QSqlDatabase conn, const QString &root, const QString &sourcePath,
                QString *errorOut)
{
    const QString sourceOid = oidFromStorePath(root, sourcePath);
    if (sourceOid.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("'%1' is not a store object — nothing to key a bake on")
                            .arg(QFileInfo(sourcePath).fileName());
        return false;
    }

    // Extraction has to go SOMEWHERE (MaterialHelper writes embedded textures
    // while it reads a material), and it must not be next to the CAS objects:
    // a re-bake is not an import and may not add bytes to the store.
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        if (errorOut) *errorOut = QStringLiteral("cannot create a bake staging directory");
        return false;
    }

    iris::MeshBake::Model model = iris::MeshBake::buildFromFile(
        sourcePath, iris::MeshBake::fingerprintFor(sourceOid), scratch.path());
    if (!model.valid) {
        if (errorOut)
            *errorOut = QStringLiteral("could not parse '%1' for baking").arg(sourcePath);
        return false;
    }

    const QString bakeName = iris::MeshBake::fileNameFor(sourceOid);
    const QString bakePath = QDir(scratch.path()).filePath(bakeName);
    if (!iris::MeshBake::write(bakePath, model, errorOut)) return false;
    return recordBake(conn, root, sourceOid, bakePath, errorOut);
}

// --- Lazy re-bake -----------------------------------------------------------

namespace
{

QStringList sQueue;                 ///< UI thread only
bool sBakeInFlight = false;         ///< UI thread only
bool sCancelled = false;

/// What a worker produced: the temp dir that holds the blob (kept alive with
/// the result) and the path inside it.
struct BakeOutput
{
    std::shared_ptr<QTemporaryDir> dir;
    QString path;
    QString sourceOid;
};

void pumpQueue();

void startNext()
{
    if (sBakeInFlight || sQueue.isEmpty() || sCancelled) return;
    if (!QCoreApplication::instance()) { sQueue.clear(); return; }

    const QString sourcePath = sQueue.takeFirst();
    const QString root = AssetStorePaths::root();
    const QString sourceOid = oidFromStorePath(root, sourcePath);
    if (sourceOid.isEmpty()) { pumpQueue(); return; }
    if (isFresh(QSqlDatabase::database(), root, sourcePath)) { pumpQueue(); return; }

    sBakeInFlight = true;
    auto *watcher = new QFutureWatcher<BakeOutput>();
    QObject::connect(watcher, &QFutureWatcherBase::finished, QCoreApplication::instance(),
                     [watcher]() {
        const BakeOutput out = watcher->result();
        watcher->deleteLater();
        sBakeInFlight = false;
        if (!out.path.isEmpty() && !sCancelled) {
            QString error;
            if (recordBake(QSqlDatabase::database(), AssetStorePaths::root(),
                           out.sourceOid, out.path, &error)) {
                clear();
                irisLog("mesh bake: baked " + out.sourceOid.left(12));
            } else if (!error.isEmpty()) {
                irisLog("mesh bake: " + error);
            }
        }
        pumpQueue();
    });
    // The PARSE runs on a worker: it is the cost the bake exists to remove and
    // it must not be paid on the UI thread just because it is being removed.
    // The lambda touches nothing but its captured values.
    watcher->setFuture(QtConcurrent::run([sourcePath, sourceOid]() -> BakeOutput {
        BakeOutput out;
        out.sourceOid = sourceOid;
        auto dir = std::make_shared<QTemporaryDir>();
        if (!dir->isValid()) return out;
        iris::MeshBake::Model model = iris::MeshBake::buildFromFile(
            sourcePath, iris::MeshBake::fingerprintFor(sourceOid), dir->path());
        if (!model.valid) return out;
        const QString path = QDir(dir->path()).filePath(iris::MeshBake::fileNameFor(sourceOid));
        QString error;
        if (!iris::MeshBake::write(path, model, &error)) return out;
        out.dir = dir;
        out.path = path;
        return out;
    }));
}

void pumpQueue()
{
    if (sCancelled || sQueue.isEmpty()) return;
    QTimer::singleShot(0, QCoreApplication::instance(), []() { startNext(); });
}

}   // namespace

int scheduleBakes(const QStringList &paths)
{
    if (!QCoreApplication::instance()) return 0;
    sCancelled = false;
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    int queued = 0;
    for (const QString &path : paths) {
        if (path.isEmpty() || sQueue.contains(path)) continue;
        if (oidFromStorePath(root, path).isEmpty()) continue;
        if (isFresh(conn, root, path)) continue;
        sQueue.append(path);
        ++queued;
    }
    if (queued) pumpQueue();
    return queued;
}

int pendingBakes() { return sQueue.size() + (sBakeInFlight ? 1 : 0); }

void cancelPendingBakes()
{
    sCancelled = true;
    sQueue.clear();
}

QStringList modelSourcesNeedingBake(QSqlDatabase conn, const QString &root)
{
    QStringList out;
    // Every recorded file whose DISPLAY NAME is a model, deduplicated by
    // content: one object backs however many asset rows name it, and it needs
    // exactly one bake.
    QSqlQuery query(conn);
    query.prepare("SELECT DISTINCT AF.oid, AF.name, F.ext FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.role <> ?");
    query.addBindValue(iris::MeshBake::casRole());
    if (!query.exec()) return out;
    QSet<QString> seen;
    while (query.next()) {
        const QString oid = query.value(0).toString();
        if (seen.contains(oid)) continue;
        if (!Constants::MODEL_EXTS.contains(
                QFileInfo(query.value(1).toString()).suffix().toLower()))
            continue;
        seen.insert(oid);
        const QString path = AssetStorePaths::objectPathIn(root, oid, query.value(2).toString());
        if (!QFileInfo::exists(path)) continue;   // offline/purged object
        if (isFresh(conn, root, path)) continue;
        out.append(path);
    }
    return out;
}

}   // namespace MeshBakeStore
