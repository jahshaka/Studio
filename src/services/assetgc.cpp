/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "services/assetgc.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariantList>

#include "services/assetstorepaths.h"

namespace AssetGc
{

QVariantMap Item::toMap() const
{
    QVariantMap map;
    map["id"] = id;
    map["path"] = path;
    map["bytes"] = bytes;
    map["reason"] = reason;
    return map;
}

QVariantMap ClassReport::toMap() const
{
    QVariantList list;
    for (const Item &item : items) list.append(item.toMap());
    QVariantMap map;
    map["count"] = items.size();
    map["bytes"] = bytes;
    map["items"] = list;
    map["removed"] = removed;
    map["removedBytes"] = removedBytes;
    return map;
}

int Report::totalCount() const
{
    return unreferencedObjects.items.size() + strayObjects.items.size()
         + straySidecars.items.size() + legacyFolders.items.size()
         + redundantLegacyFiles.items.size();
}

qint64 Report::totalBytes() const
{
    return unreferencedObjects.bytes + strayObjects.bytes + straySidecars.bytes
         + legacyFolders.bytes + redundantLegacyFiles.bytes;
}

int Report::removedCount() const
{
    return unreferencedObjects.removed + strayObjects.removed + straySidecars.removed
         + legacyFolders.removed + redundantLegacyFiles.removed;
}

qint64 Report::removedBytes() const
{
    return unreferencedObjects.removedBytes + strayObjects.removedBytes
         + straySidecars.removedBytes + legacyFolders.removedBytes
         + redundantLegacyFiles.removedBytes;
}

QVariantMap Report::toMap() const
{
    QVariantMap classes;
    classes["unreferencedObjects"] = unreferencedObjects.toMap();
    classes["strayObjects"] = strayObjects.toMap();
    classes["straySidecars"] = straySidecars.toMap();
    classes["legacyFolders"] = legacyFolders.toMap();
    classes["redundantLegacyFiles"] = redundantLegacyFiles.toMap();

    QVariantMap map;
    map["ok"] = ok;
    map["dryRun"] = dryRun;
    if (!error.isEmpty()) map["error"] = error;
    map["root"] = root;
    map["classes"] = classes;
    map["count"] = totalCount();
    map["bytes"] = totalBytes();
    map["removed"] = removedCount();
    map["removedBytes"] = removedBytes();
    map["refcountDrift"] = refcountDrift;
    map["failures"] = failures;
    map["elapsedMs"] = elapsedMs;
    return map;
}

namespace
{
// Directory names the store owns itself: never candidates for the legacy
// per-guid sweep, whatever the catalog says.
bool isReservedStoreDir(const QString &name)
{
    return name == QLatin1String("objects")
        || name == QLatin1String("sidecar")
        || name == QLatin1String("derived")
        || name.startsWith(QLatin1Char('.'));
}

// FileWrite::stagingTempPath's shape: "<final>.tmp-<pid>-<serial>".
bool isStagingTemp(const QString &fileName)
{
    return fileName.contains(QLatin1String(".tmp-"));
}

bool tableExists(QSqlDatabase conn, const QString &name)
{
    QSqlQuery q(conn);
    q.prepare("SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name = ?");
    q.addBindValue(name);
    return q.exec() && q.next();
}

int scalar(QSqlDatabase conn, const QString &sql)
{
    QSqlQuery q(conn);
    if (!q.exec(sql) || !q.next()) return -1;
    return q.value(0).toInt();
}

QSet<QString> stringSet(QSqlDatabase conn, const QString &sql)
{
    QSet<QString> out;
    QSqlQuery q(conn);
    if (!q.exec(sql)) return out;
    while (q.next()) {
        const QString value = q.value(0).toString();
        if (!value.isEmpty()) out.insert(value);
    }
    return out;
}

void add(ClassReport &report, const QString &id, const QString &path,
         qint64 bytes, const QString &reason)
{
    report.items.append(Item{ id, path, bytes, reason });
    report.bytes += bytes;
}

qint64 directoryBytes(const QString &path)
{
    qint64 total = 0;
    QDirIterator it(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) { it.next(); total += it.fileInfo().size(); }
    return total;
}
} // namespace

Report collect(QSqlDatabase conn, const QString &root, bool force)
{
    Report report;
    report.root = root;
    report.dryRun = true;
    QElapsedTimer timer;
    timer.start();

    if (!conn.isValid() || !conn.isOpen()) {
        report.error = QStringLiteral("the catalog connection is not open");
        return report;
    }
    if (root.isEmpty() || !QDir(root).exists()) {
        report.error = QStringLiteral("the asset store is offline: %1").arg(root);
        return report;
    }
    if (!tableExists(conn, "files") || !tableExists(conn, "asset_files")
        || !tableExists(conn, "assets")) {
        report.error = QStringLiteral("this database has no content catalog (files/asset_files/assets)");
        return report;
    }

    // --- Property (2): refuse a store this catalog does not recognize ------
    //
    // A fresh or wrong database opened over a populated store makes EVERY
    // artifact look unreferenced. That is the one input under which this code
    // would delete a whole library, so it is the one input it refuses.
    const int knownObjects = scalar(conn, "SELECT COUNT(*) FROM files");
    const int knownAssets  = scalar(conn, "SELECT COUNT(*) FROM assets");
    const QDir objectsDir(QDir(root).filePath(QStringLiteral("objects")));
    bool storeHasObjects = false;
    if (objectsDir.exists()) {
        QDirIterator probe(objectsDir.path(), QDir::Files, QDirIterator::Subdirectories);
        storeHasObjects = probe.hasNext();
    }
    if (!force && knownObjects <= 0 && knownAssets <= 0 && storeHasObjects) {
        report.error = QStringLiteral(
            "refusing to sweep %1: this catalog knows no assets and no objects, but the store "
            "holds content — the database and the store do not belong together (pass force to "
            "override)").arg(root);
        return report;
    }

    // --- Reachability, read from the rows (property 1) ---------------------
    const QSet<QString> mappedOids = stringSet(conn, "SELECT DISTINCT oid FROM asset_files");
    const QSet<QString> pinnedOids =
        tableExists(conn, "project_assets")
            ? stringSet(conn, "SELECT DISTINCT oid_pin FROM project_assets WHERE oid_pin <> ''")
            : QSet<QString>();
    const QSet<QString> assetGuids = stringSet(conn, "SELECT guid FROM assets");

    // --- (a) catalogued objects nothing references -------------------------
    {
        QSqlQuery q(conn);
        q.exec("SELECT oid, ext, size, refcount FROM files");
        while (q.next()) {
            const QString oid = q.value(0).toString();
            const QString ext = q.value(1).toString();
            const qint64  size = q.value(2).toLongLong();
            const int     refcount = q.value(3).toInt();

            const bool mapped = mappedOids.contains(oid);
            const bool pinned = pinnedOids.contains(oid);
            // Informational: the counter disagreeing with the rows is exactly
            // why the rows are the authority here.
            if ((refcount > 0) != mapped) ++report.refcountDrift;
            if (mapped || pinned) continue;

            const QString path = AssetStorePaths::objectPathIn(root, oid, ext);
            const QFileInfo info(path);
            add(report.unreferencedObjects, oid, path,
                info.exists() ? info.size() : size,
                QStringLiteral("no asset_files row and no project pin names this content"));
        }
    }

    // --- (b) objects on disk the catalog never recorded --------------------
    if (objectsDir.exists()) {
        const QSet<QString> catalogued = stringSet(conn, "SELECT oid FROM files");
        const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-kStaleTempSeconds);
        QDirIterator it(objectsDir.path(), QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QFileInfo info = it.fileInfo();
            const QString name = info.fileName();

            if (isStagingTemp(name)) {
                // A staging temp may belong to an import running RIGHT NOW —
                // only an old one is provably abandoned.
                if (info.lastModified() > cutoff) continue;
                add(report.strayObjects, name, info.absoluteFilePath(), info.size(),
                    QStringLiteral("abandoned staging temp (older than one hour)"));
                continue;
            }
            // The object's name IS its oid; the extension is display-only, so
            // an object whose oid is catalogued is live under ANY extension.
            const QString oid = info.completeBaseName().toLower();
            if (catalogued.contains(oid)) continue;
            add(report.strayObjects, oid.isEmpty() ? name : oid, info.absoluteFilePath(),
                info.size(), QStringLiteral("no files row records this object"));
        }
    }

