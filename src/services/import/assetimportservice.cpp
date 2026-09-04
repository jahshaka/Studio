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
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

#include "assimp/version.h"

#include "data/database/database.h"
#include "data/project.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/import/assetimporters.h"
#include "irisgl/core/logger.h"

AssetImportService::AssetImportService(Database *db, Project *project)
    : db(db), project(project)
{
    mImporters.append(new MeshImporter());
    mImporters.append(new MediaImporter(static_cast<int>(ModelTypes::Texture)));
    mImporters.append(new MediaImporter(static_cast<int>(ModelTypes::Music)));
    mImporters.append(new MediaImporter(static_cast<int>(ModelTypes::Video)));
    mImporters.append(new ShaderImporter());
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

AssetImporterBase *AssetImportService::pickImporter(const ImportRequest &request,
                                                    QString *error) const
{
    for (auto *importer : mImporters) {
        if (request.typeHint >= 0 && importer->modelType() != request.typeHint) continue;
        if (importer->sniff(request.sourcePath)) return importer;
    }
    if (error)
        *error = QStringLiteral("'%1' is not an importable library file "
                                "(models, images, audio, video, shaders, materials, "
                                ".ies light profiles or .jaf)")
                     .arg(QFileInfo(request.sourcePath).fileName());
    return nullptr;
}

ImportResult AssetImportService::import(const ImportRequest &request,
                                        const ImportProgressFn &progress)
{
    PreparedImport prepared = prepare(request, progress);
    if (!prepared.ok()) return prepared.result;
    return commit(prepared, progress);
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
    // version + assimp version. Recorded on the row; assets.importSettings
    // reads it back and assets.checkConsistency re-derives the object set.
    staged.importRecord = QJsonObject{
        { "sourceOid", AssetCas::hashFile(request.sourcePath) },
        { "importer", importer->name() },
        { "importerVersion", importer->version() },
        { "assimp", QStringLiteral("%1.%2.%3").arg(aiGetVersionMajor())
                        .arg(aiGetVersionMinor()).arg(aiGetVersionRevision()) },
        { "settings", request.settings },
    };

    // Prepay the content hashing (the CPU cost of the store stage) so the
    // DB-thread commit never re-hashes; a cancel here still costs nothing —
    // no store/DB writes have happened yet.
    for (const StagedFile &file : staged.files) {
        if (staged.fileOids.contains(file.path)) continue;
        if (progress && !progress(QStringLiteral("hash"),
                                  staged.fileOids.size(), staged.files.size())) {
            result.error = QStringLiteral("cancelled");
            return prepared;
        }
        const QString oid = AssetCas::hashFile(file.path);
        if (!oid.isEmpty()) staged.fileOids.insert(file.path, oid);
    }
    return prepared;
}

ImportResult AssetImportService::commit(PreparedImport &prepared,
                                        const ImportProgressFn &progress)
{
    const ImportRequest &request = prepared.request;
    StagedAsset &staged = prepared.staged;
    ImportResult result = prepared.result;

    // ---- store + register (one transaction) ----
    if (!commitStagedAsset(request, staged, result, progress)) return result;

    result.assetGuid = staged.mainGuid;
    result.meshGuid = staged.meshGuid;
    result.node = staged.node;
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

    auto cleanupObjects = [&]() {
        // Remove objects this import wrote whose files row did not survive
        // the rollback (refcount 0 orphans of a failed import).
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

    DbTransaction tx(conn);

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
                    if (!AssetCas::ingestFile(conn, root, info.absoluteFilePath(), it.value(),
                                              role, info.fileName(), &oid, &result.error)) {
                        cleanupObjects();
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
            else if (staged.jaf.kind == QStringLiteral("shader")) jafType = ModelTypes::Shader;
            else if (staged.jaf.kind == QStringLiteral("sky")) jafType = ModelTypes::Sky;
            else if (staged.jaf.kind == QStringLiteral("particle_system")) jafType = ModelTypes::ParticleSystem;

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
                if (!AssetCas::ingestFile(conn, root, info.absoluteFilePath(), guid,
                                          role, info.fileName(), &oid, &result.error)) {
                    cleanupObjects();
                    return false;
                }
                createdOids.append(oid);
                result.objectOids.append(oid);
            }
            touchedGuids.append(guid);
        }

        if (staged.mainGuid.isEmpty()) {
            result.error = QStringLiteral("the archive's catalog could not be imported");
            cleanupObjects();
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
                cleanupObjects();
                return false;
            }
            QString oid;
            if (!AssetCas::ingestFile(conn, root, file.path, file.forGuid,
                                      file.role, file.name, &oid, &result.error,
                                      staged.fileOids.value(file.path))) {
                cleanupObjects();
                return false;
            }
            createdOids.append(oid);
            if (!result.objectOids.contains(oid)) result.objectOids.append(oid);
            ++done;
        }
    }

    if (!tx.commit()) {
        result.error = QStringLiteral("could not commit the import transaction");
        cleanupObjects();
        return false;
    }

    // Post-commit, non-fatal: rebuild sidecars and the legacy hardlink view
    // (the compatibility surface for read sites not yet on the resolver).
    touchedGuids.removeDuplicates();
    for (const QString &guid : touchedGuids) {
        QString casError;
        AssetCas::writeSidecar(conn, root, guid, &casError);
        AssetCas::materializeLegacyView(conn, root, guid, &casError);
        if (!casError.isEmpty()) irisLog("import post-commit: " + casError);
    }

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
