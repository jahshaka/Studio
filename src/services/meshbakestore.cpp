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
#include <QJsonDocument>
#include <QJsonObject>
#include <QPair>
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
#include "irisgl/import/parsecensus.h"

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

/// The import-settings record recorded for the content at `oid` — the inverse
/// lookup MeshBakeStore::settingsHashFor documents. Several rows can name one
/// object (an import records the model under both the Object and the Mesh
/// member); they carry the SAME import record, so the first row that has one
/// wins and a row with none reads as identity.
QJsonObject importRecordForOid(QSqlDatabase conn, const QString &oid)
{
    if (oid.isEmpty()) return QJsonObject();
    QSqlQuery query(conn);
    // NEWEST FIRST, as the header promises: with no ORDER BY the answer was
    // SQLite's index order, so "the default variant for this content" was not
    // even stable between two runs over one library (the second read's F6).
    query.prepare("SELECT A.properties FROM asset_files AF "
                  "JOIN assets A ON A.guid = AF.asset_guid WHERE AF.oid = ? "
                  "ORDER BY AF.rowid DESC");
    query.addBindValue(oid);
    if (!query.exec()) return QJsonObject();
    QJsonObject best;
    while (query.next()) {
        const QJsonObject props =
            QJsonDocument::fromJson(query.value(0).toByteArray()).object();
        const QJsonObject record = props.value(QStringLiteral("import")).toObject();
        if (record.isEmpty()) continue;
        if (best.isEmpty()) best = props;
        if (!record.value(QStringLiteral("settings")).toObject().isEmpty()) return props;
    }
    return best;
}

