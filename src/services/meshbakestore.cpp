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
#include <QHash>
#include <QSet>
#include <QMutex>
#include <QUuid>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <memory>
#include <atomic>
#include <QtConcurrent/QtConcurrentRun>
#include <QThreadPool>
#include <QThread>
#include <QFuture>
#include <QPointer>

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
/// THE CACHES' GENERATION (STAGE-CLOSE-TRIAGE-1 F8): bumped by clear(). A lookup reads the catalog
/// OUTSIDE the locks (the boot sweep does it on its worker), so a commit's clear() on the UI thread can
/// land between that read and the lookup's insert — which would then plant the pre-commit answer (an
/// old settings hash, a missing bake) for the rest of the session. A lookup inserts only when the
/// generation it started under is still current. The settings cache is guarded by clear() bumping it
/// under the lock the insert checks; the model cache (sCache) is correct by ORDER — the bump precedes
/// sCache.clear(), so any insert after that clear sees the new generation. A lookup INSIDE a plan's
/// pinned snapshot starts under the plan's generation, not its own (PinGeneration, I7).
std::atomic<quint64> sCacheGeneration { 0 };
/// A PLAN'S PINNED GENERATION (I7): set for the life of a plan whose lookups run inside one
/// still-stepping statement (modelBakesNeeded, staleJobsFor). Per thread: the sweep plans on its
/// worker while the UI thread looks up on its own.
thread_local bool tGenerationPinned = false;
thread_local quint64 tPinnedGeneration = 0;
struct PinGeneration
{
    bool outer = !tGenerationPinned;
    PinGeneration()
    {
        if (outer) { tGenerationPinned = true; tPinnedGeneration = sCacheGeneration.load(); }
    }
    ~PinGeneration() { if (outer) tGenerationPinned = false; }
};
/// The generation a lookup starts under: the plan's pinned one inside a plan, else now.
quint64 lookupGeneration()
{
    return tGenerationPinned ? tPinnedGeneration : sCacheGeneration.load();
}


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
///
/// `staged` non-null = the bytes are ALREADY in the store and already flushed,
/// put there by the bake worker (FSYNC-2), so publishing them here is a rename.
/// Null = the synchronous path (Preferences' bake-all), which ingests here.
bool recordBake(QSqlDatabase conn, const QString &root, const QString &sourceOid,
                const QString &bakePath, QString *errorOut,
                AssetCas::Staged *staged = nullptr)
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

    // THE OLD GENERATION IS RETIRED FIRST (BAKE-RETIRE-1, 2026-09-22).
    //
    // A bake's file NAME is `<sourceOid16>-<settingsHash>.jmb`
    // (MeshBake::fileNameFor) — it names the CONTENT and the SETTINGS and says
    // nothing about the PRODUCER, because the producer lives in the fingerprint
    // INSIDE the file. `asset_files`' primary key is (asset_guid, role, name)
    // and `AssetCas::ingestFile` inserts OR IGNOREs. So after a kFormatVersion
    // bump the rebuilt bake — new bytes, new oid, the SAME name — was SILENTLY
    // DROPPED on any library that already had one: `planFor` kept answering
    // with the stale oid, `MeshBake::read` refused its fingerprint, and every
    // caller fell back to a parse... except the baked-asset callers that have no
    // parse to fall back to, which is every shipped primitive since ATOM P2
    // (they drew NOTHING, and `PrimitiveAssets::ensureSeeded` re-baked all
    // fourteen into an object nothing linked, on every boot, forever). No gate
    // saw it because every test home is fresh.
    //
    // DELETE + INSERT, not an UPDATE, and for the reason AssetCas::
    // moveSourcePointer states at its own DELETE: the refcount triggers are
    // AFTER INSERT and AFTER DELETE on asset_files, so this is what hands the
    // superseded object back to `assets.gc` instead of inflating its refcount
    // forever. Scoped to (this owner, 'bake', THIS name) and only when the oid
    // really differs: a second row under another name is another SETTINGS
    // variant of the same content and is nobody's business here
    // (modelBakesNeeded's "two rows over one object with different settings are
    // two different bakes").
    for (const QString &ownerGuid : owners) {
        QSqlQuery stale(conn);
        stale.prepare("DELETE FROM asset_files WHERE asset_guid = ? AND role = ? "
                      "AND name = ? AND oid <> ?");
        stale.addBindValue(ownerGuid);
        stale.addBindValue(iris::MeshBake::casRole());
        stale.addBindValue(bakeName);
        stale.addBindValue(staged && !staged->oid.isEmpty() ? staged->oid
                                                            : AssetCas::hashFile(bakePath));
        if (!stale.exec()) {
            if (errorOut) *errorOut = stale.lastError().text();
            return false;
        }
    }

    for (const QString &ownerGuid : owners) {
        QString oid;
        if (staged && !staged->oid.isEmpty()) {
            // Every owner records the SAME content: the first publish renames
            // the staged temp into place, the rest find the object already
            // there and only write their rows.
            staged->role = iris::MeshBake::casRole();
            staged->name = bakeName;
            if (!AssetCas::commitStaged(conn, root, ownerGuid, *staged, errorOut)) return false;
        } else if (!AssetCas::ingestFile(conn, root, bakePath, ownerGuid,
                                         iris::MeshBake::casRole(), bakeName, &oid, errorOut)) {
            return false;
        }
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
    const quint64 generation = lookupGeneration();
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
    if (sCacheGeneration.load() == generation) sSettingsCache.insert(key, out);
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
    QSqlQuery query(conn);
    query.prepare("SELECT AF.oid, F.ext, F.size FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid "
                  "WHERE AF.role = ? AND AF.name = ? ORDER BY AF.rowid DESC");
    query.addBindValue(iris::MeshBake::casRole());
    query.addBindValue(iris::MeshBake::fileNameFor(sourceOid, settings));
    if (!query.exec()) return item;

    const QString fingerprint = iris::MeshBake::fingerprintFor(sourceOid, settings);
    while (query.next()) {
        const QString path = AssetStorePaths::objectPathIn(root, query.value(0).toString(),
                                                           query.value(1).toString());
        // CHEAP, AND ENOUGH (FORWARD-ONLY-1 D3): the object's size against
        // the catalog's record of it (a truncated or rewritten object differs)
        // and the header's producer fingerprint — no payload is read.
        const QFileInfo info(path);
        if (!info.exists()) continue;
        if (!query.value(2).isNull() && info.size() != query.value(2).toLongLong()) continue;
        if (!iris::MeshBake::headerMatches(path, fingerprint)) continue;
        item.bakePath = path;
        item.bakeFingerprint = fingerprint;
        return item;
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
    const quint64 generation = lookupGeneration();
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
    if (sScopeDepth > 0 && sCacheGeneration.load() == generation) sCache.insert(cacheKey, result);
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
        sCacheGeneration.fetch_add(1);
        sSettingsCache.clear();
    }
    QMutexLocker locked(&sLock);
    sCache.clear();
}


