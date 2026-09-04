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

#include "irisgl/core/logger.h"
#include "data/database/casschema.h"
#include "services/assetstorepaths.h"
#include "services/filewriteatomic.h"

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
    const bool    existed = QFileInfo::exists(dstPath);
    if (existed && QFileInfo(dstPath).size() == QFileInfo(srcPath).size())
        return true;   // the dedup: this content is already stored

    if (!QDir().mkpath(QFileInfo(dstPath).absolutePath())) {
        if (errorOut) *errorOut = QStringLiteral("cannot create %1").arg(QFileInfo(dstPath).absolutePath());
        return false;
    }
    if (existed) {
        // Same oid, wrong size = a torn object left by an older build (this
        // function cannot produce one any more — see below). It used to be
        // repaired in silence, which is how a store could carry corrupt
        // objects for weeks; say so.
        iris::Logger::getSingleton()->warn(
            QStringLiteral("asset store: replacing a %1-byte object that claims %2 bytes of "
                           "content (%3) — a torn write from an interrupted run")
                .arg(QFileInfo(dstPath).size()).arg(QFileInfo(srcPath).size()).arg(dstPath));
    }

    // THE STAGING TEMP, and why the object is never written at its own name.
    //
    // The file name IS the sha256 of the bytes, so a partially written object
    // is a file that lies about its content — and nothing re-hashes on read
    // (resolveSource/resolveFile only check existence). Kill the process
    // halfway through and every later run believes the truncated file. The old
    // code had two windows for that: QFile::copy straight to dstPath, and, for
    // the replace case, a QFile::remove BEFORE the replacement existed, which
    // could also destroy a good object outright.
    //
    // Both close the same way: stage into a sibling temp, then one rename
    // (FileWrite::stagingTempPath + FileWrite::atomicRename — the same tail
    // sidecars, store.json and the baked maps now share; deep audit area 6).
    // rename(2) is atomic within a filesystem and REPLACES an existing target,
    // so the remove disappears too. The temp must be in the destination's own
    // directory for that ("same filesystem"), which mkpath above guarantees
    // exists.
    //
    // The staging step itself is NOT a byte write (hardlink first, copy only
    // as the fallback), which is why storeObject drives the pieces rather than
    // calling writeFileAtomic.
    //
    // (Found while building this: Qt 6.10 on Linux already gets QFile::copy
    // atomic by accident — it opens the destination with O_TMPFILE and linkat()s
    // it into place, so the tear could not be reproduced on this box. That is a
    // fast path, not a contract: it needs a filesystem that supports O_TMPFILE,
    // and QFile::copy on macOS and Windows has no such property. The guarantee
    // is ours now, on every platform.)
    const QString tmpPath = FileWrite::stagingTempPath(dstPath);
    QFile::remove(tmpPath);   // a leftover from a dead run that had our pid

    bool staged = false;
#ifdef Q_OS_UNIX
    // Hardlink first: same filesystem = ~0 extra bytes and instant; falls
    // back to a copy across devices (EXDEV) or on filesystems without links.
    // Linking to the TEMP rather than the final name also means no EEXIST
    // special case when replacing.
    staged = ::link(QFile::encodeName(srcPath).constData(),
                    QFile::encodeName(tmpPath).constData()) == 0;
#endif
    if (!staged) {
        if (!QFile::copy(srcPath, tmpPath)) {
            QFile::remove(tmpPath);
            if (errorOut) *errorOut = QStringLiteral("copy failed: %1 -> %2").arg(srcPath, dstPath);
            return false;
        }
        // Flush the COPY (never the link: those bytes are already the source's
        // and already durable). This is the difference between a power cut
        // leaving no object and leaving a correctly-named EMPTY one, which is
        // the exact failure the rename exists to prevent. The directory entry
        // is deliberately NOT fsynced: losing the rename loses the object, and
        // a missing object is a re-ingest, not a corruption.
        FileWrite::fsyncPath(tmpPath);
    }

    return FileWrite::atomicRename(tmpPath, dstPath, errorOut);
}

void ensureCasSchema(QSqlDatabase conn)
{
    QSqlQuery(CasSchema::kFilesTable, conn);
    QSqlQuery(CasSchema::kAssetFilesTable, conn);
    QSqlQuery(CasSchema::kAssetFilesOidIndex, conn);
    QSqlQuery(CasSchema::kRefcountInsertTrigger, conn);
    QSqlQuery(CasSchema::kRefcountDeleteTrigger, conn);
    QSqlQuery(CasSchema::kProjectAssetsTable, conn);

    // PRAGMA user_version arrives with the CAS (spec §3.1.3) — informational
    // at this phase; never lowered.
    QSqlQuery versionQuery(conn);
    versionQuery.exec("PRAGMA user_version");
    int current = 0;
    if (versionQuery.next()) current = versionQuery.value(0).toInt();
    if (current < CasSchema::kUserVersion)
        QSqlQuery(QStringLiteral("PRAGMA user_version = %1").arg(CasSchema::kUserVersion), conn);
}

