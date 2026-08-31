/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "services/assetcas.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <QUuid>

#ifdef Q_OS_UNIX
#include <unistd.h>   // link(2) — hardlink migration, preflight §3.3
#endif

#include "data/database/casschema.h"
#include "services/assetstorepaths.h"

namespace AssetCas
{

QString hashFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) return QString();
    return QString::fromLatin1(hash.result().toHex());   // lowercase hex
}

bool storeObject(const QString &srcPath, const QString &root,
                 const QString &oid, const QString &ext, QString *errorOut)
{
    const QString dstPath = AssetStorePaths::objectPathIn(root, oid, ext);
    if (QFileInfo::exists(dstPath)) {
        if (QFileInfo(dstPath).size() == QFileInfo(srcPath).size()) return true;
        // Same oid, wrong size = a torn write from an interrupted run —
        // replace it (content addressing makes this safe).
        QFile::remove(dstPath);
    }
    if (!QDir().mkpath(QFileInfo(dstPath).absolutePath())) {
        if (errorOut) *errorOut = QStringLiteral("cannot create %1").arg(QFileInfo(dstPath).absolutePath());
        return false;
    }

#ifdef Q_OS_UNIX
    // Hardlink first: same filesystem = ~0 extra bytes and instant; falls
    // back to a copy across devices (EXDEV) or on filesystems without links.
    if (::link(QFile::encodeName(srcPath).constData(),
               QFile::encodeName(dstPath).constData()) == 0) {
        return true;
    }
#endif
    if (!QFile::copy(srcPath, dstPath)) {
        if (errorOut) *errorOut = QStringLiteral("copy failed: %1 -> %2").arg(srcPath, dstPath);
        return false;
    }
    return true;
}

void ensureCasSchema(QSqlDatabase conn)
{
    QSqlQuery(CasSchema::kFilesTable, conn);
    QSqlQuery(CasSchema::kAssetFilesTable, conn);
    QSqlQuery(CasSchema::kAssetFilesOidIndex, conn);
    QSqlQuery(CasSchema::kRefcountInsertTrigger, conn);
    QSqlQuery(CasSchema::kRefcountDeleteTrigger, conn);

    // PRAGMA user_version arrives with the CAS (spec §3.1.3) — informational
    // at this phase; never lowered.
    QSqlQuery versionQuery(conn);
    versionQuery.exec("PRAGMA user_version");
    int current = 0;
    if (versionQuery.next()) current = versionQuery.value(0).toInt();
    if (current < CasSchema::kUserVersion)
        QSqlQuery(QStringLiteral("PRAGMA user_version = %1").arg(CasSchema::kUserVersion), conn);
}

bool ingestLegacyFolder(QSqlDatabase conn, const QString &root, const QString &guid,
                        const QString &assetName, IngestStats *stats, QString *errorOut)
{
    const QString folder = AssetStorePaths::legacyFolderIn(root, guid);
    if (!QDir(folder).exists()) return true;   // zero files, not an error

    QDirIterator it(folder, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString srcPath = it.next();
        const QFileInfo info(srcPath);

        const QString oid = hashFile(srcPath);
        if (oid.isEmpty()) {
            if (errorOut) *errorOut = QStringLiteral("cannot hash %1").arg(srcPath);
            return false;
        }
        if (stats) { ++stats->files; stats->bytesHashed += info.size(); }

        // One object per oid: when the catalog already knows this content,
        // its recorded extension is canonical — a same-bytes file arriving
        // under another extension (jpeg vs jpg) reuses the existing object
        // instead of writing a sibling copy.
        QString ext = info.suffix().toLower();
        {
            QSqlQuery known(conn);
            known.prepare("SELECT ext FROM files WHERE oid = ?");
            known.addBindValue(oid);
            if (known.exec() && known.next()) ext = known.value(0).toString();
        }

        const bool existed = QFileInfo::exists(AssetStorePaths::objectPathIn(root, oid, ext));
        if (!storeObject(srcPath, root, oid, ext, errorOut)) return false;
        if (stats) { existed ? ++stats->objectsReused : ++stats->objectsCreated; }

        QSqlQuery insertFile(conn);
        insertFile.prepare("INSERT OR IGNORE INTO files (oid, size, ext, refcount) VALUES (?, ?, ?, 0)");
        insertFile.addBindValue(oid);
        insertFile.addBindValue(info.size());
        insertFile.addBindValue(ext);
        insertFile.exec();

        // The role: the file matching the asset's recorded name is the
        // primary 'source'; everything else rides along as 'file'.
        const QString role = (info.fileName() == assetName) ? QStringLiteral("source")
                                                            : QStringLiteral("file");
        QSqlQuery insertLink(conn);
        insertLink.prepare("INSERT OR IGNORE INTO asset_files (asset_guid, role, oid, name) VALUES (?, ?, ?, ?)");
        insertLink.addBindValue(guid);
        insertLink.addBindValue(role);
        insertLink.addBindValue(oid);
        insertLink.addBindValue(info.fileName());
        insertLink.exec();
    }
    return true;
}