bool isFresh(QSqlDatabase conn, const QString &root, const QString &sourcePath,
             const QString &assetGuid)
{
    // The plan's own test (size + header fingerprint), never a payload read:
    // a sweep over a library asks this of every model.
    return !planFor(conn, root, sourcePath, assetGuid).bakePath.isEmpty();
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
    if (type != ModelTypes::Object && type != ModelTypes::Mesh && type != ModelTypes::Animation) {
        // Not a model or a clip: nothing to bake, and not an error.
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

    // A CLIP ROW'S BAKE is its clip bake (SHIPPED-BAKES-1).
    if (type == ModelTypes::Animation) {
        if (!clipBakePath(conn, root, sourcePath).isEmpty()) return true;
        if (neededOut) *neededOut = true;
        if (dryRun) return true;
        if (!bakeSource(conn, root, sourcePath, errorOut, guid)) return false;
        clear();
        return true;
    }

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
    iris::ParseCensus::BakeBuildScope building;   // the bake's own parse, not a read's
    if (!scratch.isValid()) {
        if (errorOut) *errorOut = QStringLiteral("cannot create a bake staging directory");
        return false;
    }

    if (isClipContent(conn, root, sourcePath)) {
        // THE CLIP BAKE (SHIPPED-BAKES-1): content only, built at identity.
        const iris::MeshBake::Clip clip = iris::MeshBake::buildClipFromFile(
            sourcePath, iris::MeshBake::clipFingerprintFor(sourceOid));
        if (!clip.valid) {
            if (errorOut)
                *errorOut = QStringLiteral("could not read '%1' for its clip bake").arg(sourcePath);
            return false;
        }
        const QString clipPath =
            QDir(scratch.path()).filePath(iris::MeshBake::clipFileNameFor(sourceOid));
        if (!iris::MeshBake::writeClip(clipPath, clip, errorOut)) return false;
        return recordBake(conn, root, sourceOid, clipPath, errorOut);
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

QVector<BakeTarget> modelBakesNeeded(QSqlDatabase conn, const QString &root,
                                     const QSet<QString> *onlyGuids, QVector<BakeTarget> *allRows)
{
    QVector<BakeTarget> out;
    // ONE CACHE GENERATION FOR THE WHOLE PLAN (I7): the lookups below run inside this statement's
    // still-stepping SELECT, whose WAL read snapshot is pinned until it finishes — a lookup that
    // sampled the generation AFTER a commit's clear() would still read the pre-commit snapshot and
    // plant it as current. Sampled once, here, before the statement, every insert the plan makes
    // is dropped if any clear() happened since.
    const PinGeneration pinned;
    // Every recorded file whose DISPLAY NAME is a model, with the ROW that
    // names it — one entry per distinct (content, import settings) pair, since
    // that pair is what a bake is keyed on. Deduplicating by content alone
    // (what this did until the second read's F3) built one variant and left
    // every other row parsing on every open, forever.
    QSqlQuery query(conn);
    // THE GUID FILTER IS THE QUERY'S (D3): an archive import asks about the
    // rows it brought, and must not walk the whole library to find them.
    // The ROW'S TYPE rides along: a clip row's content gets a CLIP bake
    // (SHIPPED-BAKES-1), every other model file a model bake.
    QString sql = QStringLiteral("SELECT AF.oid, AF.name, F.ext, AF.asset_guid, A.type FROM asset_files AF "
                                 "LEFT JOIN files F ON AF.oid = F.oid "
                                 "LEFT JOIN assets A ON A.guid = AF.asset_guid WHERE AF.role <> ?");
    if (onlyGuids) {
        if (onlyGuids->isEmpty()) return out;
        QStringList marks;
        for (int i = 0; i < onlyGuids->size(); ++i) marks << QStringLiteral("?");
        sql += QStringLiteral(" AND AF.asset_guid IN (%1)").arg(marks.join(QLatin1Char(',')));
    }
    sql += QStringLiteral(" ORDER BY AF.rowid");
    query.prepare(sql);
    query.addBindValue(iris::MeshBake::casRole());
    if (onlyGuids)
        for (const QString &guid : *onlyGuids) query.addBindValue(guid);
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
        if (allRows) allRows->append({ path, guid, query.value(4).toInt() == static_cast<int>(ModelTypes::Animation) });
        if (query.value(4).toInt() == static_cast<int>(ModelTypes::Animation)) {
            const QString key = oid + QStringLiteral("|clip");
            if (seen.contains(key)) continue;
            seen.insert(key);
            if (!clipBakePath(conn, root, path).isEmpty()) continue;
            out.append({ path, guid, true });
            continue;
        }
        const QString key = oid + QLatin1Char('|') + settingsHashFor(conn, root, path, guid);
        if (seen.contains(key)) continue;
        seen.insert(key);
        if (isFresh(conn, root, path, guid)) continue;
        out.append({ path, guid, false });
    }
    return out;
}

BakeJob prepareBake(QSqlDatabase conn, const QString &root, const BakeTarget &target)
{
    BakeJob job;
    job.sourceOid = oidFromStorePath(root, target.path);
    if (job.sourceOid.isEmpty()) return job;
    job.path = target.path;
    job.storeRoot = root;
    job.clip = target.clip;
    if (job.clip) return job;   // a clip bake is built at identity: no settings, no transform
    job.settings = settingsHashFor(conn, root, target.path, target.assetGuid);
    job.transform = transformFor(conn, root, target.path, target.assetGuid);
    return job;
}

BakeResult runBake(const BakeJob &job)
{
    BakeResult out;
    out.sourceOid = job.sourceOid;
    if (job.path.isEmpty()) { out.error = QStringLiteral("not a store object"); return out; }
    auto dir = std::make_shared<QTemporaryDir>();
    if (!dir->isValid()) { out.error = QStringLiteral("cannot create a bake staging directory"); return out; }
    iris::ParseCensus::BakeBuildScope building;   // the bake's own parse, not an open's
    QString path;
    if (job.clip) {
        const iris::MeshBake::Clip clip = iris::MeshBake::buildClipFromFile(
            job.path, iris::MeshBake::clipFingerprintFor(job.sourceOid));
        if (!clip.valid) {
            out.error = QStringLiteral("could not read '%1' for its clip bake")
                            .arg(QFileInfo(job.path).fileName());
            return out;
        }
        path = QDir(dir->path()).filePath(iris::MeshBake::clipFileNameFor(job.sourceOid));
        if (!iris::MeshBake::writeClip(path, clip, &out.error)) return out;
    } else {
        iris::MeshBake::Model model = iris::MeshBake::buildFromFile(
            job.path, iris::MeshBake::fingerprintFor(job.sourceOid, job.settings), dir->path(),
            job.transform);
        if (!model.valid) {
            out.error = QStringLiteral("could not parse '%1' for baking").arg(QFileInfo(job.path).fileName());
            return out;
        }
        path = QDir(dir->path()).filePath(iris::MeshBake::fileNameFor(job.sourceOid, job.settings));
        if (!iris::MeshBake::write(path, model, &out.error)) return out;
    }
    out.dir = dir;
    out.path = path;
    // The store's half of the write, here and not on the database thread:
    // hash, copy into objects/ under a temp name, flush (FSYNC-2).
    out.staged.srcPath = path;
    out.staged.role = iris::MeshBake::casRole();
    out.staged.name = QFileInfo(path).fileName();
    if (AssetCas::stage(job.storeRoot, out.staged)) {
        QVector<AssetCas::Staged> batch{ out.staged };
        AssetCas::flushStaged(batch);
        out.staged = batch.first();
    } else {
        out.staged = AssetCas::Staged();   // the commit ingests synchronously
    }
    return out;
}

bool commitBake(QSqlDatabase conn, const QString &root, BakeResult &result, QString *errorOut)
{
    bool ok = false;
    if (!result.path.isEmpty()) {
        ok = recordBake(conn, root, result.sourceOid, result.path, errorOut, &result.staged);
        if (ok) clear();
    } else if (errorOut) {
        *errorOut = result.error;
    }
    // A failed or unrecorded bake leaves the temp the worker staged; it names
    // no object and no row (discardStaged is a no-op once committed).
    QVector<AssetCas::Staged> leftover{ result.staged };
    AssetCas::discardStaged(leftover);
    return ok;
}

QVector<BakeJob> staleJobsFor(QSqlDatabase conn, const QString &root, const QStringList &paths)
{
    QVector<BakeJob> jobs;
    const PinGeneration pinned;   // the lookups run inside `rows`' pinned snapshot (I7)
    QSet<QString> seen;   // "<oid>|<settingsHash>"
    for (const QString &path : paths) {
        const QString oid = oidFromStorePath(root, path);
        if (oid.isEmpty() || !QFileInfo::exists(path)) continue;   // no source: missing
        QSqlQuery rows(conn);
        rows.prepare("SELECT DISTINCT AF.asset_guid, A.type FROM asset_files AF "
                     "LEFT JOIN assets A ON A.guid = AF.asset_guid WHERE AF.oid = ? AND AF.role <> ?");
        rows.addBindValue(oid);
        rows.addBindValue(iris::MeshBake::casRole());
        if (!rows.exec()) continue;
        while (rows.next()) {
            const QString guid = rows.value(0).toString();
            if (rows.value(1).toInt() == static_cast<int>(ModelTypes::Animation)) {
                // A CLIP's content: its clip bake (SHIPPED-BAKES-1).
                const QString key = oid + QStringLiteral("|clip");
                if (seen.contains(key)) continue;
                seen.insert(key);
                if (!clipBakePath(conn, root, path).isEmpty()) continue;   // fresh
                BakeJob job = prepareBake(conn, root, { path, guid, true });
                if (!job.path.isEmpty()) jobs.append(job);
                continue;
            }
            const QString key = oid + QLatin1Char('|') + settingsHashFor(conn, root, path, guid);
            if (seen.contains(key)) continue;
            seen.insert(key);
            if (!planFor(conn, root, path, guid).bakePath.isEmpty()) continue;   // fresh
            BakeJob job = prepareBake(conn, root, { path, guid });
            if (!job.path.isEmpty()) jobs.append(job);
        }
    }
    return jobs;
}

int rebuildPumped(const QVector<BakeJob> &jobs, const std::function<void(int, int)> &progress)
{
    if (jobs.isEmpty()) return 0;
    std::atomic<int> at { -1 };
    std::atomic<bool> done { false };
    QVector<BakeResult> results(jobs.size());
    QFuture<void> future = QtConcurrent::run([&]() {
        struct Finish { std::atomic<bool> &flag; ~Finish() { flag.store(true); } } finish{ done };
        for (int i = 0; i < jobs.size(); ++i) {
            at.store(i);
            results[i] = runBake(jobs[i]);
        }
    });
    int reported = -1;
    while (!done.load()) {
        const int now = at.load();
        if (now != reported && now >= 0) { reported = now; if (progress) progress(now, jobs.size()); }
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 20);
        if (!done.load()) QThread::msleep(5);
    }
    future.waitForFinished();
    int failed = 0;
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    for (BakeResult &result : results) {
        QString error;
        if (!commitBake(conn, root, result, &error)) {
            ++failed;
            irisLog(QStringLiteral("mesh bake: a stale bake could not be rebuilt (%1)").arg(error));
        }
    }
    return failed;
}

namespace {
QFuture<void> sBackground;
// THE ASYNC BAKES (STAGE-CLOSE-TRIAGE-1 F3). Everything here but the worker's runBake loop is the
// database thread's: the pending map, the listeners, the commits.
QHash<QString, int> sPending;                       ///< model file -> async bakes in flight over it
int sPendingJobs = 0;
/// Moved by stopBackgroundRebuild (a reset, a shutdown). ATOMIC: the async worker reads it between jobs
/// and stops there (STAGE-CLOSE-TRIAGE-1 F5); the continuation drops a landing whose epoch moved.
std::atomic<quint64> sAsyncEpoch { 0 };
QList<QFuture<QVector<BakeResult>>> sAsyncFutures;  ///< joined by stopBackgroundRebuild
struct Listener
{
    QPointer<QObject> context;
    std::function<void(const QVector<LandedBake> &)> fn;
};
QVector<Listener> sListeners;
std::atomic<bool> sBackgroundStop { false };
QThreadPool *backgroundPool()
{
    static QThreadPool *pool = [] {
        auto *p = new QThreadPool;
        p->setMaxThreadCount(1);
        p->setThreadPriority(QThread::LowestPriority);
        return p;
    }();
    return pool;
}
}   // namespace

void startBackgroundRebuild()
{
    if (backgroundRebuildRunning() || !QCoreApplication::instance()) return;
    // JAHSHAKA_BAKE_SWEEP=0: no library sweep (a suite that proves the OPEN
    // rebuilds its own stale bakes needs the sweep out of the race).
    if (qEnvironmentVariableIsSet("JAHSHAKA_BAKE_SWEEP")
        && qEnvironmentVariableIntValue("JAHSHAKA_BAKE_SWEEP") == 0)
        return;
    const QSqlDatabase main = QSqlDatabase::database();
    if (!main.isOpen()) return;
    const QString dbPath = main.databaseName();
    const QString root = AssetStorePaths::root();
    sBackgroundStop.store(false);
    // THE SWEEP IS PLANNED ON THE WORKER TOO (TC2-LIBRARY-BOOT-READ, 2026-10-10): the plan reads
    // every model file of the library and every row naming it (modelBakesNeeded: 22.6 MB of a
    // 10k-asset library's 130 MB database, measured) — it ran on the UI thread inside the main
    // window's constructor, at every boot, to find nothing stale. The catalog is per thread, so
    // the worker reads through its OWN read connection to the same file; only the commits come
    // back to this (the database) thread, as before.
    sBackground = QtConcurrent::run(backgroundPool(), [dbPath, root]() {
        QVector<BakeJob> jobs;
        {
            const QString name = QStringLiteral("BakeSweep-%1")
                                     .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
            {
                QSqlDatabase conn = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), name);
                conn.setDatabaseName(dbPath);
                conn.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=5000"));
                if (conn.open()) {
                    for (const BakeTarget &target : modelBakesNeeded(conn, root)) {
                        if (sBackgroundStop.load()) break;
                        if (!QFileInfo::exists(target.path)) continue;
                        BakeJob job = prepareBake(conn, root, target);
                        if (!job.path.isEmpty()) jobs.append(job);
                    }
                    conn.close();
                }
            }
            QSqlDatabase::removeDatabase(name);
        }
        if (jobs.isEmpty() || sBackgroundStop.load()) return;
        irisLog(QStringLiteral("mesh bake: rebuilding %1 stale bake(s) in the background").arg(jobs.size()));
        for (const BakeJob &job : jobs) {
            if (sBackgroundStop.load()) return;
            auto result = std::make_shared<BakeResult>(runBake(job));
            QMetaObject::invokeMethod(QCoreApplication::instance(), [result, root]() {
                QString error;
                if (!commitBake(QSqlDatabase::database(), root, *result, &error))
                    irisLog(QStringLiteral("mesh bake: background rebuild failed (%1)").arg(error));
            }, Qt::QueuedConnection);
        }
    });
}