/// The import record of ONE row, by guid — the unambiguous half of the lookup
/// above.
///
/// UP TO THE OWNER when the row itself has none: an import writes the record on
/// the OBJECT row, and the thing a scene node names is the MESH MEMBER row
/// under it. A member's import settings are its owner's, by construction — they
/// came out of the same import of the same file — so the walk is to `parent`
/// and is bounded (the catalog is two levels deep; the loop guards anyway).
/// A row with no record anywhere above it reads as identity, exactly as every
/// row imported before the import dialog does.
QJsonObject importRecordForGuid(QSqlDatabase conn, const QString &guid)
{
    QString at = guid;
    for (int hop = 0; hop < 8 && !at.isEmpty(); ++hop) {
        QSqlQuery query(conn);
        query.prepare("SELECT properties, parent FROM assets WHERE guid = ?");
        query.addBindValue(at);
        if (!query.exec() || !query.next()) return QJsonObject();
        const QJsonObject props = QJsonDocument::fromJson(query.value(0).toByteArray()).object();
        if (!props.value(QStringLiteral("import")).toObject().isEmpty()) return props;
        at = query.value(1).toString();
    }
    return QJsonObject();
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

namespace
{
/// Memo for the two lookups below: one open asks for the same path once per
/// mesh node. Cleared with the model cache (clear()).
QMutex sSettingsLock;
QHash<QString, QPair<QString, iris::ImportTransform>> sSettingsCache;

QPair<QString, iris::ImportTransform> resolveSettings(QSqlDatabase conn, const QString &root,
                                                     const QString &sourcePath,
                                                     const QString &assetGuid)
{
    const QString key = sourcePath + QLatin1Char('|') + assetGuid;
    {
        QMutexLocker locked(&sSettingsLock);
        const auto hit = sSettingsCache.constFind(key);
        if (hit != sSettingsCache.constEnd()) return hit.value();
    }
    QPair<QString, iris::ImportTransform> out(iris::ImportSettings::identityHash(),
                                              iris::ImportTransform());
    const QString oid = oidFromStorePath(root, sourcePath);
    if (!oid.isEmpty()) {
        const QJsonObject props = assetGuid.isEmpty() ? importRecordForOid(conn, oid)
                                                      : importRecordForGuid(conn, assetGuid);
        const QJsonObject record = props.value(QStringLiteral("import")).toObject();
        const QJsonObject settings = record.value(QStringLiteral("settings")).toObject();
        const iris::ImportSettings parsed = iris::ImportSettings::fromJson(settings);
        // The file's own declaration, already measured at import: a unit
        // OVERRIDE then costs no probe parse (irisgl/import/scenesource.h).
        const double declared = props.value(QStringLiteral("metadata")).toObject()
                                     .value(QStringLiteral("unitScale")).toDouble(0.0);
        out.first = parsed.hash();
        out.second = parsed.transform(declared);
    }
    QMutexLocker locked(&sSettingsLock);
    sSettingsCache.insert(key, out);
    return out;
}
}   // namespace

QString settingsHashFor(QSqlDatabase conn, const QString &root, const QString &sourcePath,
                        const QString &assetGuid)
{
    return resolveSettings(conn, root, sourcePath, assetGuid).first;
}

QString settingsHashFor(const QString &sourcePath, const QString &assetGuid)
{
    return settingsHashFor(QSqlDatabase::database(), AssetStorePaths::root(), sourcePath,
                           assetGuid);
}

iris::ImportTransform transformFor(QSqlDatabase conn, const QString &root,
                                   const QString &sourcePath, const QString &assetGuid)
{
    return resolveSettings(conn, root, sourcePath, assetGuid).second;
}

iris::ImportTransform transformFor(const QString &sourcePath, const QString &assetGuid)
{
    return transformFor(QSqlDatabase::database(), AssetStorePaths::root(), sourcePath, assetGuid);
}


iris::PrewarmItem planFor(QSqlDatabase conn, const QString &root, const QString &sourcePath,
                          const QString &assetGuid)
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
    // NEWEST FIRST, and every candidate row — not `LIMIT 1`. The name is
    // derived from the SOURCE oid only, so every producer generation of the
    // same model registers under the identical name; an upgraded library
    // holds the stale generations in front of the fresh one, and a bare
    // LIMIT 1 returned the OLDEST row forever: fingerprint mismatch, silent
    // permanent assimp fallback, and assets.bakeAll could never converge
    // (its re-bake deduped to the already-present object behind the stale
    // row). Found by the 2026-09-06 pre-push gate when the sockets merge
    // bumped the producer id. read() validates the fingerprint, so the scan
    // stops at the first row that is actually the current generation.
    const QString settings = settingsHashFor(conn, root, sourcePath, assetGuid);
    item.transform = transformFor(conn, root, sourcePath, assetGuid);
    QSqlQuery query(conn);
    query.prepare("SELECT AF.oid, F.ext FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid "
                  "WHERE AF.role = ? AND AF.name = ? ORDER BY AF.rowid DESC");
    query.addBindValue(iris::MeshBake::casRole());
    query.addBindValue(iris::MeshBake::fileNameFor(sourceOid, settings));
    if (!query.exec()) return item;

    const QString fingerprint = iris::MeshBake::fingerprintFor(sourceOid, settings);
    int stale = 0;
    while (query.next()) {
        const QString path = AssetStorePaths::objectPathIn(root, query.value(0).toString(),
                                                           query.value(1).toString());
        if (!QFileInfo::exists(path)) continue;
        if (!iris::MeshBake::headerMatches(path, fingerprint)) { ++stale; continue; }
        item.bakePath = path;
        item.bakeFingerprint = fingerprint;
        return item;
    }
    // SAY SO. A bake exists for this model but was produced by a different
    // importer generation, so the open path silently falls back to parsing the
    // source — and since 2026-09-08 that parse can produce DIFFERENT GEOMETRY
    // from the bake it replaced: iris::ImportFlags::Canonical gained
    // aiProcess_GlobalScale, so an FBX that declares centimetres now imports
    // 100x smaller than the bake in the library holds. Ships-as-new-app: there
    // is no migration, and the node transforms in an already-saved scene were
    // authored against the old size, so RE-IMPORT is the honest path. This
    // line is how the owner finds out, once per model per session, instead of
    // wondering why an old scene's character shrank.
    if (stale > 0) {
        static QSet<QString> told;
        static QMutex tellLock;
        QMutexLocker locked(&tellLock);
        if (!told.contains(sourceOid)) {
            told.insert(sourceOid);
            irisLog(QStringLiteral("mesh bake: '%1' has %2 bake(s) from an older importer "
                                   "generation — parsing the source instead. If this model is an "
                                   "FBX imported before the unit-scale fix (2026-09-08) it will "
                                   "come in at its file's declared units, which is NOT the size "
                                   "the saved scene was built around: re-import it.")
                        .arg(QFileInfo(sourcePath).fileName())
                        .arg(stale));
        }
    }
    return item;
}