bool ingestFile(QSqlDatabase conn, const QString &root, const QString &srcPath,
                const QString &guid, const QString &role, const QString &name,
                QString *oidOut, QString *errorOut)
{
    const QFileInfo info(srcPath);
    if (!info.exists() || !info.isFile()) {
        if (errorOut) *errorOut = QStringLiteral("no such file %1").arg(srcPath);
        return false;
    }

    const QString oid = hashFile(srcPath);
    if (oid.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("cannot hash %1").arg(srcPath);
        return false;
    }
    if (oidOut) *oidOut = oid;

    // Known content keeps its recorded extension (jpeg/jpg aliasing — one
    // object per oid, never a sibling copy under another name).
    QString ext = info.suffix().toLower();
    {
        QSqlQuery known(conn);
        known.prepare("SELECT ext FROM files WHERE oid = ?");
        known.addBindValue(oid);
        if (known.exec() && known.next()) ext = known.value(0).toString();
    }

    if (!storeObject(srcPath, root, oid, ext, errorOut)) return false;

    QSqlQuery insertFile(conn);
    insertFile.prepare("INSERT OR IGNORE INTO files (oid, size, ext, refcount) VALUES (?, ?, ?, 0)");
    insertFile.addBindValue(oid);
    insertFile.addBindValue(info.size());
    insertFile.addBindValue(ext);
    insertFile.exec();

    QSqlQuery insertLink(conn);
    insertLink.prepare("INSERT OR IGNORE INTO asset_files (asset_guid, role, oid, name) VALUES (?, ?, ?, ?)");
    insertLink.addBindValue(guid);
    insertLink.addBindValue(role);
    insertLink.addBindValue(oid);
    insertLink.addBindValue(name.isEmpty() ? info.fileName() : name);
    insertLink.exec();
    return true;
}

bool materializeLegacyView(QSqlDatabase conn, const QString &root,
                           const QString &guid, QString *errorOut)
{
    QSqlQuery query(conn);
    query.prepare("SELECT AF.name, AF.oid, F.ext FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.asset_guid = ?");
    query.addBindValue(guid);
    if (!query.exec()) {
        if (errorOut) *errorOut = QStringLiteral("asset_files query failed for %1").arg(guid);
        return false;
    }

    const QString folder = AssetStorePaths::legacyFolderIn(root, guid);
    bool any = false;
    while (query.next()) {
        const QString name = query.value(0).toString();
        const QString objPath = AssetStorePaths::objectPathIn(
            root, query.value(1).toString(), query.value(2).toString());
        if (!QFileInfo::exists(objPath)) continue;
        if (!any) {
            if (!QDir().mkpath(folder)) {
                if (errorOut) *errorOut = QStringLiteral("cannot create %1").arg(folder);
                return false;
            }
            any = true;
        }
        const QString dstPath = QDir(folder).filePath(name);
        if (QFileInfo::exists(dstPath)) continue;
#ifdef Q_OS_UNIX
        if (::link(QFile::encodeName(objPath).constData(),
                   QFile::encodeName(dstPath).constData()) == 0)
            continue;
#endif
        if (!QFile::copy(objPath, dstPath)) {
            if (errorOut) *errorOut = QStringLiteral("copy failed: %1 -> %2").arg(objPath, dstPath);
            return false;
        }
    }
    return true;
}

QString resolveSource(QSqlDatabase conn, const QString &root,
                      const QString &guid, QString *nameOut)
{
    QSqlQuery query(conn);
    query.prepare("SELECT AF.name, AF.oid, F.ext FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.asset_guid = ? "
                  "ORDER BY CASE AF.role WHEN 'source' THEN 0 ELSE 1 END, AF.name");
    query.addBindValue(guid);
    if (query.exec() && query.next()) {
        const QString path = AssetStorePaths::objectPathIn(
            root, query.value(1).toString(), query.value(2).toString());
        if (QFileInfo::exists(path)) {
            if (nameOut) *nameOut = query.value(0).toString();
            return path;
        }
        // Object missing (offline root, purged store): legacy fallback below.
        const QString legacy = QDir(AssetStorePaths::legacyFolderIn(root, guid))
                                   .filePath(query.value(0).toString());
        if (QFileInfo::exists(legacy)) {
            if (nameOut) *nameOut = query.value(0).toString();
            return legacy;
        }
    }
    return QString();
}