void stopBackgroundRebuild()
{
    sBackgroundStop.store(true);
    if (sBackground.isValid() && !sBackground.isFinished()) sBackground.waitForFinished();
    // THE ASYNC BAKES TOO (a reset, a shutdown). The epoch moves FIRST: each worker stops between
    // jobs on it (F5), so the join below waits for the job IN FLIGHT only — never the rest of a
    // multi-model paste — and every landing still queued to this thread is dropped by it (its
    // `done` told why: F7), so nothing lands in a library that is no longer the one it was planned
    // against. (A single job has no cancel point: MeshBake::buildFromFile runs to its end.)
    ++sAsyncEpoch;
    for (QFuture<QVector<BakeResult>> &future : sAsyncFutures)
        if (!future.isFinished()) future.waitForFinished();
    sAsyncFutures.clear();
    // A test hold left on: its parked commits are released into the moved epoch, which discards them.
    holdCommits(false);
}

bool backgroundRebuildRunning()
{
    return sBackground.isValid() && !sBackground.isFinished();
}

// ---- the clip bake (SHIPPED-BAKES-1) ----------------------------------------

bool isClipContent(QSqlDatabase conn, const QString &root, const QString &sourcePath)
{
    const QString oid = oidFromStorePath(root, sourcePath);
    if (oid.isEmpty()) return false;
    QSqlQuery query(conn);
    query.prepare("SELECT 1 FROM asset_files AF JOIN assets A ON A.guid = AF.asset_guid "
                  "WHERE AF.oid = ? AND A.type = ? LIMIT 1");
    query.addBindValue(oid);
    query.addBindValue(static_cast<int>(ModelTypes::Animation));
    return query.exec() && query.next();
}

