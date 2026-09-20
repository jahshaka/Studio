/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/import/assetimportservice.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include <utility>

#include "irisgl/import/meshbake.h"
#include "irisgl/import/modelsceneinfo.h"

#include "data/constants.h"
#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/meshbakestore.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimporters.h"
#include "services/jahlog.h"
#include "irisgl/core/logger.h"

AssetImportService::AssetImportService(Database *db, Project *project)
    : db(db), project(project)
{
    // BEFORE MeshImporter, deliberately: the two share every model extension
    // and only the file's CONTENTS separate them (a clip file has no meshes),
    // so the animation sniff — structural, no parse — has to be asked first.
    mImporters.append(new AnimationImporter());
    mImporters.append(new MeshImporter());
    mImporters.append(new MediaImporter(static_cast<int>(ModelTypes::Texture)));
    mImporters.append(new MediaImporter(static_cast<int>(ModelTypes::Music)));
    mImporters.append(new MediaImporter(static_cast<int>(ModelTypes::Video)));
    mImporters.append(new MaterialImporter());
    mImporters.append(new IesImporter());
    mImporters.append(new JafImporter());
    mImporters.append(new FileImporter());
}

AssetImportService::~AssetImportService()
{
    qDeleteAll(mImporters);
}

namespace {

// SIZE CAPS (deep audit 2026-09: "no size caps anywhere"). The pipeline hashes
// every source file, and several importers read theirs entirely into memory
// (the shader/material JSON readers, the IES tokenizer, image decoders). A cap
// per type is the cheap half of the answer: it costs one stat() and it turns
// "the app allocated until the machine died" into a named import error.
//
// The numbers are deliberately generous — a cap that rejects real user content
// is a worse defect than the one it prevents. A 2 GB model or texture is
// already past what the rest of the pipeline survives; a 64 MB shader is not a
// shader.
constexpr qint64 kMaxParsedBytes = 64ll * 1024 * 1024;         // text/JSON-ish types
constexpr qint64 kMaxContentBytes = 2ll * 1024 * 1024 * 1024;  // meshes and media

qint64 maxSourceBytes(int modelType)
{
    switch (static_cast<ModelTypes>(modelType)) {
    case ModelTypes::Mesh:
    case ModelTypes::Object:   // JafImporter — a .jaf archive carries content
    case ModelTypes::Animation:   // a clip file is a model file without the meshes
    case ModelTypes::Texture:
    case ModelTypes::Music:
    case ModelTypes::Video:
        return kMaxContentBytes;
    default:
        // Shader, Material, LightProfile (.ies), File (the text whitelist).
        return kMaxParsedBytes;
    }
}

QString humanSize(qint64 bytes)
{
    if (bytes >= 1024ll * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(double(bytes) / (1024.0 * 1024 * 1024), 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(double(bytes) / (1024.0 * 1024), 0, 'f', 1);
}

}  // namespace

QString AssetImportService::relistUnlistedMatch(const StagedAsset &staged)
{
    if (!db) return QString();
    QSqlDatabase conn = QSqlDatabase::database();
    if (!conn.isOpen()) return QString();

    // The common case pays exactly this: an unlisted row only exists after a
    // library delete that projects vetoed, so most libraries answer 0 here and
    // nothing else in this function runs.
    QSqlQuery any(conn);
    if (!any.exec("SELECT COUNT(*) FROM assets WHERE listed = 0") || !any.next()
        || any.value(0).toInt() == 0)
        return QString();

    // The source oid is ALREADY known: prepare() stamps it into the
    // determinism record for EVERY import — from the importer's own hash when
    // it had one (MeshImporter keys its bake on it), from a read of the source
    // otherwise — and .jaf plans get one too. Re-hashing here would read the
    // whole file a second time on the DB/UI thread, a multi-second freeze on a
    // big model for an answer we are holding (code review 2026-09-10). So
    // there is no fallback to write: an empty oid means this plan never went
    // through prepare(), and the honest answer to "is this an unlisted row's
    // content?" is then "we do not know" — no re-list, a normal import.
    const QString oid = staged.importRecord.value(QStringLiteral("sourceOid")).toString();
    if (oid.isEmpty()) return QString();

    // Same BYTES as an unlisted row's source = the same asset. The type is
    // derived from the content, so identical bytes cannot mean a different
    // kind of asset — no sniff needed (and no second sniff paid).
    //
    // TOP-LEVEL ROWS ONLY (code review 2026-09-10). An unlisted MODEL's
    // texture MEMBER is hidden from every listing (its parent is the Object it
    // rides), so re-listing it would answer "imported" with a row that appears
    // nowhere: the user imports the texture standalone and gets nothing. Such a
    // member falls through to the normal import and gets its own, visible,
    // listed row — sharing the same CAS object, which costs no bytes.
    //
    // Newest first: two unlisted rows can share one source oid (the same file
    // imported twice, then both deleted while pinned), and the more recently
    // IMPORTED row is the one whose name, tags and drawer are most likely to
    // be what the user wants back (the catalog records no deletion time, so
    // creation recency is what there is). rowid breaks the ties date_created
    // cannot: it is second-resolution, and two rows of one import share it.
    QSqlQuery match(conn);
    match.prepare("SELECT AF.asset_guid FROM asset_files AF "
                  "JOIN assets A ON A.guid = AF.asset_guid "
                  "WHERE AF.role = 'source' AND AF.oid = ? AND A.listed = 0 "
                  "AND " + Database::memberSubquery(QStringLiteral("A.guid")) + " "
                  "ORDER BY A.date_created DESC, A.rowid DESC LIMIT 1");
    match.addBindValue(oid);
    if (!match.exec() || !match.next()) return QString();

    const QString guid = match.value(0).toString();
    return db->setAssetListed(guid, true) ? guid : QString();
}

// THE ONE "is this a model?" test the import dialog keys on
// (SPECS/IMPORT_DIALOG_SPEC.md §8). By EXTENSION, deliberately: the question is
// asked BEFORE anything is read, so a sniff is not available yet — and the
// extension is also what the browse dialogs filter on.
bool isModelImportPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty() || suffix == Constants::ASSET_EXT) return false;
    return Constants::MODEL_EXTS.contains(suffix);
}