iris::PrewarmItem planFor(const QString &sourcePath, const QString &assetGuid)
{
    return planFor(QSqlDatabase::database(), AssetStorePaths::root(), sourcePath, assetGuid);
}

iris::BakedModelPtr load(const QString &sourcePath, const QString &assetGuid)
{
    if (sourcePath.isEmpty()) return iris::BakedModelPtr();
    // KEYED BY (path, settings): identical bytes imported twice under different
    // settings are two different geometries and must not share a cache slot.
    const QString cacheKey =
        sourcePath + QLatin1Char('|') + settingsHashFor(sourcePath, assetGuid);
    {
        QMutexLocker locked(&sLock);
        const auto hit = sCache.constFind(cacheKey);
        if (hit != sCache.constEnd()) return hit.value();
    }

    const iris::PrewarmItem item = planFor(sourcePath, assetGuid);
    iris::BakedModelPtr result;
    if (!item.bakePath.isEmpty()) {
        iris::MeshBake::Model model = iris::MeshBake::read(item.bakePath, item.bakeFingerprint);
        if (model.valid)
            result = std::make_shared<const iris::MeshBake::Model>(std::move(model));
    }
    // THE HIT/MISS SPLIT (app.openStats()), reported to the SAME census the
    // prewarm worker reports to (irisgl/import/parsecensus.h) — an open reads
    // bakes from both threads and one number has to cover both.
    iris::ParseCensus::recordBake(result != nullptr);

    QMutexLocker locked(&sLock);
    // Negative results are cached too: a model with no bake must not re-query
    // the catalog for every mesh node that references it.
    if (sScopeDepth > 0) sCache.insert(cacheKey, result);
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
    {
        QMutexLocker locked(&sSettingsLock);
        sSettingsCache.clear();
    }
    QMutexLocker locked(&sLock);
    sCache.clear();
}


bool isFresh(QSqlDatabase conn, const QString &root, const QString &sourcePath,
             const QString &assetGuid)
{
    const iris::PrewarmItem item = planFor(conn, root, sourcePath, assetGuid);
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

    // BY THE ROW, not by the path (IMPORT-1): this asset's import settings are
    // half the bake key, and a sibling row over the same bytes may want other
    // settings. bakeAsset knows which row it is baking for; the content-first
    // sweep behind assets.bakeAll does not, and gets that content's default.
    if (isFresh(conn, root, sourcePath, guid)) return true;
    if (neededOut) *neededOut = true;
    if (dryRun) return true;
    if (!bakeSource(conn, root, sourcePath, errorOut, guid)) return false;
    clear();
    return true;
}

bool bakeSource(QSqlDatabase conn, const QString &root, const QString &sourcePath,
                QString *errorOut, const QString &assetGuid)
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

    const QString settings = settingsHashFor(conn, root, sourcePath, assetGuid);
    iris::MeshBake::Model model = iris::MeshBake::buildFromFile(
        sourcePath, iris::MeshBake::fingerprintFor(sourceOid, settings), scratch.path(),
        transformFor(conn, root, sourcePath, assetGuid));
    if (!model.valid) {
        if (errorOut)
            *errorOut = QStringLiteral("could not parse '%1' for baking").arg(sourcePath);
        return false;
    }

    const QString bakeName = iris::MeshBake::fileNameFor(sourceOid, settings);
    const QString bakePath = QDir(scratch.path()).filePath(bakeName);
    if (!iris::MeshBake::write(bakePath, model, errorOut)) return false;
    return recordBake(conn, root, sourceOid, bakePath, errorOut);
}

// --- Lazy re-bake -----------------------------------------------------------

namespace
{

QVector<BakeTarget> sQueue;         ///< UI thread only; a PAIR, not a path
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