QString clipBakePath(QSqlDatabase conn, const QString &root, const QString &sourcePath)
{
    const QString sourceOid = oidFromStorePath(root, sourcePath);
    if (sourceOid.isEmpty()) return QString();
    // NEWEST FIRST, every candidate — planFor's rule, for planFor's reason: a
    // producer bump leaves the stale generation under the same name until the
    // rebuild retires it.
    QSqlQuery query(conn);
    query.prepare("SELECT AF.oid, F.ext, F.size FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid "
                  "WHERE AF.role = ? AND AF.name = ? ORDER BY AF.rowid DESC");
    query.addBindValue(iris::MeshBake::casRole());
    query.addBindValue(iris::MeshBake::clipFileNameFor(sourceOid));
    if (!query.exec()) return QString();
    const QString fingerprint = iris::MeshBake::clipFingerprintFor(sourceOid);
    while (query.next()) {
        const QString path = AssetStorePaths::objectPathIn(root, query.value(0).toString(),
                                                           query.value(1).toString());
        const QFileInfo info(path);
        if (!info.exists()) continue;
        if (!query.value(2).isNull() && info.size() != query.value(2).toLongLong()) continue;
        if (!iris::MeshBake::clipHeaderMatches(path, fingerprint)) continue;
        return path;
    }
    return QString();
}