    // --- (c) sidecars naming no asset row ----------------------------------
    {
        const QDir sidecarDir(QDir(root).filePath(QStringLiteral("sidecar")));
        if (sidecarDir.exists()) {
            const QDateTime cutoff = QDateTime::currentDateTime().addSecs(-kStaleTempSeconds);
            QDirIterator it(sidecarDir.path(), QDir::Files | QDir::Hidden);
            while (it.hasNext()) {
                it.next();
                const QFileInfo info = it.fileInfo();
                if (isStagingTemp(info.fileName())) {
                    if (info.lastModified() > cutoff) continue;
                    add(report.straySidecars, info.fileName(), info.absoluteFilePath(),
                        info.size(), QStringLiteral("abandoned staging temp (older than one hour)"));
                    continue;
                }
                if (info.suffix().toLower() != QLatin1String("json")) continue;
                const QString guid = info.completeBaseName();
                if (assetGuids.contains(guid)) continue;
                add(report.straySidecars, guid, info.absoluteFilePath(), info.size(),
                    QStringLiteral("no assets row carries this guid"));
            }
        }
    }

    // --- (d)/(e) the legacy per-guid view ----------------------------------
    //
    // (d) a folder naming no asset row is garbage outright; (e) a folder that
    // DOES name a live asset keeps only the entries the CAS cannot serve —
    // an entry whose bytes are proven present as an object (same size, object
    // on disk) is the second copy Windows pays full price for.
    {
        QDirIterator dirs(root, QDir::Dirs | QDir::NoDotAndDotDot);
        while (dirs.hasNext()) {
            dirs.next();
            const QFileInfo dirInfo = dirs.fileInfo();
            const QString name = dirInfo.fileName();
            if (isReservedStoreDir(name)) continue;

            if (!assetGuids.contains(name)) {
                add(report.legacyFolders, name, dirInfo.absoluteFilePath(),
                    directoryBytes(dirInfo.absoluteFilePath()),
                    QStringLiteral("no assets row carries this guid"));
                continue;
            }

            // Live asset: reclaim only entries the CAS demonstrably holds.
            QSqlQuery q(conn);
            q.prepare("SELECT AF.name, AF.oid, F.ext FROM asset_files AF "
                      "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.asset_guid = ?");
            q.addBindValue(name);
            if (!q.exec()) continue;
            QHash<QString, QString> objectForName;   // display name -> object path
            while (q.next()) {
                objectForName.insert(q.value(0).toString(),
                                     AssetStorePaths::objectPathIn(root, q.value(1).toString(),
                                                                   q.value(2).toString()));
            }

            QDirIterator files(dirInfo.absoluteFilePath(), QDir::Files | QDir::Hidden);
            while (files.hasNext()) {
                files.next();
                const QFileInfo info = files.fileInfo();
                const QString objectPath = objectForName.value(info.fileName());
                if (objectPath.isEmpty()) continue;              // not in the CAS — keep
                const QFileInfo object(objectPath);
                if (!object.exists() || object.size() != info.size()) continue;   // unproven — keep
                add(report.redundantLegacyFiles, name + QLatin1Char('/') + info.fileName(),
                    info.absoluteFilePath(), info.size(),
                    QStringLiteral("byte-for-byte present in objects/ (retired legacy view)"));
            }
        }
    }