AssetImporterBase *AssetImportService::pickImporter(const ImportRequest &request,
                                                    QString *error) const
{
    for (auto *importer : mImporters) {
        if (request.typeHint >= 0 && importer->modelType() != request.typeHint) continue;
        if (importer->sniff(request.sourcePath)) return importer;
    }
    if (error)
        *error = QStringLiteral("'%1' is not an importable library file "
                                "(models, animation clips, images, audio, video, shaders, "
                                "materials, .ies light profiles or .jaf)")
                     .arg(QFileInfo(request.sourcePath).fileName());
    return nullptr;
}

ImportResult AssetImportService::import(const ImportRequest &request,
                                        const ImportProgressFn &progress)
{
    // The IMPORT RECORD (SESSION_LOG_SPEC §5). This is the one pipeline
    // (sniff -> validate -> convert -> store -> register), so one record here
    // covers every import route the app has.
    QElapsedTimer clock;
    clock.start();

    PreparedImport prepared = prepare(request, progress);
    ImportResult out = prepared.ok() ? commit(prepared, progress) : prepared.result;

    logImportRecord(request, out, clock.elapsed());
    return out;
}

void AssetImportService::logImportRecord(const ImportRequest &request, const ImportResult &result,
                                         qint64 elapsedMs)
{
    const QString source = QFileInfo(request.sourcePath).fileName();
    if (result.ok()) {
        JAH_LOG(JahLog::assets, Display,
                QStringLiteral("import: '%1' -> %2 (%3 object(s), %4 ms)%5")
                    .arg(source, result.assetGuid)
                    .arg(result.objectOids.size())
                    .arg(elapsedMs)
                    .arg(result.warnings.isEmpty()
                             ? QString()
                             : QStringLiteral(" — %1 warning(s)").arg(result.warnings.size())));
        for (const QString &w : result.warnings)
            JAH_LOG(JahLog::assets, Warning, QStringLiteral("import '%1': %2").arg(source, w));
    } else {
        JAH_LOG(JahLog::assets, Error,
                QStringLiteral("import: '%1' FAILED after %2 ms — %3")
                    .arg(source).arg(elapsedMs).arg(result.error));
    }
}