iris::BakedClipPtr loadClip(const QString &sourcePath)
{
    if (sourcePath.isEmpty()) return iris::BakedClipPtr();
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QString bake = clipBakePath(conn, root, sourcePath);
    iris::BakedClipPtr result;
    if (!bake.isEmpty()) {
        const QString oid = oidFromStorePath(root, sourcePath);
        iris::MeshBake::Clip clip =
            iris::MeshBake::readClip(bake, iris::MeshBake::clipFingerprintFor(oid));
        if (clip.valid) result = std::make_shared<const iris::MeshBake::Clip>(std::move(clip));
    }
    // The same census every bake read reports to (app.openStats()).
    iris::ParseCensus::recordBake(result != nullptr);
    return result;
}

iris::BakedClipPtr ensureClip(const QString &sourcePath)
{
    if (iris::BakedClipPtr have = loadClip(sourcePath)) return have;
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    // THE SAME REBUILD THE OPEN RUNS: the store's own source, on a worker, this
    // thread pumping, committed under every row that names the content. Any
    // model content may carry clips, so the job is built for the path asked
    // about, whatever row type names it.
    BakeJob job = prepareBake(conn, root, { sourcePath, QString(), true });
    if (job.path.isEmpty() || !QFileInfo::exists(job.path)) return iris::BakedClipPtr();
    rebuildPumped({ job }, std::function<void(int, int)>());
    return loadClip(sourcePath);
}

