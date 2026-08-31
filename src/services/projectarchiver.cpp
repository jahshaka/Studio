/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/projectarchiver.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>

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

} // namespace

ProjectArchiver::Result ProjectArchiver::exportArchive(Database *db, Project *project,
                                                       const QString &destZipPath)
{
    Result result;
    if (!db || !project || project->getProjectGuid().isEmpty()) {
        result.error = QStringLiteral("no open project");
        return result;
    }
    const QString projectGuid = project->getProjectGuid();

    QTemporaryDir stage;
    if (!stage.isValid()) {
        result.error = QStringLiteral("cannot create a staging directory");
        return result;
    }

    // 1. The catalog snapshot (pin-aware since phase 4).
    db->createExportScene(stage.path(), projectGuid);
    if (!QFileInfo::exists(QDir(stage.path()).filePath(projectGuid + ".db"))) {
        result.error = QStringLiteral("could not write the catalog snapshot");
        return result;
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

    ExportManifest manifest;
    manifest.kind = QStringLiteral("project");
    manifest.generator = QStringLiteral("Jahshaka");
    manifest.created = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    const QString objectsDir = QDir(stage.path()).filePath(QStringLiteral("objects"));
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

            const QString objPath = AssetStorePaths::objectPathIn(root, file.oid, ext);
            if (!file.oid.isEmpty() && QFileInfo::exists(objPath) && !written.contains(file.oid)) {
                QDir().mkpath(objectsDir);
                QFile::copy(objPath, QDir(objectsDir).filePath(file.oid + "." + ext));
                written.insert(file.oid);
                ++result.objects;
            }
            asset.files.append(file);
        }

        manifest.assets.append(asset);
        ++result.assets;
    }

    if (!manifest.write(QDir(stage.path()).filePath(exportformat::manifestFileName()),
                        &result.error))
        return result;

    // Legacy .manifest marker so pre-v2 validators recognise the archive.
    {
        QFile marker(QDir(stage.path()).filePath(QStringLiteral(".manifest")));
        if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate))
            marker.write("project\n");
    }

    if (!ZipHelper::zipDirectory(stage.path(), destZipPath, &result.error)) return result;
    result.path = destZipPath;
    return result;
}

ProjectArchiver::Result ProjectArchiver::importArchive(Database *db, const QString &zipPath)
{
    Result result;
    if (!db) { result.error = QStringLiteral("no database"); return result; }

    QTemporaryDir stage;
    if (!stage.isValid()) {
        result.error = QStringLiteral("cannot create a staging directory");
        return result;
    }
    if (!ZipHelper::extract(zipPath, stage.path(), &result.error)) return result;

    // The catalog snapshot: <guid>.db.
    QString blobDbBase;
    for (const QFileInfo &entry : QDir(stage.path()).entryInfoList(QDir::Files)) {
        if (entry.suffix() == QStringLiteral("db")) { blobDbBase = entry.completeBaseName(); break; }
    }
    if (blobDbBase.isEmpty()) {
        result.error = QStringLiteral("not a Jahshaka project archive (no catalog snapshot)");
        return result;
    }
    if (!db->checkIfProjectVersionSupported(QDir(stage.path()).filePath(blobDbBase + ".db"))) {
        result.error = QStringLiteral("this scene was made with an unsupported version of Jahshaka");
        return result;
    }

    QString manifestError;
    const ExportManifest manifest = ExportManifest::fromFile(
        QDir(stage.path()).filePath(exportformat::manifestFileName()), &manifestError);

    // Catalog rows (fresh guids; scene blob remapped inside importProject).
    const QString newProjectGuid = GUIDManager::generateGUID();
    QMap<QString, QString> guidMap;
    if (!db->importProject(QDir(stage.path()).filePath(blobDbBase), newProjectGuid,
                           result.worldName, guidMap)) {
        result.error = QStringLiteral("the archive's catalog could not be imported");
        return result;
    }

    // Objects + asset_files + pins, from the manifest (v2 archives; a legacy
    // archive without one imports rows only — its flat files are ignored,
    // there is no flat-folder world to put them in).
    QSqlDatabase conn = QSqlDatabase::database();
    const QString root = AssetStorePaths::root();
    const QDir objectsDir(QDir(stage.path()).filePath(QStringLiteral("objects")));

    if (manifest.isValid() && manifest.version >= 2) {
        for (const ManifestAsset &asset : manifest.assets) {
            const QString localGuid = guidMap.value(asset.guid, asset.guid);
            QString sourceOid;
            for (const ManifestFile &file : asset.files) {
                // objects/<oid>.<ext> — find by oid prefix (ext recorded in name).
                QString objectFile;
                const auto candidates = objectsDir.entryInfoList({ file.oid + ".*", file.oid }, QDir::Files);
                if (!candidates.isEmpty()) objectFile = candidates.first().absoluteFilePath();
                if (objectFile.isEmpty()) continue;

                QString oid;
                if (!AssetCas::ingestFile(conn, root, objectFile, localGuid,
                                          file.role, file.name, &oid, &result.error))
                    return result;
                if (sourceOid.isEmpty() || file.role == QStringLiteral("source"))
                    if (sourceOid.isEmpty()) sourceOid = oid;
                ++result.objects;
            }
            AssetCas::writePin(conn, newProjectGuid, localGuid, sourceOid);
            ++result.assets;
        }
    }

    result.projectGuid = newProjectGuid;
    result.path = stage.path();
    return result;
}