PreparedImport AssetImportService::prepare(const ImportRequest &request,
                                           const ImportProgressFn &progress)
{
    PreparedImport prepared;
    prepared.request = request;
    ImportResult &result = prepared.result;

    if (!db) { result.error = QStringLiteral("no database"); return prepared; }

    const QFileInfo sourceInfo(request.sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile()) {
        result.error = QStringLiteral("no such file '%1'").arg(request.sourcePath);
        return prepared;
    }

    // ---- sniff ----
    if (progress && !progress(QStringLiteral("sniff"), 0, 0)) {
        result.error = QStringLiteral("cancelled");
        return prepared;
    }
    AssetImporterBase *importer = pickImporter(request, &result.error);
    if (!importer) return prepared;

    // ---- size ----
    const qint64 maxBytes = maxSourceBytes(importer->modelType());
    if (sourceInfo.size() > maxBytes) {
        result.error = QStringLiteral("'%1' is %2; the limit for this asset type is %3")
                           .arg(sourceInfo.fileName(), humanSize(sourceInfo.size()),
                                humanSize(maxBytes));
        return prepared;
    }

    // ---- validate ----
    if (!importer->validate(request.sourcePath, &result.error)) return prepared;

    // ---- convert (private staging; the dir must outlive commit) ----
    prepared.staging = std::make_shared<QTemporaryDir>();
    if (!prepared.staging->isValid()) {
        result.error = QStringLiteral("cannot create an import staging directory");
        return prepared;
    }

    StagedAsset &staged = prepared.staged;
    if (!importer->convert(request, prepared.staging->path(), db, project, staged,
                           &result.error, progress)) {
        if (result.error.isEmpty()) result.error = QStringLiteral("import failed");
        return prepared;
    }
    result.warnings = staged.warnings;

    // The determinism record (spec §3.2.2): content + settings + importer
    // version + the import library's version. Recorded on the row; assets.importSettings
    // reads it back and assets.checkConsistency re-derives the object set.
    staged.importRecord = QJsonObject{
        // An importer that already hashed the source (MeshImporter, which keys
        // its bake on it) hands the oid over instead of making us read the
        // whole file again.
        { "sourceOid", staged.sourceOid.isEmpty() ? AssetCas::hashFile(request.sourcePath)
                                                  : staged.sourceOid },
        { "importer", importer->name() },
        { "importerVersion", importer->version() },
        { "assimp", iris::ModelSceneInfo::importerVersion() },
        // The record the importer APPLIED where it has one (a model's scale,
        // orientation, origin and tuning — importtypes.h), the caller's raw
        // request otherwise.
        { "settings", staged.appliedSettings.isEmpty() ? request.settings
                                                       : staged.appliedSettings },
    };

    // ---- store, phase 1: the BYTES (FSYNC-2) ------------------------------
    //
    // The hashes used to be all this stage prepaid, and the commit then COPIED
    // and FSYNCED every object on the DB thread — which is the UI thread in the
    // app. An fsync waits for the whole device (220-1 153 ms each on this box's
    // store while anything else was writing), so a threaded import froze the
    // window it was threaded to keep alive: avatar.responsive's watchdog caught
    // the UI thread inside libc fsync, 2 019 ms, under commitStagedAsset.
    //
    // So the store stage splits here exactly where AssetCas already splits it
    // (assetcas.h, the two-phase ingest): stage() hashes and puts the bytes in
    // their destination directory under a temp name, flushStaged() makes the
    // whole batch durable in ONE pass, and the commit is left with renames and
    // rows. Both halves of the wait — the copy and the flush — are on this
    // worker thread, where waiting is the job.
    //
    // A cancel here still costs nothing: nothing is published and no row is
    // written, so the staged temps are simply removed.
    const QString storeRoot = AssetStorePaths::root();
    const auto stageBytes = [&](const QString &path, const QString &role, const QString &name) {
        for (const AssetCas::Staged &already : std::as_const(staged.stagedBytes))
            if (already.srcPath == path) return;
        AssetCas::Staged entry;
        entry.srcPath = path;
        entry.role = role;
        entry.name = name;
        entry.knownOid = staged.fileOids.value(path);
        // A staging failure is NOT fatal: the commit falls back to the
        // synchronous ingest, which produces the user-facing error with the
        // wording the pipeline has always used.
        if (!AssetCas::stage(storeRoot, entry)) return;
        staged.fileOids.insert(path, entry.oid);
        staged.stagedBytes.append(entry);
    };

    int stagedCount = 0;
    for (const StagedFile &file : staged.files) {
        if (progress && !progress(QStringLiteral("hash"), stagedCount, staged.files.size())) {
            AssetCas::discardStaged(staged.stagedBytes);
            result.error = QStringLiteral("cancelled");
            return prepared;
        }
        stageBytes(file.path, file.role, file.name);
        ++stagedCount;
    }
    // .jaf archives: the rows come from the archive's own catalog on the DB
    // thread, but the PAYLOAD is already extracted and its bytes are nobody's
    // secret — stage them by path here and let the commit match them up once
    // it knows which guid each one belongs to.
    if (!staged.jaf.assetsDir.isEmpty()) {
        QDirIterator payload(staged.jaf.assetsDir,
                             QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden,
                             QDirIterator::Subdirectories);
        while (payload.hasNext()) {
            const QFileInfo info(payload.next());
            if (progress && !progress(QStringLiteral("hash"), stagedCount, stagedCount + 1)) {
                AssetCas::discardStaged(staged.stagedBytes);
                result.error = QStringLiteral("cancelled");
                return prepared;
            }
            stageBytes(info.absoluteFilePath(), QStringLiteral("file"), info.fileName());
            ++stagedCount;
        }
    }
    AssetCas::flushStaged(staged.stagedBytes);   // the batch's one wait for the device
    return prepared;
}