QString currentBakePath(int assetType, const QString &sourcePath, const QString &assetGuid)
{
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    if (assetType == static_cast<int>(ModelTypes::Animation))
        return clipBakePath(conn, root, sourcePath);
    return planFor(conn, root, sourcePath, assetGuid).bakePath;
}

bool ensureFresh(const QString &sourcePath, const QString &assetGuid)
{
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    if (isFresh(conn, root, sourcePath, assetGuid)) return true;
    const QVector<BakeJob> jobs = staleJobsFor(conn, root, { sourcePath });
    if (jobs.isEmpty()) return false;   // no source on disk: the model stays missing
    rebuildPumped(jobs, std::function<void(int, int)>());
    return isFresh(conn, root, sourcePath, assetGuid);
}

namespace {

bool sCommitHold = false;
QVector<std::function<void()>> sHeldCommits;
/// `done(failed, dropped)`: `dropped` names why NOTHING was committed (a reset or shutdown moved the
/// epoch, the store moved), empty when the landing ran.
using DoneFn = std::function<void(int failed, const QString &dropped)>;
void landAsync(const QVector<BakeJob> &jobs, const QVector<LandedBake> &targets,
               const DoneFn &done, quint64 epoch, QVector<BakeResult> results);

/// Bakes `jobs` on a worker; commits them on the database thread when they land, tells the
/// listeners which of `targets` landed, then calls `done(failed)`.
void bakeAsync(const QVector<BakeJob> &jobs, const QVector<LandedBake> &targets, DoneFn done)
{
    for (const BakeJob &job : jobs) ++sPending[job.path];
    sPendingJobs += jobs.size();
    const quint64 epoch = sAsyncEpoch.load();
    QFuture<QVector<BakeResult>> future = QtConcurrent::run([jobs, epoch]() {
        QVector<BakeResult> results;
        results.reserve(jobs.size());
        for (const BakeJob &job : jobs) {
            // A RESET OR SHUTDOWN STOPS THE WORKER BETWEEN JOBS (F5), like the background sweep.
            if (sAsyncEpoch.load() != epoch) break;
            results.append(runBake(job));
        }
        return results;
    });
    sAsyncFutures.removeIf([](const QFuture<QVector<BakeResult>> &f) { return f.isFinished(); });
    sAsyncFutures.append(future);
    future.then(QCoreApplication::instance(), [jobs, targets, done, epoch](QVector<BakeResult> results) {
        landAsync(jobs, targets, done, epoch, std::move(results));
    });
}

void landAsync(const QVector<BakeJob> &jobs, const QVector<LandedBake> &targets,
               const DoneFn &done, quint64 epoch, QVector<BakeResult> results)
{
    // THE TEST HOLD (assets.holdBakes): the finished bake waits here, uncommitted and still pending,
    // until the hold is released — so a suite can place and SAVE inside the window deterministically.
    if (sCommitHold) {
        auto held = std::make_shared<QVector<BakeResult>>(std::move(results));
        sHeldCommits.append([jobs, targets, done, epoch, held]() {
            landAsync(jobs, targets, done, epoch, std::move(*held));
        });
        return;
    }
    {
        for (const BakeJob &job : jobs)
            if (--sPending[job.path] <= 0) sPending.remove(job.path);
        sPendingJobs -= jobs.size();
        // NOTHING IS COMMITTED when the library or the store these were planned against is gone —
        // a reset or shutdown moved the epoch, or the store root moved while the worker staged into
        // the old one (F8: the commit would record objects that are not in the active store). The
        // staged temps go with the results, and `done` is TOLD (F7: a caller waiting on the landing
        // — the avatar module's busy job — ends with the reason instead of waiting forever).
        const QString root = AssetStorePaths::root();
        QString dropped;
        if (epoch != sAsyncEpoch.load())
            dropped = QStringLiteral("the library was reset or closed while its bake was running");
        else if (!jobs.isEmpty() && jobs.first().storeRoot != root)
            dropped = QStringLiteral("the asset store moved while its bake was running — import it again");
        if (!dropped.isEmpty()) {
            for (BakeResult &result : results) {
                QVector<AssetCas::Staged> leftover{ result.staged };
                AssetCas::discardStaged(leftover);
            }
            irisLog(QStringLiteral("mesh bake: %1 bake(s) not committed: %2").arg(jobs.size()).arg(dropped));
            QVector<LandedBake> lost = targets;
            for (LandedBake &row : lost) row.ok = false;
            for (int i = 0; i < sListeners.size();) {
                if (!sListeners[i].context) { sListeners.removeAt(i); continue; }
                sListeners[i].fn(lost);
                ++i;
            }
            if (done) done(jobs.size(), dropped);
            return;
        }
        QSqlDatabase conn = QSqlDatabase::database();
        int failed = 0;
        QSet<QString> failedPaths;
        for (int i = 0; i < results.size(); ++i) {
            QString error;
            if (!commitBake(conn, root, results[i], &error)) {
                ++failed;
                failedPaths.insert(jobs.at(i).path);
                irisLog(QStringLiteral("mesh bake: '%1' could not be baked (%2)")
                            .arg(QFileInfo(jobs.at(i).path).fileName(), error));
            }
        }
        clear();
        QVector<LandedBake> landed = targets;
        for (LandedBake &row : landed) row.ok = !failedPaths.contains(row.path);
        for (int i = 0; i < sListeners.size();) {
            if (!sListeners[i].context) { sListeners.removeAt(i); continue; }
            sListeners[i].fn(landed);
            ++i;
        }
        if (done) done(failed, QString());
    }
}
}   // namespace

