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
#include <QTemporaryDir>
#include <memory>
#include <atomic>
#include <QtConcurrent/QtConcurrentRun>
#include <QThreadPool>

#include "irisgl/core/threadbudget.h"
#include <QThread>
#include <QFuture>
#include <QPointer>

#include "data/database/database.h"
#include "data/database/dbqt.h"
#include "data/db/statement.h"
#include "data/constants.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/bootreport.h"
#include "services/jahlog.h"
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
QJsonObject importRecordForOid(db::Connection &conn, const QString &oid)
{
    if (oid.isEmpty()) return QJsonObject();
    // NEWEST FIRST, as the header promises: with no ORDER BY the answer was
    // SQLite's index order, so "the default variant for this content" was not
    // even stable between two runs over one library (the second read's F6).
    db::Statement query(conn, "SELECT A.properties FROM asset_files AF "
                              "JOIN assets A ON A.guid = AF.asset_guid WHERE AF.oid = ? "
                              "ORDER BY AF.rowid DESC");
    dbqt::bindText(query, 1, oid);
    db::Error e;
    QJsonObject best;
    while (query.step(&e)) {
        const QJsonObject props =
            QJsonDocument::fromJson(dbqt::bytes(query, 0)).object();
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
QJsonObject importRecordForGuid(db::Connection &conn, const QString &guid)
{
    QString at = guid;
    for (int hop = 0; hop < 8 && !at.isEmpty(); ++hop) {
        db::Statement query(conn, "SELECT properties, parent FROM assets WHERE guid = ?");
        dbqt::bindText(query, 1, at);
        db::Error e;
        if (!query.step(&e)) return QJsonObject();
        const QJsonObject props = QJsonDocument::fromJson(dbqt::bytes(query, 0)).object();
        if (!props.value(QStringLiteral("import")).toObject().isEmpty()) return props;
        at = dbqt::text(query, 1);
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
bool recordBake(db::Connection &conn, const QString &root, const QString &sourceOid,
                const QString &bakePath, QString *errorOut,
                AssetCas::Staged *staged = nullptr)
{
    QStringList owners;
    {
        db::Statement owner(conn, "SELECT DISTINCT asset_guid FROM asset_files WHERE oid = ?");
        dbqt::bindText(owner, 1, sourceOid);
        db::Error e;
        while (owner.step(&e)) owners.append(dbqt::text(owner, 0));
        if (e) {   // a failed read records the bake under none of its owners: said, never "no owner"
            if (errorOut) *errorOut = dbqt::describe(e);
            return false;
        }
    }
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
        db::Statement stale(conn, "DELETE FROM asset_files WHERE asset_guid = ? AND role = ? "
                                  "AND name = ? AND oid <> ?");
        dbqt::bindText(stale, 1, ownerGuid);
        dbqt::bindText(stale, 2, iris::MeshBake::casRole());
        dbqt::bindText(stale, 3, bakeName);
        dbqt::bindText(stale, 4, staged && !staged->oid.isEmpty() ? staged->oid
                                                                 : AssetCas::hashFile(bakePath));
        db::Error e;
        if (!stale.run(&e)) {
            if (errorOut) *errorOut = dbqt::describe(e);
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
        if (!casError.isEmpty()) JahLog::write(JahLog::legacy, JahLog::Level::Warning, "mesh bake: " + casError);
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

/// A row's import settings (its `import` record's `settings`, and the file's declared unit scale) as the bake
/// keys on them: the settings hash and the import transform. Identity for a row with no record.
QPair<QString, iris::ImportTransform> settingsOfRecord(const QJsonObject &props)
{
    const QJsonObject record = props.value(QStringLiteral("import")).toObject();
    const QJsonObject settings = record.value(QStringLiteral("settings")).toObject();
    const iris::ImportSettings parsed = iris::ImportSettings::fromJson(settings);
    // The file's own declaration, already measured at import: a unit
    // OVERRIDE then costs no probe parse (irisgl/import/scenesource.h).
    const double declared = props.value(QStringLiteral("metadata")).toObject()
                                 .value(QStringLiteral("unitScale")).toDouble(0.0);
    return { parsed.hash(), parsed.transform(declared) };
}

QPair<QString, iris::ImportTransform> resolveSettings(db::Connection &conn, const QString &root,
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
    if (!oid.isEmpty())
        out = settingsOfRecord(assetGuid.isEmpty() ? importRecordForOid(conn, oid)
                                                   : importRecordForGuid(conn, assetGuid));
    // THE SHARED CACHE IS FILLED BY THE DEFAULT CONNECTION'S LOOKUPS ONLY (LARGE-LIBRARY-1): a worker's
    // (the bake sweep's, an archive's) filling it made the UI thread's statement count depend on how far
    // that worker had got — at 10,000 assets the boot's material preview paid its 2 lookups, at 1,000 the
    // sweep had paid them first. A worker reads what is there; it never writes it.
    if (conn.name() != Database::kEditorConnection) return out;
    QMutexLocker locked(&sSettingsLock);
    if (sCacheGeneration.load() == generation) sSettingsCache.insert(key, out);
    return out;
}
}   // namespace

QString settingsHashFor(db::Connection &conn, const QString &root, const QString &sourcePath,
                        const QString &assetGuid)
{
    return resolveSettings(conn, root, sourcePath, assetGuid).first;
}


iris::ImportTransform transformFor(db::Connection &conn, const QString &root,
                                   const QString &sourcePath, const QString &assetGuid)
{
    return resolveSettings(conn, root, sourcePath, assetGuid).second;
}



namespace {
/// One recorded row of a bake's name: its object, the object's extension and the catalog's size of it.
struct BakeRow
{
    QString oid;
    QString ext;
    qint64 size = -1;   ///< -1: the catalog records no size (NULL)
};

/// The newest of a bake name's recorded rows that is the CURRENT bake: its object present, its size the
/// catalog's, its header's producer fingerprint `fingerprint`. CHEAP, AND ENOUGH (FORWARD-ONLY-1 D3): no
/// payload is read. Empty when none is.
QString freshBakeAmong(const QString &root, const QVector<BakeRow> &newestFirst, const QString &fingerprint)
{
    for (const BakeRow &row : newestFirst) {
        const QString path = AssetStorePaths::objectPathIn(root, row.oid, row.ext);
        const QFileInfo info(path);
        if (!info.exists()) continue;
        if (row.size >= 0 && info.size() != row.size) continue;
        // A READ REFUSED THIS OBJECT's payload (its checksum: N3): its header still matches, so the header-only
        // check alone would call it fresh forever. One set lookup, no payload read.
        if (AssetCas::isMarkedCorrupt(path)) continue;
        if (!iris::MeshBake::headerMatches(path, fingerprint)) continue;
        return path;
    }
    return QString();
}
}   // namespace

iris::PrewarmItem planFor(db::Connection &conn, const QString &root, const QString &sourcePath,
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
    // The role is a LITERAL: the partial idx_asset_files_bake (Database's index list) serves only a WHERE that
    // states its own — a bound role read the whole of asset_files for every model a session loads.
    db::Statement query(conn, dbqt::utf8(QStringLiteral("SELECT AF.oid, F.ext, F.size FROM asset_files AF "
                                                        "LEFT JOIN files F ON AF.oid = F.oid "
                                                        "WHERE AF.role = '%1' AND AF.name = ? ORDER BY AF.rowid DESC")
                                             .arg(iris::MeshBake::casRole())));
    dbqt::bindText(query, 1, iris::MeshBake::fileNameFor(sourceOid, settings));
    db::Error e;
    QVector<BakeRow> rows;
    while (query.step(&e))
        rows.append({ dbqt::text(query, 0), dbqt::text(query, 1), query.isNull(2) ? -1 : query.int64(2) });
    if (e) return item;   // a failed read plans no bake: the model parses, as before
    const QString fingerprint = iris::MeshBake::fingerprintFor(sourceOid, settings);
    item.bakePath = freshBakeAmong(root, rows, fingerprint);
    if (!item.bakePath.isEmpty()) item.bakeFingerprint = fingerprint;
    return item;
}


iris::BakedModelPtr load(db::Connection &conn, const QString &sourcePath, const QString &assetGuid)
{
    const QString root = AssetStorePaths::root();
    if (sourcePath.isEmpty()) return iris::BakedModelPtr();
    const quint64 generation = lookupGeneration();
    // KEYED BY (path, settings): identical bytes imported twice under different
    // settings are two different geometries and must not share a cache slot.
    const QString cacheKey =
        sourcePath + QLatin1Char('|') + settingsHashFor(conn, root, sourcePath, assetGuid);
    {
        QMutexLocker locked(&sLock);
        const auto hit = sCache.constFind(cacheKey);
        if (hit != sCache.constEnd()) return hit.value();
    }

    const iris::PrewarmItem item = planFor(conn, root, sourcePath, assetGuid);
    iris::BakedModelPtr result;
    if (!item.bakePath.isEmpty()) {
        iris::MeshBake::Model model = iris::MeshBake::read(item.bakePath, item.bakeFingerprint);
        if (model.valid)
            result = std::make_shared<const iris::MeshBake::Model>(std::move(model));
        else   // its header matched (planFor), so the PAYLOAD is corrupt: the next stale pass re-bakes it (N3)
            AssetCas::markCorrupt(item.bakePath);
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


bool isFresh(db::Connection &conn, const QString &root, const QString &sourcePath,
             const QString &assetGuid)
{
    // The plan's own test (size + header fingerprint), never a payload read:
    // a sweep over a library asks this of every model.
    return !planFor(conn, root, sourcePath, assetGuid).bakePath.isEmpty();
}

bool bakeAsset(Database *db, db::Connection &conn, const QString &root, const QString &guid,
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
        db::Statement files(conn, "SELECT AF.oid, AF.name, F.ext FROM asset_files AF "
                                  "LEFT JOIN files F ON AF.oid = F.oid "
                                  "WHERE AF.asset_guid = ? AND AF.role <> ? "
                                  "ORDER BY CASE AF.role WHEN 'source' THEN 0 ELSE 1 END, AF.name");
        dbqt::bindText(files, 1, guid);
        dbqt::bindText(files, 2, iris::MeshBake::casRole());
        db::Error e;
        while (files.step(&e)) {
            if (!Constants::MODEL_EXTS.contains(QFileInfo(dbqt::text(files, 1)).suffix().toLower()))
                continue;
            const QString candidate = AssetStorePaths::objectPathIn(root, dbqt::text(files, 0), dbqt::text(files, 2));
            if (QFileInfo::exists(candidate)) { sourcePath = candidate; break; }
        }
        if (e) {   // a failed read is not "a row with no model": said
            if (errorOut) *errorOut = dbqt::describe(e);
            return false;
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

bool bakeSource(db::Connection &conn, const QString &root, const QString &sourcePath,
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

QVector<BakeTarget> modelBakesNeeded(db::Connection &conn, const QString &root,
                                     const QSet<QString> *onlyGuids, QVector<BakeTarget> *allRows)
{
    QVector<BakeTarget> out;
    if (onlyGuids && onlyGuids->isEmpty()) return out;
    // ONE CACHE GENERATION FOR THE WHOLE PLAN (I7): a clip's lookup below runs inside the files statement's
    // still-stepping SELECT, whose WAL read snapshot is pinned until it finishes — sampled once, here.
    const PinGeneration pinned;
    // THE GUID FILTER IS THE QUERY'S (D3): an archive import asks about the rows it brought, and must not walk
    // the whole library to find them.
    QString guidFilter;
    if (onlyGuids) {
        QStringList marks;
        for (int i = 0; i < onlyGuids->size(); ++i) marks << QStringLiteral("?");
        guidFilter = QStringLiteral(" AND AF.asset_guid IN (%1)").arg(marks.join(QLatin1Char(',')));
    }
    const auto bindFilter = [&](db::Statement &query) {
        int at = 0;
        dbqt::bindText(query, ++at, iris::MeshBake::casRole());
        if (onlyGuids)
            for (const QString &guid : *onlyGuids) dbqt::bindText(query, ++at, guid);
    };

    // THE PLAN READS THE CATALOG IN THREE STATEMENTS, whatever the library's size (LARGE-LIBRARY-1 R5): it asked
    // two per model — the row's import record (importRecordForGuid, a walk up to the Object) and its bake's rows
    // by name, a statement that SCANS asset_files (no index on role, name): O(models x files) at every boot.
    // (1) THE IMPORT RECORDS of every row naming a model file and of the rows above it (the walk
    // importRecordForGuid makes, its bound included), read once.
    QStringList modelNames;
    for (const QString &ext : Constants::MODEL_EXTS) modelNames << QStringLiteral("AF.name LIKE '%.%1'").arg(ext);
    QHash<QString, QPair<QString, QByteArray>> records;   // guid -> (parent, properties)
    {
        db::Statement query(conn, dbqt::utf8(QStringLiteral("WITH RECURSIVE chain(guid, hop) AS ("
                                     "SELECT DISTINCT AF.asset_guid, 0 FROM asset_files AF WHERE AF.role <> ?%1 AND (%2) "
                                     "UNION SELECT A.parent, chain.hop + 1 FROM chain JOIN assets A ON A.guid = chain.guid "
                                     "WHERE A.parent IS NOT NULL AND A.parent <> '' AND chain.hop < 7) "
                                     "SELECT A.guid, A.parent, A.properties FROM assets A WHERE A.guid IN (SELECT guid FROM chain)")
                          .arg(guidFilter, modelNames.join(QStringLiteral(" OR ")))));
        bindFilter(query);
        db::Error e;
        while (query.step(&e))
            records.insert(dbqt::text(query, 0), { dbqt::text(query, 1), dbqt::bytes(query, 2) });
        if (e) return out;   // a plan from a failed read plans nothing (as before: the next open's sweep plans again)
    }
    const auto recordOf = [&records](QString at) {   // importRecordForGuid over the rows read
        for (int hop = 0; hop < 8 && !at.isEmpty(); ++hop) {
            const auto row = records.constFind(at);
            if (row == records.cend()) return QJsonObject();
            const QJsonObject props = QJsonDocument::fromJson(row->second).object();
            if (!props.value(QStringLiteral("import")).toObject().isEmpty()) return props;
            at = row->first;
        }
        return QJsonObject();
    };
    // (2) EVERY RECORDED BAKE, by its name, newest first (planFor's lookup, for all of them at once).
    QHash<QString, QVector<BakeRow>> bakes;
    {
        db::Statement query(conn, "SELECT AF.name, AF.oid, F.ext, F.size FROM asset_files AF "
                                  "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.role = ? ORDER BY AF.rowid DESC");
        dbqt::bindText(query, 1, iris::MeshBake::casRole());
        db::Error e;
        while (query.step(&e))
            bakes[dbqt::text(query, 0)].append({ dbqt::text(query, 1), dbqt::text(query, 2),
                                                 query.isNull(3) ? -1 : query.int64(3) });
        if (e) return out;
    }

    // (3) Every recorded file whose DISPLAY NAME is a model, with the ROW that names it — one entry per distinct
    // (content, import settings) pair, since that pair is what a bake is keyed on. Deduplicating by content alone
    // (what this did until the second read's F3) built one variant and left every other row parsing on every
    // open, forever. The ROW'S TYPE rides along: a clip row's content gets a CLIP bake (SHIPPED-BAKES-1), every
    // other model file a model bake.
    db::Statement query(conn, dbqt::utf8(QStringLiteral("SELECT AF.oid, AF.name, F.ext, AF.asset_guid, A.type FROM asset_files AF "
                                                        "LEFT JOIN files F ON AF.oid = F.oid "
                                                        "LEFT JOIN assets A ON A.guid = AF.asset_guid WHERE AF.role <> ?%1 ORDER BY AF.rowid")
                                             .arg(guidFilter)));
    bindFilter(query);
    QSet<QString> seen;          // "<oid>|<settingsHash>"
    QHash<QString, QString> pathForOid;
    db::Error e;
    while (query.step(&e)) {
        const QString oid = dbqt::text(query, 0);
        if (!Constants::MODEL_EXTS.contains(QFileInfo(dbqt::text(query, 1)).suffix().toLower()))
            continue;
        QString path = pathForOid.value(oid);
        if (path.isEmpty()) {
            path = AssetStorePaths::objectPathIn(root, oid, dbqt::text(query, 2));
            if (!QFileInfo::exists(path)) { pathForOid.insert(oid, QString()); continue; }
            pathForOid.insert(oid, path);
        }
        if (path.isEmpty()) continue;   // offline/purged object, already judged
        const QString guid = dbqt::text(query, 3);
        const bool clipRow = int(query.int64(4)) == static_cast<int>(ModelTypes::Animation);
        if (allRows) allRows->append({ path, guid, clipRow });
        if (clipRow) {
            const QString key = oid + QStringLiteral("|clip");
            if (seen.contains(key)) continue;
            seen.insert(key);
            if (!clipBakePath(conn, root, path).isEmpty()) continue;
            out.append({ path, guid, true });
            continue;
        }
        // The bake's key and its freshness as settingsHashFor and isFresh compute them, from the rows read above.
        const QString storeOid = oidFromStorePath(root, path);
        const QString settings = storeOid.isEmpty() ? iris::ImportSettings::identityHash()
                                                    : settingsOfRecord(recordOf(guid)).first;
        const QString key = oid + QLatin1Char('|') + settings;
        if (seen.contains(key)) continue;
        seen.insert(key);
        if (!storeOid.isEmpty()
            && !freshBakeAmong(root, bakes.value(iris::MeshBake::fileNameFor(storeOid, settings)),
                               iris::MeshBake::fingerprintFor(storeOid, settings))
                    .isEmpty())
            continue;
        out.append({ path, guid, false });
    }
    if (e) return {};   // a plan cut short by a failed read is no plan: the next sweep plans again
    return out;
}

BakeJob prepareBake(db::Connection &conn, const QString &root, const BakeTarget &target)
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

BakeResult runBake(const BakeJob &job, const std::atomic<bool> *cancel)
{
    const auto cancelled = [cancel]() { return cancel && cancel->load(); };
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
            job.transform, cancel);
        if (cancelled()) { out.error = QStringLiteral("cancelled"); return out; }
        if (!model.valid) {
            out.error = QStringLiteral("could not parse '%1' for baking").arg(QFileInfo(job.path).fileName());
            return out;
        }
        path = QDir(dir->path()).filePath(iris::MeshBake::fileNameFor(job.sourceOid, job.settings));
        // CANCEL POINTS (F5) before the write and before the store's stage: a cancelled bake writes
        // and stages nothing.
        if (cancelled()) { out.error = QStringLiteral("cancelled"); return out; }
        if (!iris::MeshBake::write(path, model, &out.error)) return out;
    }
    if (cancelled()) { out.error = QStringLiteral("cancelled"); return out; }
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

bool commitBake(db::Connection &conn, const QString &root, BakeResult &result, QString *errorOut)
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

namespace {
QMutex sRequeuedLock;
QSet<QString> sRequeued;   // the models takeRefused sent back once (the process's lifetime)
}   // namespace

QStringList takeRefused(const QVector<iris::PrewarmItem> &plan, iris::MeshPrewarm &prewarm)
{
    QStringList refused;
    for (const iris::PrewarmItem &item : plan) {
        if (item.bakePath.isEmpty() || prewarm.baked(item.path) || !prewarm.contains(item.path)) continue;
        {
            QMutexLocker locked(&sRequeuedLock);
            if (sRequeued.contains(item.path)) {   // ONCE: a re-bake refused again is a miss, never a loop
                JahLog::write(JahLog::legacy, JahLog::Level::Warning,
                              QStringLiteral("mesh bake: %1: its re-bake was refused again - not retried, the model "
                                             "stays missing").arg(item.path));
                continue;
            }
            sRequeued.insert(item.path);
        }
        AssetCas::markCorrupt(item.bakePath);
        prewarm.forget(item.path);
        refused.append(item.path);
    }
    return refused;
}

QVector<BakeJob> staleJobsFor(db::Connection &conn, const QString &root, const QStringList &paths)
{
    QVector<BakeJob> jobs;
    const PinGeneration pinned;   // the lookups run inside `rows`' pinned snapshot (I7)
    QSet<QString> seen;   // "<oid>|<settingsHash>"
    for (const QString &path : paths) {
        const QString oid = oidFromStorePath(root, path);
        if (oid.isEmpty() || !QFileInfo::exists(path)) continue;   // no source: missing
        db::Statement rows(conn, "SELECT DISTINCT AF.asset_guid, A.type FROM asset_files AF "
                                 "LEFT JOIN assets A ON A.guid = AF.asset_guid WHERE AF.oid = ? AND AF.role <> ?");
        dbqt::bindText(rows, 1, oid);
        dbqt::bindText(rows, 2, iris::MeshBake::casRole());
        db::Error e;
        while (rows.step(&e)) {
            const QString guid = dbqt::text(rows, 0);
            if (int(rows.int64(1)) == static_cast<int>(ModelTypes::Animation)) {
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

int rebuildPumped(db::Connection &conn, const QVector<BakeJob> &jobs, const std::function<void(int, int)> &progress)
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
    const QString root = AssetStorePaths::root();
    for (BakeResult &result : results) {
        QString error;
        if (!commitBake(conn, root, result, &error)) {
            ++failed;
            JahLog::write(JahLog::legacy, JahLog::Level::Warning, QStringLiteral("mesh bake: a stale bake could not be rebuilt (%1)").arg(error));
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
/// THE IN-FLIGHT JOBS' CANCEL FLAG (F5): every async worker started under it reads it inside its bake
/// (MeshBake's cancel points); stopBackgroundRebuild raises it and installs a fresh one for later bakes.
std::shared_ptr<std::atomic<bool>> sAsyncCancel = std::make_shared<std::atomic<bool>>(false);
/// How many async jobs are INSIDE runBake right now (a worker that has started its bake) — what a suite
/// that quits during a bake waits for (assets.bakesRunning), so its quit lands inside the job.
std::atomic<int> sAsyncRunning { 0 };
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
        // ONE thread of the background tier (irisgl/core/threadbudget.h, THREADING-1): the library sweep
        // rebuilds its bakes one at a time, each bake as wide as the bake pool lets it.
        auto *p = new QThreadPool;
        p->setMaxThreadCount(std::min(1, iris::threadBudget().background));
        p->setThreadPriority(QThread::LowestPriority);
        return p;
    }();
    return pool;
}
}   // namespace

void startBackgroundRebuild(Database &library, std::shared_ptr<SweepPlan> plan)
{
    // EVERY WAY OUT SAYS WHICH (the plan is a result a reader joins: services/bootreport.h).
    if (backgroundRebuildRunning() || !QCoreApplication::instance()) { plan->finish(SweepPlan::State::Off); return; }
    // JAHSHAKA_BAKE_SWEEP=0: no library sweep (a suite that proves the OPEN
    // rebuilds its own stale bakes needs the sweep out of the race).
    if (qEnvironmentVariableIsSet("JAHSHAKA_BAKE_SWEEP")
        && qEnvironmentVariableIntValue("JAHSHAKA_BAKE_SWEEP") == 0) {
        plan->finish(SweepPlan::State::Off);
        return;
    }
    if (!library.connection().isOpen()) { plan->finish(SweepPlan::State::Off); return; }
    const QString dbPath = library.filePath();
    db::Connection *editor = &library.connection();   // the commits' (UI thread); a stop moves the epoch first
    const quint64 epoch = sAsyncEpoch.load();
    const QString root = AssetStorePaths::root();
    sBackgroundStop.store(false);
    // THE SWEEP IS PLANNED ON THE WORKER TOO (TC2-LIBRARY-BOOT-READ, 2026-10-10): the plan reads
    // every model file of the library and every row naming it (modelBakesNeeded: 22.6 MB of a
    // 10k-asset library's 130 MB database, measured) — it ran on the UI thread inside the main
    // window's constructor, at every boot, to find nothing stale. The catalog is per thread, so
    // the worker reads through its OWN read connection to the same file; only the commits come
    // back to this (the database) thread, as before.
    plan->start();
    sBackground = QtConcurrent::run(backgroundPool(), [dbPath, root, plan, editor, epoch]() {
        // THE WORKER's EXIT ALWAYS ANSWERS: a way out that published nothing is Silent, never a join left waiting.
        struct ExitSays { std::shared_ptr<SweepPlan> plan; ~ExitSays() { plan->finishIfPlanning(); } } exitSays{ plan };
        QVector<BakeJob> jobs;
        bool read = false;   // the plan read the library (a connection that never opened planned nothing)
        {
            // ITS OWN READ-ONLY CONNECTION, OPENED BY THE LAYER on this worker (NOQT-DATABASE-1 stage 2), closed
            // with this scope.
            db::Options options;
            options.readOnly = true;
            options.busyMs = db::kEditorBusyMs;
            options.name = "BakeSweep";
            db::Error error;
            db::Connection conn = db::Connection::open(dbPath.toStdString(), options, &error);
            if (conn.isOpen()) {
                for (const BakeTarget &target : modelBakesNeeded(conn, root)) {
                    if (sBackgroundStop.load()) break;
                    if (!QFileInfo::exists(target.path)) continue;
                    BakeJob job = prepareBake(conn, root, target);
                    if (!job.path.isEmpty()) jobs.append(job);
                }
                read = true;
            } else {
                JahLog::write(JahLog::legacy, JahLog::Level::Warning,
                    QStringLiteral("mesh bake: the background sweep could not read the library (%1)").arg(dbqt::describe(error)));
            }
        }
        if (sBackgroundStop.load()) { plan->finish(SweepPlan::State::Stopped); return; }
        if (!read) { plan->finish(SweepPlan::State::Unread); return; }
        plan->finish(SweepPlan::State::Planned, int(jobs.size()));
        if (jobs.isEmpty()) return;
        JahLog::write(JahLog::legacy, JahLog::Level::Log, QStringLiteral("mesh bake: rebuilding %1 stale bake(s) in the background").arg(jobs.size()));
        for (const BakeJob &job : jobs) {
            if (sBackgroundStop.load()) return;
            auto result = std::make_shared<BakeResult>(runBake(job, &sBackgroundStop));
            QMetaObject::invokeMethod(QCoreApplication::instance(), [result, root, editor, epoch]() {
                QString error;
                // A LANDING AFTER A RESET OR A QUIT COMMITS NOTHING (the async bakes' rule): the library it was
                // planned against is gone, and so may be the connection it would commit on.
                if (epoch != sAsyncEpoch.load()) {
                    QVector<AssetCas::Staged> leftover{ result->staged };
                    AssetCas::discardStaged(leftover);
                    return;
                }
                // A CANCEL IS NOT A FAILURE (read 5): a reset or a quit stopped the job — the next
                // library open's sweep builds it again. Said as what it is.
                if (result->error == QStringLiteral("cancelled")) {
                    QVector<AssetCas::Staged> leftover{ result->staged };
                    AssetCas::discardStaged(leftover);
                    JahLog::write(JahLog::legacy, JahLog::Level::Log, QStringLiteral("mesh bake: a background rebuild was cancelled (a reset or a quit)"));
                    return;
                }
                if (!commitBake(*editor, root, *result, &error))
                    JahLog::write(JahLog::legacy, JahLog::Level::Warning, QStringLiteral("mesh bake: background rebuild failed (%1)").arg(error));
            }, Qt::QueuedConnection);
        }
    });
}

void stopBackgroundRebuild()
{
    ++sAsyncEpoch;   // first: every landing still queued (the sweep's and the async bakes') is dropped by it
    sBackgroundStop.store(true);
    if (sBackground.isValid() && !sBackground.isFinished()) sBackground.waitForFinished();
    // THE ASYNC BAKES TOO (a reset, a shutdown). The epoch moves FIRST: each worker stops between
    // jobs on it (F5), so the join below waits for the job IN FLIGHT only — never the rest of a
    // multi-model paste — and every landing still queued to this thread is dropped by it (its
    // `done` told why: F7), so nothing lands in a library that is no longer the one it was planned
    // against. The job IN FLIGHT stops too, at its next cancel point (the parse's progress handler,
    // between meshes, before each stage task, before the write and the stage): the join is short.
    sAsyncCancel->store(true);   // the job in flight stops at its next cancel point
    sAsyncCancel = std::make_shared<std::atomic<bool>>(false);
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

bool isClipContent(db::Connection &conn, const QString &root, const QString &sourcePath)
{
    const QString oid = oidFromStorePath(root, sourcePath);
    if (oid.isEmpty()) return false;
    db::Statement query(conn, "SELECT 1 FROM asset_files AF JOIN assets A ON A.guid = AF.asset_guid "
                              "WHERE AF.oid = ? AND A.type = ? LIMIT 1");
    dbqt::bindText(query, 1, oid);
    query.bindInt(2, static_cast<int>(ModelTypes::Animation));
    db::Error e;
    return query.step(&e);
}

QString clipBakePath(db::Connection &conn, const QString &root, const QString &sourcePath)
{
    const QString sourceOid = oidFromStorePath(root, sourcePath);
    if (sourceOid.isEmpty()) return QString();
    // NEWEST FIRST, every candidate — planFor's rule, for planFor's reason: a
    // producer bump leaves the stale generation under the same name until the
    // rebuild retires it.
    db::Statement query(conn, dbqt::utf8(QStringLiteral("SELECT AF.oid, F.ext, F.size FROM asset_files AF "
                                                        "LEFT JOIN files F ON AF.oid = F.oid "
                                                        "WHERE AF.role = '%1' AND AF.name = ? ORDER BY AF.rowid DESC")
                                             .arg(iris::MeshBake::casRole())));   // literal: idx_asset_files_bake (planFor)
    dbqt::bindText(query, 1, iris::MeshBake::clipFileNameFor(sourceOid));
    const QString fingerprint = iris::MeshBake::clipFingerprintFor(sourceOid);
    db::Error e;
    while (query.step(&e)) {
        const QString path = AssetStorePaths::objectPathIn(root, dbqt::text(query, 0), dbqt::text(query, 1));
        const QFileInfo info(path);
        if (!info.exists()) continue;
        if (!query.isNull(2) && info.size() != query.int64(2)) continue;
        if (!iris::MeshBake::clipHeaderMatches(path, fingerprint)) continue;
        return path;
    }
    return QString();
}

iris::BakedClipPtr loadClip(db::Connection &conn, const QString &sourcePath)
{
    if (sourcePath.isEmpty()) return iris::BakedClipPtr();
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

iris::BakedClipPtr ensureClip(db::Connection &conn, const QString &sourcePath)
{
    if (iris::BakedClipPtr have = loadClip(conn, sourcePath)) return have;
    const QString root = AssetStorePaths::root();
    // THE SAME REBUILD THE OPEN RUNS: the store's own source, on a worker, this
    // thread pumping, committed under every row that names the content. Any
    // model content may carry clips, so the job is built for the path asked
    // about, whatever row type names it.
    BakeJob job = prepareBake(conn, root, { sourcePath, QString(), true });
    if (job.path.isEmpty() || !QFileInfo::exists(job.path)) return iris::BakedClipPtr();
    rebuildPumped(conn, { job }, std::function<void(int, int)>());
    return loadClip(conn, sourcePath);
}

QString currentBakePath(db::Connection &conn, int assetType, const QString &sourcePath, const QString &assetGuid)
{
    const QString root = AssetStorePaths::root();
    if (assetType == static_cast<int>(ModelTypes::Animation))
        return clipBakePath(conn, root, sourcePath);
    return planFor(conn, root, sourcePath, assetGuid).bakePath;
}

bool ensureFresh(db::Connection &conn, const QString &sourcePath, const QString &assetGuid)
{
    const QString root = AssetStorePaths::root();
    if (isFresh(conn, root, sourcePath, assetGuid)) return true;
    const QVector<BakeJob> jobs = staleJobsFor(conn, root, { sourcePath });
    if (jobs.isEmpty()) return false;   // no source on disk: the model stays missing
    rebuildPumped(conn, jobs, std::function<void(int, int)>());
    return isFresh(conn, root, sourcePath, assetGuid);
}

namespace {

bool sCommitHold = false;
QVector<std::function<void()>> sHeldCommits;
/// `done(failed, dropped)`: `dropped` names why NOTHING was committed (a reset or shutdown moved the
/// epoch, the store moved), empty when the landing ran.
using DoneFn = std::function<void(int failed, const QString &dropped)>;
void landAsync(db::Connection *conn, const QVector<BakeJob> &jobs, const QVector<LandedBake> &targets,
               const DoneFn &done, quint64 epoch, QVector<BakeResult> results);

/// Bakes `jobs` on a worker; commits them on the database thread when they land, tells the
/// listeners which of `targets` landed, then calls `done(failed)`.
void bakeAsync(db::Connection &conn, const QVector<BakeJob> &jobs, const QVector<LandedBake> &targets, DoneFn done)
{
    db::Connection *committer = &conn;   // the UI thread's; a stop (reset, quit) moves the epoch before it can go
    for (const BakeJob &job : jobs) ++sPending[job.path];
    sPendingJobs += jobs.size();
    const quint64 epoch = sAsyncEpoch.load();
    const std::shared_ptr<std::atomic<bool>> cancel = sAsyncCancel;
    QFuture<QVector<BakeResult>> future = QtConcurrent::run([jobs, epoch, cancel]() {
        QVector<BakeResult> results;
        results.reserve(jobs.size());
        for (const BakeJob &job : jobs) {
            // A RESET OR SHUTDOWN STOPS THE WORKER BETWEEN JOBS (F5), like the background sweep —
            // and INSIDE the job in flight, at MeshBake's cancel points (the flag below).
            if (sAsyncEpoch.load() != epoch || cancel->load()) break;
            ++sAsyncRunning;
            results.append(runBake(job, cancel.get()));
            --sAsyncRunning;
        }
        return results;
    });
    sAsyncFutures.removeIf([](const QFuture<QVector<BakeResult>> &f) { return f.isFinished(); });
    sAsyncFutures.append(future);
    future.then(QCoreApplication::instance(), [committer, jobs, targets, done, epoch](QVector<BakeResult> results) {
        landAsync(committer, jobs, targets, done, epoch, std::move(results));
    });
}

void landAsync(db::Connection *conn, const QVector<BakeJob> &jobs, const QVector<LandedBake> &targets,
               const DoneFn &done, quint64 epoch, QVector<BakeResult> results)
{
    // THE TEST HOLD (assets.holdBakes): the finished bake waits here, uncommitted and still pending,
    // until the hold is released — so a suite can place and SAVE inside the window deterministically.
    if (sCommitHold) {
        auto held = std::make_shared<QVector<BakeResult>>(std::move(results));
        sHeldCommits.append([conn, jobs, targets, done, epoch, held]() {
            landAsync(conn, jobs, targets, done, epoch, std::move(*held));
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
            JahLog::write(JahLog::legacy, JahLog::Level::Warning, QStringLiteral("mesh bake: %1 bake(s) not committed: %2").arg(jobs.size()).arg(dropped));
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
        int failed = 0;
        QSet<QString> failedPaths;
        for (int i = 0; i < results.size(); ++i) {
            QString error;
            if (!commitBake(*conn, root, results[i], &error)) {
                ++failed;
                failedPaths.insert(jobs.at(i).path);
                JahLog::write(JahLog::legacy, JahLog::Level::Warning, QStringLiteral("mesh bake: '%1' could not be baked (%2)")
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

int bakeLandedAsync(db::Connection &conn, const QStringList &guids)
{
    if (guids.isEmpty()) return 0;
    // THE ROWS WERE JUST WRITTEN: the caches may still hold what a removed row of the same guid and
    // content answered (a paste re-lands a deleted row under its original guid), and a plan from
    // those stale settings bakes a variant no lookup of the new row will ever ask for. Measured: a
    // pasted rig planned under the old row's settings hash, committed, and the landing then found no
    // bake for the row's own. The landed rows carry their import record — the closure a paste and a
    // share file carry includes each row's properties and a member's Object row (assetclosure) — so
    // the plan below reads the settings the model was imported under.
    clear();
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
    JahLog::write(JahLog::legacy, JahLog::Level::Log, QStringLiteral("mesh bake: %1 import bake(s) started for %2 landed row(s)")
                                                          .arg(jobs.size()).arg(targets.size()));
    bakeAsync(conn, jobs, targets, DoneFn());
    return jobs.size();
}

bool ensureFreshAsync(db::Connection &conn, const QString &sourcePath, const QString &assetGuid, QObject *context,
                      std::function<void(bool fresh, const QString &dropped)> landed)
{
    db::Connection *asker = &conn;   // read again at the landing, on the UI thread (the epoch guards it)
    const QString root = AssetStorePaths::root();
    if (sourcePath.isEmpty() || isFresh(conn, root, sourcePath, assetGuid)) return false;
    const QVector<BakeJob> jobs = staleJobsFor(conn, root, { sourcePath });
    if (jobs.isEmpty()) return false;   // no source on disk: the model stays missing
    QPointer<QObject> guard(context);
    bakeAsync(conn, jobs, { { assetGuid, sourcePath, true } },
              [guard, landed, sourcePath, assetGuid, asker](int, const QString &dropped) {
                  if (!guard || !landed) return;
                  if (!dropped.isEmpty()) { landed(false, dropped); return; }
                  landed(isFresh(*asker, AssetStorePaths::root(), sourcePath, assetGuid), QString());
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

QString modelSourceFor(db::Connection &conn, const QString &root, const QString &guid)
{
    db::Statement query(conn, "SELECT AF.name, AF.oid, F.ext FROM asset_files AF LEFT JOIN files F ON AF.oid = F.oid "
                              "WHERE AF.asset_guid = ? AND AF.role <> ? ORDER BY AF.rowid");
    dbqt::bindText(query, 1, guid);
    dbqt::bindText(query, 2, iris::MeshBake::casRole());
    db::Error e;
    while (query.step(&e)) {
        if (!Constants::MODEL_EXTS.contains(QFileInfo(dbqt::text(query, 0)).suffix().toLower()))
            continue;
        const QString path = AssetStorePaths::objectPathIn(root, dbqt::text(query, 1), dbqt::text(query, 2));
        if (QFileInfo::exists(path)) return path;
    }
    return QString();
}

bool bakePending(const QString &sourcePath)
{
    return sPending.contains(sourcePath);
}

int pendingBakeCount()
{
    return sPendingJobs;
}

int runningBakeCount()
{
    return sAsyncRunning.load();
}

void addLandedListener(QObject *context, std::function<void(const QVector<LandedBake> &)> listener)
{
    sListeners.append({ QPointer<QObject>(context), std::move(listener) });
}

QStringList modelSourcesNeedingBake(db::Connection &conn, const QString &root)
{
    QStringList out;
    for (const BakeTarget &target : modelBakesNeeded(conn, root))
        if (!out.contains(target.path)) out.append(target.path);
    return out;
}

}   // namespace MeshBakeStore