ImportResult AssetImportService::commit(PreparedImport &prepared,
                                        const ImportProgressFn &progress)
{
    const ImportRequest &request = prepared.request;
    StagedAsset &staged = prepared.staged;
    ImportResult result = prepared.result;

    // RE-LISTING (see relistUnlistedMatch): the DB half is the only half that
    // may touch the database (prepare runs on a worker), so this is where the
    // check that stops a duplicate row lives — once, for both entry points.
    // The staged convert is thrown away; correctness beats the wasted work.
    if (const QString relisted = relistUnlistedMatch(staged); !relisted.isEmpty()) {
        ImportResult back;
        back.assetGuid = relisted;
        back.warnings = result.warnings;
        // The re-listed row IS this import's answer, so it must land where the
        // import asked (code review 2026-09-10) — a drop into a drawer that
        // happened to match an unlisted row used to file nothing at all.
        // Only an EXPLICIT project guid is stamped: the ambient open project
        // is what commitStagedAsset falls back to for a NEW row, and stamping
        // it here would re-home a library row the user only re-imported.
        if (!request.projectGuid.isEmpty())
            db->updateAssetProject(relisted, request.projectGuid);
        if (request.drawerId > 0) {
            if (db->fetchCollectionSubtree(request.drawerId).isEmpty())
                back.error = QStringLiteral("imported, but drawer %1 does not exist")
                                 .arg(request.drawerId);
            else
                db->switchAssetCollection(request.drawerId, relisted);
        }
        JAH_LOG(JahLog::assets, Display,
                QStringLiteral("import: '%1' is the content of UNLISTED asset %2 — re-listed it "
                               "in the library instead of creating a duplicate row")
                    .arg(QFileInfo(request.sourcePath).fileName(), relisted));
        // The staged convert is thrown away, and so are the bytes it staged:
        // the content is in the store already (that is what re-listing means).
        AssetCas::discardStaged(staged.stagedBytes);
        return back;
    }

    // ---- store + register (one transaction) ----
    if (!commitStagedAsset(request, staged, result, progress)) return result;

    result.assetGuid = staged.mainGuid;
    result.meshGuid = staged.meshGuid;
    result.jafKind = staged.jafKind;
    result.metadata = staged.metadata;

    // ---- drawer filing (post-commit, exactly the old importFile contract) ----
    if (request.drawerId > 0) {
        if (db->fetchCollectionSubtree(request.drawerId).isEmpty())
            result.error = QStringLiteral("imported, but drawer %1 does not exist").arg(request.drawerId);
        else
            db->switchAssetCollection(request.drawerId, result.assetGuid);
    }
    return result;
}

namespace
{
/// The bytes prepare() already staged for `path`, or nullptr when it staged
/// none (a hand-built plan, a staging failure) — the caller then ingests
/// synchronously, exactly as the pipeline did before FSYNC-2.
AssetCas::Staged *stagedEntryFor(StagedAsset &staged, const QString &path)
{
    for (AssetCas::Staged &entry : staged.stagedBytes)
        if (entry.srcPath == path) return &entry;
    return nullptr;
}

/// Publish one content file: the staged bytes when prepare() left some (a
/// rename plus the rows — no copy, no flush, microseconds), the synchronous
/// ingest otherwise.
bool storeOneFile(QSqlDatabase conn, const QString &root, StagedAsset &staged,
                  const QString &path, const QString &guid, const QString &role,
                  const QString &name, QString *oidOut, QString *errorOut)
{
    // A store-root change between prepare and commit (Preferences moves the
    // store mid-import) would make the staged temp a CROSS-DEVICE rename: the
    // synchronous ingest is the honest answer there, not a failed publish.
    AssetCas::Staged *pre = stagedEntryFor(staged, path);
    // A store root that MOVED between prepare and commit: the temp is under the
    // old root, so a rename would cross devices — fall back to the synchronous
    // ingest. The separator matters: '/x/store' is a string prefix of '/x/store2'.
    const QString rootSlash = root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/');
    if (pre && !pre->tmpPath.isEmpty() && !pre->tmpPath.startsWith(rootSlash)) pre = nullptr;
    if (pre) {
        // The role and name are the COMMIT's to decide (a .jaf payload file is
        // 'source' or 'file' depending on a catalog name only this thread can
        // read), so they are set here rather than at staging time.
        pre->role = role;
        pre->name = name;
        if (!AssetCas::commitStaged(conn, root, guid, *pre, errorOut)) return false;
        if (oidOut) *oidOut = pre->oid;
        return true;
    }
    return AssetCas::ingestFile(conn, root, path, guid, role, name, oidOut, errorOut,
                                staged.fileOids.value(path));
}
}   // namespace