int bakeLandedAsync(const QStringList &guids)
{
    if (guids.isEmpty()) return 0;
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QSet<QString> only(guids.cbegin(), guids.cend());
    QVector<BakeJob> jobs;
    QVector<BakeTarget> rows;   // EVERY landed row over a model file, before the by-content dedupe
    for (const BakeTarget &target : modelBakesNeeded(conn, root, &only, &rows)) {
        BakeJob job = prepareBake(conn, root, target);
        if (job.path.isEmpty()) continue;
        jobs.append(job);
    }
    if (jobs.isEmpty()) return 0;
    // THE TARGETS ARE EVERY ROW OVER A BAKED FILE (F6), not the one row the dedupe kept: two objects
    // of a pack sharing one model are two rows over one bake, and the second one's nodes need their
    // geometry swapped in too.
    QSet<QString> baking;
    for (const BakeJob &job : jobs) baking.insert(job.path);
    QVector<LandedBake> targets;
    for (const BakeTarget &row : rows)
        if (baking.contains(row.path)) targets.append({ row.assetGuid, row.path, true });
    bakeAsync(jobs, targets, DoneFn());
    return jobs.size();
}

bool ensureFreshAsync(const QString &sourcePath, const QString &assetGuid, QObject *context,
                      std::function<void(bool fresh, const QString &dropped)> landed)
{
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    if (sourcePath.isEmpty() || isFresh(conn, root, sourcePath, assetGuid)) return false;
    const QVector<BakeJob> jobs = staleJobsFor(conn, root, { sourcePath });
    if (jobs.isEmpty()) return false;   // no source on disk: the model stays missing
    QPointer<QObject> guard(context);
    bakeAsync(jobs, { { assetGuid, sourcePath, true } },
              [guard, landed, sourcePath, assetGuid](int, const QString &dropped) {
                  if (!guard || !landed) return;
                  if (!dropped.isEmpty()) { landed(false, dropped); return; }
                  landed(isFresh(QSqlDatabase::database(), AssetStorePaths::root(), sourcePath,
                                 assetGuid), QString());
              });
    return true;
}

void holdCommits(bool on)
{
    sCommitHold = on;
    if (on) return;
    const QVector<std::function<void()>> held = std::move(sHeldCommits);
    sHeldCommits.clear();
    for (const std::function<void()> &land : held) land();
}

bool bakePending(const QString &sourcePath)
{
    return sPending.contains(sourcePath);
}

int pendingBakeCount()
{
    return sPendingJobs;
}

void addLandedListener(QObject *context, std::function<void(const QVector<LandedBake> &)> listener)
{
    sListeners.append({ QPointer<QObject>(context), std::move(listener) });
}

QStringList modelSourcesNeedingBake(QSqlDatabase conn, const QString &root)
{
    QStringList out;
    for (const BakeTarget &target : modelBakesNeeded(conn, root))
        if (!out.contains(target.path)) out.append(target.path);
    return out;
}

}   // namespace MeshBakeStore