bool writeSidecar(QSqlDatabase conn, const QString &root, const QString &guid,
                  QString *errorOut)
{
    QSqlQuery assetQuery(conn);
    assetQuery.prepare("SELECT name, type, view_filter, collection, author, license, properties, tags "
                       "FROM assets WHERE guid = ?");
    assetQuery.addBindValue(guid);
    if (!assetQuery.exec() || !assetQuery.next()) {
        if (errorOut) *errorOut = QStringLiteral("no asset row for %1").arg(guid);
        return false;
    }

    QJsonObject sidecar;
    sidecar["formatVersion"] = 1;
    sidecar["guid"] = guid;
    sidecar["name"] = assetQuery.value(0).toString();
    sidecar["type"] = assetQuery.value(1).toInt();
    sidecar["viewFilter"] = assetQuery.value(2).toInt();
    sidecar["collection"] = assetQuery.value(3).toInt();
    sidecar["author"] = assetQuery.value(4).toString();
    sidecar["license"] = assetQuery.value(5).toString();
    const QJsonDocument props = QJsonDocument::fromJson(assetQuery.value(6).toByteArray());
    if (props.isObject()) sidecar["properties"] = props.object();
    const QJsonDocument tags = QJsonDocument::fromJson(assetQuery.value(7).toByteArray());
    if (tags.isObject()) sidecar["tags"] = tags.object();

    QJsonArray files;
    QSqlQuery filesQuery(conn);
    filesQuery.prepare("SELECT AF.role, AF.oid, AF.name, F.size, F.ext "
                       "FROM asset_files AF LEFT JOIN files F ON AF.oid = F.oid "
                       "WHERE AF.asset_guid = ? ORDER BY AF.role, AF.name");
    filesQuery.addBindValue(guid);
    filesQuery.exec();
    while (filesQuery.next()) {
        QJsonObject file;
        file["role"] = filesQuery.value(0).toString();
        file["oid"] = filesQuery.value(1).toString();
        file["name"] = filesQuery.value(2).toString();
        file["size"] = filesQuery.value(3).toDouble();
        file["ext"] = filesQuery.value(4).toString();
        files.append(file);
    }
    sidecar["files"] = files;

    const QString path = AssetStorePaths::sidecarPathIn(root, guid);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        if (errorOut) *errorOut = QStringLiteral("cannot create sidecar dir for %1").arg(guid);
        return false;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    out.write(QJsonDocument(sidecar).toJson(QJsonDocument::Indented));
    return true;
}

QString resolveFile(QSqlDatabase conn, const QString &root,
                    const QString &guid, const QString &name)
{
    // asset_files first (invariant I1)…
    QSqlQuery query(conn);
    query.prepare("SELECT AF.oid, F.ext FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid "
                  "WHERE AF.asset_guid = ? AND AF.name = ?");
    query.addBindValue(guid);
    query.addBindValue(name);
    if (query.exec() && query.next()) {
        const QString path = AssetStorePaths::objectPathIn(
            root, query.value(0).toString(), query.value(1).toString());
        if (QFileInfo::exists(path)) return path;
    }
    // …legacy per-guid folder as the one-release read fallback.
    const QString legacy = QDir(AssetStorePaths::legacyFolderIn(root, guid)).filePath(name);
    if (QFileInfo::exists(legacy)) return legacy;
    return QString();
}

bool writeStoreInfo(const QString &root, QString *errorOut)
{
    const QString path = AssetStorePaths::storeInfoPathIn(root);

    QString storeId;
    {
        QFile existing(path);
        if (existing.open(QIODevice::ReadOnly)) {
            const QJsonObject obj = QJsonDocument::fromJson(existing.readAll()).object();
            storeId = obj.value("storeId").toString();
        }
    }
    if (storeId.isEmpty()) storeId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QJsonObject info;
    info["storeId"] = storeId;
    info["formatVersion"] = 1;

    if (!QDir().mkpath(root)) {
        if (errorOut) *errorOut = QStringLiteral("cannot create %1").arg(root);
        return false;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut) *errorOut = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    out.write(QJsonDocument(info).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace AssetCas