bool AssetImportService::commitStagedAsset(const ImportRequest &request, StagedAsset &staged,
                                           ImportResult &result, const ImportProgressFn &progress)
{
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QString projectGuid = !request.projectGuid.isEmpty()
                                    ? request.projectGuid
                                    : (project ? project->getProjectGuid() : QString());

    QStringList createdOids;      // objects written by THIS import — rollback set
    QStringList touchedGuids;     // sidecar + legacy-view targets

    DbTransaction tx(conn);
    bool committed = false;   // distinguishes "commit() already unwound" from nesting

    // THE ROLLBACK, and why it has to end the transaction first.
    //
    // The store stage is HALF transactional: the files/asset_files rows are
    // SQL and unwind with the transaction, but the object bytes are files on
    // disk and do not. So a failed import must delete exactly the objects
    // whose rows did not survive — and the way to know that is to ask the
    // database AFTER the rollback.
    //
    // Asking before it (which is what this did until the 2026-09 deep audit)
    // reads the caller's OWN uncommitted inserts: `SELECT 1 FROM files` found
    // every oid this import had just written, `continue` skipped every one,
    // and the cleanup removed NOTHING — ever. Every cancelled or failed import
    // left its objects in the store at refcount 0 with no row naming them, and
    // no GC exists to reap them. (The guard test passed because it cancelled
    // before the first file was stored, so there was nothing to clean either
    // way; tests/importasync now cancels after a real ingest.)
    //
    // Objects that ALREADY existed before this import keep their committed
    // files row through the rollback, so the same query is what protects
    // content shared with another asset from being deleted underneath it.
    auto rollbackAndCleanupObjects = [&]() {
        // No caller nests a transaction around commit() today (verified across
        // the verb, facade and batch-runner paths). If one ever does, the guard
        // degrades to a no-op, the rows below stay uncommitted and this cleanup
        // silently returns to being the no-op the audit found — say so instead.
        if (!tx.isActive() && !committed)
            irisLog("import rollback: no transaction of our own to roll back — "
                    "object cleanup may be reading uncommitted rows");
        tx.rollback();   // idempotent; a no-op if commit() already unwound
        // Whatever prepare() staged and this commit never published is a temp
        // file in the store's own objects/ tree; it names no object and no row.
        AssetCas::discardStaged(staged.stagedBytes);
        for (const QString &oid : createdOids) {
            QSqlQuery still(conn);
            still.prepare("SELECT 1 FROM files WHERE oid = ?");
            still.addBindValue(oid);
            if (still.exec() && still.next()) continue;
            const QDir fanout(QFileInfo(AssetStorePaths::objectPathIn(root, oid, QStringLiteral("x"))).absolutePath());
            for (const QFileInfo &candidate : fanout.entryInfoList({ oid + ".*", oid }, QDir::Files))
                QFile::remove(candidate.absoluteFilePath());
        }
    };

    // -------- .jaf archives: rows come from the archive's own asset.db ------
    if (!staged.jaf.kind.isEmpty()) {
        QMap<QString, QString> guidCompareMap;
        QVector<AssetRecord> records;

        if (staged.jaf.kind == QStringLiteral("bundle")) {
            const QString guid = db->importAssetBundle(staged.jaf.dbPath, QMap<QString, QString>(),
                                                       guidCompareMap, records, projectGuid);
            staged.mainGuid = guid;
            result.guidMap = guidCompareMap;
            for (auto it = guidCompareMap.constBegin(); it != guidCompareMap.constEnd(); ++it) {
                if (!staged.jaf.bundleLines.contains(it.key())) continue;
                const QDir memberDir(QDir(staged.jaf.assetsDir).filePath(it.key()));
                const QString memberName = db->fetchAsset(it.value()).name;
                QDirIterator files(memberDir.absolutePath(), QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden);
                while (files.hasNext()) {
                    const QFileInfo info(files.next());
                    const QString role = (info.fileName() == memberName)
                                             ? QStringLiteral("source") : QStringLiteral("file");
                    QString oid;
                    if (!storeOneFile(conn, root, staged, info.absoluteFilePath(), it.value(),
                                      role, info.fileName(), &oid, &result.error)) {
                        rollbackAndCleanupObjects();
                        return false;
                    }
                    createdOids.append(oid);
                    result.objectOids.append(oid);
                }
                touchedGuids.append(it.value());
            }
        } else {
            ModelTypes jafType = ModelTypes::Undefined;
            if (staged.jaf.kind == QStringLiteral("object")) jafType = ModelTypes::Object;
            else if (staged.jaf.kind == QStringLiteral("texture")) jafType = ModelTypes::Texture;
            else if (staged.jaf.kind == QStringLiteral("material")) jafType = ModelTypes::Material;
            // NOT "shader" (fix round F11): a .jaf claiming that kind was the
            // last door left open onto a ModelTypes::Shader row, and nothing
            // in this build can read one. Undefined means the import is
            // refused by name below rather than landing a row that shows a
            // tile nobody can open.
            else if (staged.jaf.kind == QStringLiteral("sky")) jafType = ModelTypes::Sky;
            else if (staged.jaf.kind == QStringLiteral("particle_system")) jafType = ModelTypes::ParticleSystem;

            if (jafType == ModelTypes::Undefined) {
                result.error = QStringLiteral(
                                   "this archive carries a '%1', which this version of Jahshaka "
                                   "does not import")
                                   .arg(staged.jaf.kind);
                rollbackAndCleanupObjects();
                return false;
            }

            const QString guid = db->importAsset(jafType, staged.jaf.dbPath, QMap<QString, QString>(),
                                                 guidCompareMap, records,
                                                 AssetViewFilter::AssetsView, projectGuid);
            staged.mainGuid = guid;
            result.guidMap = guidCompareMap;
            const QString assetName = db->fetchAsset(guid).name;

            QDirIterator files(staged.jaf.assetsDir, QDir::NoDotAndDotDot | QDir::Files | QDir::Hidden);
            while (files.hasNext()) {
                const QFileInfo info(files.next());
                const QString role = (info.fileName() == assetName)
                                         ? QStringLiteral("source") : QStringLiteral("file");
                QString oid;
                if (!storeOneFile(conn, root, staged, info.absoluteFilePath(), guid,
                                  role, info.fileName(), &oid, &result.error)) {
                    rollbackAndCleanupObjects();
                    return false;
                }
                createdOids.append(oid);
                result.objectOids.append(oid);
            }
            touchedGuids.append(guid);
        }

        if (staged.mainGuid.isEmpty()) {
            result.error = QStringLiteral("the archive's catalog could not be imported");
            rollbackAndCleanupObjects();
            return false;
        }
    }
    // -------- regular imports: the importer's staged plan -------------------
    else {
        for (const StagedRow &row : staged.rows) {
            // The main row carries the metadata + determinism record.
            QByteArray properties = row.properties;
            if (row.guid == staged.mainGuid) {
                QJsonObject props = QJsonDocument::fromJson(properties).object();
                if (!staged.metadata.isEmpty()) props["metadata"] = staged.metadata;
                props["import"] = staged.importRecord;
                properties = QJsonDocument(props).toJson();
            }
            db->createAssetEntry(row.guid, row.name, row.type, row.parent, projectGuid,
                                 QString(), QString(), row.thumbnail, properties,
                                 row.tags, row.asset,
                                 static_cast<AssetViewFilter>(row.viewFilter));
            touchedGuids.append(row.guid);
        }
        for (const StagedDep &dep : staged.deps)
            db->createDependency(dep.dependerType, dep.dependeeType,
                                 dep.depender, dep.dependee,
                                 dep.projectGuid.isEmpty() ? projectGuid : dep.projectGuid);

        int done = 0;
        for (const StagedFile &file : staged.files) {
            if (progress && !progress(QStringLiteral("store"), done, staged.files.size())) {
                result.error = QStringLiteral("cancelled");
                rollbackAndCleanupObjects();
                return false;
            }
            QString oid;
            if (!storeOneFile(conn, root, staged, file.path, file.forGuid,
                              file.role, file.name, &oid, &result.error)) {
                rollbackAndCleanupObjects();
                return false;
            }
            createdOids.append(oid);
            if (!result.objectOids.contains(oid)) result.objectOids.append(oid);
            ++done;
        }
    }

    committed = true;   // commit() rolls back itself on failure; see database.h
    if (!tx.commit()) {
        result.error = QStringLiteral("could not commit the import transaction");
        rollbackAndCleanupObjects();
        return false;
    }

    // Post-commit, non-fatal: the sidecars (invariant I2). The legacy
    // hardlink view USED to be materialized here too — it is retired (deep
    // audit 2026-09, area 6): every reader resolves through the CAS now, and
    // on a filesystem without hardlinks the view was a second full copy of
    // every imported file (Windows: 152MB store → 438MB, a second full write
    // per import).
    touchedGuids.removeDuplicates();
    for (const QString &guid : touchedGuids) {
        QString casError;
        AssetCas::writeSidecar(conn, root, guid, &casError);
        if (!casError.isEmpty()) irisLog("import post-commit: " + casError);
    }

    // Anything prepare() staged that this plan never named (it should be
    // nothing) would otherwise sit in objects/ as a stale temp until the GC.
    AssetCas::discardStaged(staged.stagedBytes);

    if (staged.registerSession) staged.registerSession();
    return true;
}