bool ingestFile(QSqlDatabase conn, const QString &root, const QString &srcPath,
                const QString &guid, const QString &role, const QString &name,
                QString *oidOut, QString *errorOut, const QString &knownOid)
{
    const QFileInfo info(srcPath);
    if (!info.exists() || !info.isFile()) {
        if (errorOut) *errorOut = QStringLiteral("no such file %1").arg(srcPath);
        return false;
    }

    const QString oid = knownOid.isEmpty() ? hashFile(srcPath) : knownOid;
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

    // Atomic, like every other artifact the store owns: this used to truncate
    // the live sidecar and then write, so an interrupted import left a
    // zero/half-length JSON that rebuildCatalog would read as an asset with no
    // files at all (deep audit 2026-09, area 6).
    const QString path = AssetStorePaths::sidecarPathIn(root, guid);
    return FileWrite::writeFileAtomic(
        path, QJsonDocument(sidecar).toJson(QJsonDocument::Indented), errorOut);
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

bool writePin(QSqlDatabase conn, const QString &projectGuid,
              const QString &assetGuid, const QString &oid)
{
    QSqlQuery upsert(conn);
    upsert.prepare("INSERT INTO project_assets (project_guid, asset_guid, oid_pin) "
                   "VALUES (?, ?, ?) "
                   "ON CONFLICT(project_guid, asset_guid) DO UPDATE SET oid_pin = excluded.oid_pin");
    upsert.addBindValue(projectGuid);
    upsert.addBindValue(assetGuid);
    upsert.addBindValue(oid);
    return upsert.exec();
}

QString pinnedOid(QSqlDatabase conn, const QString &projectGuid,
                  const QString &assetGuid)
{
    QSqlQuery query(conn);
    query.prepare("SELECT oid_pin FROM project_assets WHERE project_guid = ? AND asset_guid = ?");
    query.addBindValue(projectGuid);
    query.addBindValue(assetGuid);
    if (query.exec() && query.next()) return query.value(0).toString();
    return QString();
}

QString resolvePinned(QSqlDatabase conn, const QString &root,
                      const QString &projectGuid, const QString &guid,
                      QString *nameOut)
{
    const QString pin = pinnedOid(conn, projectGuid, guid);
    if (!pin.isEmpty()) {
        QSqlQuery query(conn);
        query.prepare("SELECT F.ext, AF.name FROM files F "
                      "LEFT JOIN asset_files AF ON AF.oid = F.oid AND AF.asset_guid = ? "
                      "WHERE F.oid = ?");
        query.addBindValue(guid);
        query.addBindValue(pin);
        if (query.exec() && query.next()) {
            const QString path = AssetStorePaths::objectPathIn(root, pin, query.value(0).toString());
            if (QFileInfo::exists(path)) {
                if (nameOut) *nameOut = query.value(1).toString();
                return path;
            }
        }
    }
    return resolveSource(conn, root, guid, nameOut);
}

QString guidForStorePath(QSqlDatabase conn, const QString &root, const QString &path,
                         const QString &projectGuid)
{
    // THE INVERSE of resolvePinned/resolveSource, and the reason it has to
    // exist: the document holds RESOLVED PATHS (the renderer opens files), the
    // scene blob holds GUIDS, and since the CAS an object's file name is its
    // sha256 — not its display name. Any writer that recovers the guid by
    // matching the file NAME (SceneWriter did, for particle textures and for
    // every material texture property) looks up "df4501e5….png", finds nothing,
    // and writes an empty guid: one save silently erased the fire's texture and
    // every mesh's maps from the Particles sample (2026-09-03).
    if (path.isEmpty()) return QString();
    const QFileInfo info(path);
    const QString objectsDir = QDir::cleanPath(
        QDir(root).absoluteFilePath(QStringLiteral("objects")));
    const QString dir = QDir::cleanPath(info.absolutePath());
    // <root>/objects/<xx>/<oid>.<ext> — anything else is not a store object.
    if (!dir.startsWith(objectsDir + QLatin1Char('/'))) return QString();
    const QString oid = info.completeBaseName().toLower();
    if (oid.length() != 64) return QString();

    // One object can back SEVERAL assets (content dedup is the whole point of
    // the store), so prefer the one THIS project pins; the plain row otherwise.
    QSqlQuery query(conn);
    query.prepare("SELECT AF.asset_guid FROM asset_files AF "
                  "LEFT JOIN project_assets PA ON PA.asset_guid = AF.asset_guid "
                  "                           AND PA.project_guid = ? "
                  "WHERE AF.oid = ? "
                  "ORDER BY (PA.asset_guid IS NOT NULL) DESC");
    query.addBindValue(projectGuid);
    query.addBindValue(oid);
    if (query.exec() && query.next()) return query.value(0).toString();
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
    // Atomic: store.json carries the storeId every relocated root is
    // identified by — truncating it in place is how a crash mid-write turns a
    // populated store into an unrecognized one.
    return FileWrite::writeFileAtomic(
        path, QJsonDocument(info).toJson(QJsonDocument::Indented), errorOut);
}

} // namespace AssetCas