    const BakeTarget target = sQueue.takeFirst();
    const QString sourcePath = target.path;
    const QString root = AssetStorePaths::root();
    const QString sourceOid = oidFromStorePath(root, sourcePath);
    if (sourceOid.isEmpty()) { pumpQueue(); return; }
    if (isFresh(QSqlDatabase::database(), root, sourcePath, target.assetGuid)) {
        pumpQueue();
        return;
    }

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
    // The settings are resolved HERE, on the UI thread, because the lookup is a
    // catalog read and QSqlDatabase connections are per-thread; the worker gets
    // plain values (the same split planFor/PrewarmItem already uses).
    const QString settings =
        settingsHashFor(QSqlDatabase::database(), root, sourcePath, target.assetGuid);
    const iris::ImportTransform xf =
        transformFor(QSqlDatabase::database(), root, sourcePath, target.assetGuid);
    watcher->setFuture(QtConcurrent::run([sourcePath, sourceOid, settings, xf]() -> BakeOutput {
        BakeOutput out;
        out.sourceOid = sourceOid;
        auto dir = std::make_shared<QTemporaryDir>();
        if (!dir->isValid()) return out;
        iris::MeshBake::Model model = iris::MeshBake::buildFromFile(
            sourcePath, iris::MeshBake::fingerprintFor(sourceOid, settings), dir->path(), xf);
        if (!model.valid) return out;
        const QString path =
            QDir(dir->path()).filePath(iris::MeshBake::fileNameFor(sourceOid, settings));
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

int scheduleBakes(const QVector<BakeTarget> &targets)
{
    if (!QCoreApplication::instance()) return 0;
    sCancelled = false;
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    int queued = 0;
    for (const BakeTarget &target : targets) {
        if (target.path.isEmpty()) continue;
        bool already = false;
        for (const BakeTarget &q : sQueue)
            if (q.path == target.path && q.assetGuid == target.assetGuid) { already = true; break; }
        if (already) continue;
        if (oidFromStorePath(root, target.path).isEmpty()) continue;
        if (isFresh(conn, root, target.path, target.assetGuid)) continue;
        sQueue.append(target);
        ++queued;
    }
    if (queued) pumpQueue();
    return queued;
}

int scheduleBakes(const QStringList &paths)
{
    QVector<BakeTarget> targets;
    targets.reserve(paths.size());
    for (const QString &path : paths) targets.append({ path, QString() });
    return scheduleBakes(targets);
}

int pendingBakes() { return sQueue.size() + (sBakeInFlight ? 1 : 0); }

void cancelPendingBakes()
{
    sCancelled = true;
    sQueue.clear();
}

QVector<BakeTarget> modelBakesNeeded(QSqlDatabase conn, const QString &root)
{
    QVector<BakeTarget> out;
    // Every recorded file whose DISPLAY NAME is a model, with the ROW that
    // names it — one entry per distinct (content, import settings) pair, since
    // that pair is what a bake is keyed on. Deduplicating by content alone
    // (what this did until the second read's F3) built one variant and left
    // every other row parsing on every open, forever.
    QSqlQuery query(conn);
    query.prepare("SELECT AF.oid, AF.name, F.ext, AF.asset_guid FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.role <> ? "
                  "ORDER BY AF.rowid");
    query.addBindValue(iris::MeshBake::casRole());
    if (!query.exec()) return out;
    QSet<QString> seen;          // "<oid>|<settingsHash>"
    QHash<QString, QString> pathForOid;
    while (query.next()) {
        const QString oid = query.value(0).toString();
        if (!Constants::MODEL_EXTS.contains(
                QFileInfo(query.value(1).toString()).suffix().toLower()))
            continue;
        QString path = pathForOid.value(oid);
        if (path.isEmpty()) {
            path = AssetStorePaths::objectPathIn(root, oid, query.value(2).toString());
            if (!QFileInfo::exists(path)) { pathForOid.insert(oid, QString()); continue; }
            pathForOid.insert(oid, path);
        }
        if (path.isEmpty()) continue;   // offline/purged object, already judged
        const QString guid = query.value(3).toString();
        const QString key = oid + QLatin1Char('|') + settingsHashFor(conn, root, path, guid);
        if (seen.contains(key)) continue;
        seen.insert(key);
        if (isFresh(conn, root, path, guid)) continue;
        out.append({ path, guid });
    }
    return out;
}

QStringList modelSourcesNeedingBake(QSqlDatabase conn, const QString &root)
{
    QStringList out;
    for (const BakeTarget &target : modelBakesNeeded(conn, root))
        if (!out.contains(target.path)) out.append(target.path);
    return out;
}

}   // namespace MeshBakeStore
