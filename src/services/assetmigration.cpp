/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "services/assetmigration.h"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

#include "data/database/casschema.h"
#include "services/assetcas.h"
#include "services/assetstore.h"
#include "services/assetstorepaths.h"

namespace AssetMigration
{

QVariantMap MigrateReport::toMap() const
{
    QVariantMap map;
    map["ok"] = ok;
    if (!error.isEmpty()) map["error"] = error;
    map["libraryRows"] = libraryRows;
    map["rowsWithFiles"] = rowsWithFiles;
    map["rowsWithoutFiles"] = rowsWithoutFiles;
    map["filesSeen"] = filesSeen;
    map["objectsCreated"] = objectsCreated;
    map["objectsReused"] = objectsReused;
    map["bytesHashed"] = bytesHashed;
    map["sidecars"] = sidecars;
    map["elapsedMs"] = elapsedMs;
    return map;
}

QVariantMap VerifyReport::toMap() const
{
    QVariantMap map;
    map["ok"] = ok;
    if (!error.isEmpty()) map["error"] = error;
    map["objects"] = objects;
    map["bytes"] = bytes;
    map["corrupt"] = corrupt;
    map["missing"] = missing;
    map["elapsedMs"] = elapsedMs;
    return map;
}

QVariantMap RebuildReport::toMap() const
{
    QVariantMap map;
    map["ok"] = ok;
    if (!error.isEmpty()) map["error"] = error;
    map["assets"] = assets;
    map["files"] = files;
    map["links"] = links;
    map["elapsedMs"] = elapsedMs;
    return map;
}

namespace
{
// Every tool opens its OWN named connection on the explicit db path —
// never the app's default connection (rehearsal isolation).
class ScopedConnection
{
public:
    explicit ScopedConnection(const QString &dbPath)
        : name(QStringLiteral("AssetMigration-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)))
    {
        db = QSqlDatabase::addDatabase("QSQLITE", name);
        db.setDatabaseName(dbPath);
        opened = db.open();
    }
    ~ScopedConnection()
    {
        if (opened) db.close();
        db = QSqlDatabase();
        QSqlDatabase::removeDatabase(name);
    }
    bool opened = false;
    QSqlDatabase db;

private:
    QString name;
};
} // namespace

MigrateReport migrateStore(const QString &dbPath, const QString &storeRoot)
{
    MigrateReport report;
    QElapsedTimer timer;
    timer.start();

    if (!QFileInfo::exists(dbPath)) {
        report.error = QStringLiteral("no database at %1").arg(dbPath);
        return report;
    }
    if (!QDir(storeRoot).exists()) {
        report.error = QStringLiteral("store root unreachable: %1").arg(storeRoot);
        return report;
    }
    // Refusal guard (preflight §6.2): another running instance = stop.
    if (LibraryLock::heldElsewhere(dbPath)) {
        report.error = QStringLiteral("the library is open in another Jahshaka instance — close Jahshaka first");
        return report;
    }

    ScopedConnection scoped(dbPath);
    if (!scoped.opened) {
        report.error = QStringLiteral("cannot open %1").arg(dbPath);
        return report;
    }
    QSqlDatabase conn = scoped.db;

    AssetCas::ensureCasSchema(conn);
    QString error;
    if (!AssetCas::writeStoreInfo(storeRoot, &error)) {
        report.error = error;
        return report;
    }

    // Library rows only: view_filter IN (2,3) — preflight §1.6.
    struct Row { QString guid, name; };
    QVector<Row> rows;
    {
        QSqlQuery query(conn);
        query.prepare("SELECT guid, name FROM assets WHERE view_filter IN (2, 3)");
        if (!query.exec()) {
            report.error = query.lastError().text();
            return report;
        }
        while (query.next()) rows.append({ query.value(0).toString(), query.value(1).toString() });
    }
    report.libraryRows = rows.size();

    conn.transaction();
    for (const Row &row : rows) {
        const bool hadFolder = QDir(AssetStorePaths::legacyFolderIn(storeRoot, row.guid)).exists();

        AssetCas::IngestStats stats;
        if (!AssetCas::ingestLegacyFolder(conn, storeRoot, row.guid, row.name, &stats, &error)) {
            conn.rollback();
            report.error = QStringLiteral("%1 (asset %2)").arg(error, row.guid);
            return report;
        }
        hadFolder ? ++report.rowsWithFiles : ++report.rowsWithoutFiles;
        report.filesSeen += stats.files;
        report.objectsCreated += stats.objectsCreated;
        report.objectsReused += stats.objectsReused;
        report.bytesHashed += stats.bytesHashed;

        if (!AssetCas::writeSidecar(conn, storeRoot, row.guid, &error)) {
            conn.rollback();
            report.error = QStringLiteral("%1 (sidecar %2)").arg(error, row.guid);
            return report;
        }
        ++report.sidecars;
    }
    if (!conn.commit()) {
        report.error = QStringLiteral("commit failed: %1").arg(conn.lastError().text());
        return report;
    }

    report.elapsedMs = timer.elapsed();
    report.ok = true;
    return report;
}

VerifyReport verify(const QString &dbPath, const QString &storeRoot)
{
    VerifyReport report;
    QElapsedTimer timer;
    timer.start();

    ScopedConnection scoped(dbPath);
    if (!scoped.opened) {
        report.error = QStringLiteral("cannot open %1").arg(dbPath);
        return report;
    }

    QSqlQuery query(scoped.db);
    if (!query.exec("SELECT oid, size, ext FROM files")) {
        report.error = QStringLiteral("no files table — migrate first (%1)").arg(query.lastError().text());
        return report;
    }
    while (query.next()) {
        const QString oid = query.value(0).toString();
        const qint64 size = query.value(1).toLongLong();
        const QString ext = query.value(2).toString();
        ++report.objects;

        const QString path = AssetStorePaths::objectPathIn(storeRoot, oid, ext);
        if (!QFileInfo::exists(path)) {
            report.missing << oid;
            continue;
        }
        report.bytes += size;
        if (QFileInfo(path).size() != size || AssetCas::hashFile(path) != oid) {
            report.corrupt << oid;
        }
    }

    report.elapsedMs = timer.elapsed();
    report.ok = report.corrupt.isEmpty() && report.missing.isEmpty();
    return report;
}

RebuildReport rebuildCatalog(const QString &dbPath, const QString &storeRoot)
{
    RebuildReport report;
    QElapsedTimer timer;
    timer.start();

    const QDir sidecarDir(QDir(storeRoot).filePath(QStringLiteral("sidecar")));
    if (!sidecarDir.exists()) {
        report.error = QStringLiteral("no sidecar directory under %1").arg(storeRoot);
        return report;
    }

    ScopedConnection scoped(dbPath);
    if (!scoped.opened) {
        report.error = QStringLiteral("cannot open %1").arg(dbPath);
        return report;
    }
    QSqlDatabase conn = scoped.db;

    // A fresh recovery DB needs the assets table too.
    QSqlQuery(CasSchema::kAssetsTableForRebuild, conn);
    AssetCas::ensureCasSchema(conn);

    conn.transaction();
    QDirIterator it(sidecarDir.path(), { "*.json" }, QDir::Files);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QJsonObject sidecar = QJsonDocument::fromJson(file.readAll()).object();
        const QString guid = sidecar.value("guid").toString();
        if (guid.isEmpty()) continue;

        QSqlQuery insertAsset(conn);
        insertAsset.prepare("INSERT OR IGNORE INTO assets (guid, name, type, view_filter, collection, author, license, properties, tags) "
                            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)");
        insertAsset.addBindValue(guid);
        insertAsset.addBindValue(sidecar.value("name").toString());
        insertAsset.addBindValue(sidecar.value("type").toInt());
        insertAsset.addBindValue(sidecar.value("viewFilter").toInt());
        insertAsset.addBindValue(sidecar.value("collection").toInt());
        insertAsset.addBindValue(sidecar.value("author").toString());
        insertAsset.addBindValue(sidecar.value("license").toString());
        insertAsset.addBindValue(sidecar.contains("properties")
            ? QJsonDocument(sidecar.value("properties").toObject()).toJson(QJsonDocument::Compact)
            : QByteArray());
        insertAsset.addBindValue(sidecar.contains("tags")
            ? QJsonDocument(sidecar.value("tags").toObject()).toJson(QJsonDocument::Compact)
            : QByteArray());
        if (insertAsset.exec() && insertAsset.numRowsAffected() > 0) ++report.assets;

        for (const auto &value : sidecar.value("files").toArray()) {
            const QJsonObject fileObj = value.toObject();

            QSqlQuery insertFile(conn);
            insertFile.prepare("INSERT OR IGNORE INTO files (oid, size, ext, refcount) VALUES (?, ?, ?, 0)");
            insertFile.addBindValue(fileObj.value("oid").toString());
            insertFile.addBindValue(qint64(fileObj.value("size").toDouble()));
            insertFile.addBindValue(fileObj.value("ext").toString());
            if (insertFile.exec() && insertFile.numRowsAffected() > 0) ++report.files;

            QSqlQuery insertLink(conn);
            insertLink.prepare("INSERT OR IGNORE INTO asset_files (asset_guid, role, oid, name) VALUES (?, ?, ?, ?)");
            insertLink.addBindValue(guid);
            insertLink.addBindValue(fileObj.value("role").toString());
            insertLink.addBindValue(fileObj.value("oid").toString());
            insertLink.addBindValue(fileObj.value("name").toString());
            if (insertLink.exec() && insertLink.numRowsAffected() > 0) ++report.links;
        }
    }
    if (!conn.commit()) {
        report.error = QStringLiteral("commit failed: %1").arg(conn.lastError().text());
        return report;
    }

    report.elapsedMs = timer.elapsed();
    report.ok = true;
    return report;
}

} // namespace AssetMigration