QJsonObject AssetImportService::importSettings(const QString &guid) const
{
    if (!db) return QJsonObject();
    const auto record = db->fetchAsset(guid);
    const QJsonObject props = QJsonDocument::fromJson(record.properties).object();
    return props.value(QStringLiteral("import")).toObject();
}

AssetImportService::Reimported AssetImportService::reimport(const QString &guid,
                                                            const QJsonObject &settings)
{
    Reimported out;
    out.guid = guid;
    const auto fail = [&out](const QString &why) { out.error = why; return out; };
    if (!db) return fail(QStringLiteral("no library is open"));

    const AssetRecord record = db->fetchAsset(guid);
    if (record.guid.isEmpty())
        return fail(QStringLiteral("no asset with guid '%1'").arg(guid));
    if (record.type != static_cast<int>(ModelTypes::Object))
        return fail(QStringLiteral("'%1' is not a model asset — only a model is imported with "
                                   "settings").arg(record.name));

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    QString sourceName;
    const QString sourcePath = AssetCas::resolveSource(conn, root, guid, &sourceName);
    if (sourcePath.isEmpty())
        return fail(QStringLiteral("'%1' has no stored source file to reimport").arg(record.name));
    out.sourcePath = sourcePath;

    // MERGED over the stored record, key by key: a caller sends only what it is
    // changing, exactly as the dialog does when a user touches one field.
    QJsonObject props = QJsonDocument::fromJson(record.properties).object();
    QJsonObject importRecord = props.value(QStringLiteral("import")).toObject();
    QJsonObject merged = importRecord.value(QStringLiteral("settings")).toObject();
    for (auto it = settings.constBegin(); it != settings.constEnd(); ++it)
        merged.insert(it.key(), it.value());

    // The object path is oid-named — the importer sniffs by EXTENSION, so the
    // re-read goes through a staging copy named as the recorded display name
    // (the checkConsistency recipe).
    QTemporaryDir staging;
    if (!staging.isValid()) return fail(QStringLiteral("cannot create a reimport staging dir"));
    const QString linked = QDir(staging.path()).filePath(
        sourceName.isEmpty() ? QFileInfo(sourcePath).fileName() : sourceName);
    if (!QFile::copy(sourcePath, linked))
        return fail(QStringLiteral("could not stage '%1' for reimport").arg(record.name));

    ImportRequest request;
    request.sourcePath = linked;
    request.settings = merged;
    request.projectGuid = project ? project->getProjectGuid() : QString();

    QString error;
    AssetImporterBase *importer = pickImporter(request, &error);
    if (!importer || importer->modelType() != static_cast<int>(ModelTypes::Mesh))
        return fail(error.isEmpty()
                        ? QStringLiteral("'%1' is not a model file this build imports")
                              .arg(record.name)
                        : error);

    QTemporaryDir convertStaging;
    StagedAsset staged;
    if (!importer->convert(request, convertStaging.path(), db, project, staged, &error, {}))
        return fail(error.isEmpty() ? QStringLiteral("reimport failed") : error);

    // ---- commit ONLY the derived products ---------------------------------
    //
    // A reimport is not a new import. The source bytes did not change, so the
    // source oid, this row's guid, its member Texture rows and every project's
    // pin stay exactly as they are; what is replaced is what was DERIVED from
    // those bytes under the old settings.
    // The MESH MEMBER row — the guid a scene node actually names
    // (SceneReader::createMesh writes it to MeshNode::meshPath), which is why
    // the bake has to be recorded under it too and why the open-scene swap
    // finds nodes by it. Same lookup SceneEditService::addMaterialMesh uses.
    const QString meshGuid = db->fetchObjectMesh(guid, static_cast<int>(ModelTypes::Object),
                                                 static_cast<int>(ModelTypes::Mesh));
    out.meshGuid = meshGuid;

    // ONE TRANSACTION over the whole commit (the second read's F4): unlinking
    // the old bake, ingesting the new one under both rows and writing the
    // record are one change, and a failure halfway used to leave an asset with
    // NO bake and its old settings still recorded — which parses on every open
    // and says the wrong thing about why. The object BYTES are not
    // transactional (the same half-transactional story `commit` documents at
    // length), but a bake object with no row naming it is exactly what
    // assets.gc reaps, so the worst case is collectable rather than wrong.
    DbTransaction tx(conn);

    // The previous bake, remembered before it is unlinked so a caller can say
    // what assets.gc will reap.
    {
        QSqlQuery old(conn);
        old.prepare("SELECT oid FROM asset_files WHERE asset_guid = ? AND role = ?");
        old.addBindValue(guid);
        old.addBindValue(iris::MeshBake::casRole());
        if (old.exec() && old.next()) out.previousBakeOid = old.value(0).toString();
    }

    QString bakePath;
    QString bakeName;
    for (const StagedFile &file : staged.files) {
        if (file.role != iris::MeshBake::casRole()) continue;
        bakePath = file.path;
        bakeName = file.name;
        break;
    }

    // Unlink the OLD bake rows FIRST: the bake's name carries the settings
    // hash, so the new one is a different row and a stale row would otherwise
    // sit in front of it in the newest-first candidate walk forever.
    QSqlQuery drop(conn);
    drop.prepare("DELETE FROM asset_files WHERE asset_guid IN (?, ?) AND role = ?");
    drop.addBindValue(guid);
    drop.addBindValue(meshGuid);
    drop.addBindValue(iris::MeshBake::casRole());
    if (!drop.exec())
        return fail(QStringLiteral("could not retire the old bake of '%1'").arg(record.name));

    if (!bakePath.isEmpty()) {
        QString oid;
        if (!AssetCas::ingestFile(conn, root, bakePath, guid, iris::MeshBake::casRole(),
                                  bakeName, &oid, &error))
            return fail(error);
        out.bakeOid = oid;
        if (!meshGuid.isEmpty()
            && !AssetCas::ingestFile(conn, root, bakePath, meshGuid, iris::MeshBake::casRole(),
                                     bakeName, &oid, &error))
            return fail(error);
    }

    // The metadata block (the extent is re-measured from the transformed parse
    // the convert stage just made) and the settings record.
    out.metadata = staged.metadata;
    out.settings = staged.appliedSettings;
    if (!out.metadata.isEmpty()) props[QStringLiteral("metadata")] = out.metadata;
    importRecord[QStringLiteral("settings")] = out.settings;
    importRecord[QStringLiteral("importerVersion")] = importer->version();
    props[QStringLiteral("import")] = importRecord;
    if (!db->updateAssetProperties(guid, QJsonDocument(props).toJson()))
        return fail(QStringLiteral("could not record the new settings on '%1'").arg(record.name));
    if (!tx.commit())
        return fail(QStringLiteral("could not commit the reimport of '%1'").arg(record.name));

    // THE MEMO, dropped HERE and not by the caller (the second read's F5):
    // MeshBakeStore's settings cache is keyed by (path, guid) and is never
    // otherwise invalidated, so a caller that forgot this — a dialog calling
    // the service directly instead of the verb — would serve the OLD transform
    // for the rest of the session.
    MeshBakeStore::clear();

    QString casError;
    AssetCas::writeSidecar(conn, root, guid, &casError);
    if (!meshGuid.isEmpty()) AssetCas::writeSidecar(conn, root, meshGuid, &casError);
    if (!casError.isEmpty()) irisLog("reimport: " + casError);
    return out;
}