    report.ok = true;
    report.elapsedMs = timer.elapsed();
    return report;
}

Report sweep(QSqlDatabase conn, const QString &root, bool dryRun, bool force)
{
    Report report = collect(conn, root, force);
    report.dryRun = dryRun;
    if (!report.ok || dryRun) return report;

    QElapsedTimer timer;
    timer.start();

    auto unlinkFiles = [&report](ClassReport &cls) {
        for (const Item &item : cls.items) {
            if (QFile::remove(item.path)) { ++cls.removed; cls.removedBytes += item.bytes; }
            else if (QFileInfo::exists(item.path))
                report.failures << QStringLiteral("could not remove %1").arg(item.path);
            else { ++cls.removed; cls.removedBytes += item.bytes; }   // already gone
        }
    };

    // Objects go with their rows, and the rows go FIRST: an object file that
    // survives a failed row delete is garbage the next sweep re-finds, while a
    // row that survives a successful unlink is a catalog pointing at nothing.
    if (!report.unreferencedObjects.items.isEmpty()) {
        const bool inTransaction = conn.transaction();
        bool rowsOk = true;
        for (const Item &item : report.unreferencedObjects.items) {
            QSqlQuery del(conn);
            del.prepare("DELETE FROM files WHERE oid = ?");
            del.addBindValue(item.id);
            if (!del.exec()) {
                rowsOk = false;
                report.failures << QStringLiteral("could not drop the files row for %1: %2")
                                       .arg(item.id, del.lastError().text());
            }
        }
        if (inTransaction) {
            if (rowsOk) rowsOk = conn.commit();
            else conn.rollback();
        }
        if (rowsOk) unlinkFiles(report.unreferencedObjects);
        else report.failures << QStringLiteral(
            "the files rows could not be dropped — their objects were left in place");
    }

    unlinkFiles(report.strayObjects);
    unlinkFiles(report.straySidecars);
    unlinkFiles(report.redundantLegacyFiles);

    for (const Item &item : report.legacyFolders.items) {
        QDir folder(item.path);
        if (folder.removeRecursively()) {
            ++report.legacyFolders.removed;
            report.legacyFolders.removedBytes += item.bytes;
        } else {
            report.failures << QStringLiteral("could not remove %1").arg(item.path);
        }
    }
    // A per-guid folder emptied by the redundant-file sweep is the retired
    // view finishing its own retirement.
    for (const Item &item : report.redundantLegacyFiles.items) {
        const QString folder = QFileInfo(item.path).absolutePath();
        QDir dir(folder);
        if (dir.exists() && dir.isEmpty()) dir.removeRecursively();
    }

    report.ok = report.failures.isEmpty();
    report.elapsedMs += timer.elapsed();
    return report;
}

} // namespace AssetGc