QJsonObject AssetImportService::checkConsistency(const QString &guid)
{
    QJsonObject report;
    report["guid"] = guid;
    if (!db) { report["ok"] = false; report["error"] = "no database"; return report; }

    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();

    QString sourceName;
    const QString sourcePath = AssetCas::resolveSource(conn, root, guid, &sourceName);
    if (sourcePath.isEmpty()) {
        report["ok"] = false;
        report["error"] = QStringLiteral("no stored source for %1").arg(guid);
        return report;
    }

    // Re-run the convert stage on the stored bytes with the recorded settings.
    ImportRequest request;
    request.sourcePath = sourcePath;
    request.settings = importSettings(guid).value(QStringLiteral("settings")).toObject();

    // The object path is oid-named — sniff by the RECORDED display name's
    // extension via a staging link named like the original.
    QTemporaryDir staging;
    if (!staging.isValid()) { report["ok"] = false; report["error"] = "no staging dir"; return report; }
    const QString linked = QDir(staging.path()).filePath(sourceName);
    QFile::copy(sourcePath, linked);
    request.sourcePath = linked;

    QString error;
    AssetImporterBase *importer = pickImporter(request, &error);
    if (!importer) { report["ok"] = false; report["error"] = error; return report; }

    QTemporaryDir convertStaging;
    StagedAsset staged;
    if (!importer->convert(request, convertStaging.path(), db, project, staged, &error, {})) {
        report["ok"] = false;
        report["error"] = error;
        return report;
    }

    // Produced object set = hashes of every staged content file.
    QSet<QString> produced;
    for (const StagedFile &file : staged.files) {
        const QString oid = AssetCas::hashFile(file.path);
        if (!oid.isEmpty()) produced.insert(oid);
    }

    // Expected = the catalog's recorded objects for this asset.
    QSet<QString> expected;
    QSqlQuery recorded(conn);
    recorded.prepare("SELECT oid FROM asset_files WHERE asset_guid = ?");
    recorded.addBindValue(guid);
    if (recorded.exec())
        while (recorded.next()) expected.insert(recorded.value(0).toString());

    QJsonArray expectedArr, producedArr, missingArr, extraArr;
    for (const auto &oid : expected) expectedArr.append(oid);
    for (const auto &oid : produced) producedArr.append(oid);
    for (const auto &oid : expected)
        if (!produced.contains(oid)) missingArr.append(oid);
    for (const auto &oid : produced)
        if (!expected.contains(oid)) extraArr.append(oid);

    report["ok"] = true;
    report["consistent"] = (expected == produced);
    report["expected"] = expectedArr;
    report["produced"] = producedArr;
    report["missingFromReimport"] = missingArr;
    report["extraFromReimport"] = extraArr;
    return report;
}
