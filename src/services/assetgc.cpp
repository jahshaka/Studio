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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QVariantList>

#include "services/assetmigration.h"
#include "services/assetstorepaths.h"
#include "services/jahlog.h"
#include "data/database/database.h"   // DbTransaction
#include "data/database/dbqt.h"
#include "data/db/statement.h"

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
         + straySidecars.items.size() + deadPins.items.size();
}

qint64 Report::totalBytes() const
{
    return unreferencedObjects.bytes + strayObjects.bytes + straySidecars.bytes
         + deadPins.bytes;
}

int Report::removedCount() const
{
    return unreferencedObjects.removed + strayObjects.removed + straySidecars.removed
         + deadPins.removed;
}

qint64 Report::removedBytes() const
{
    return unreferencedObjects.removedBytes + strayObjects.removedBytes
         + straySidecars.removedBytes + deadPins.removedBytes;
}

QVariantMap Report::toMap() const
{
    QVariantMap classes;
    classes["unreferencedObjects"] = unreferencedObjects.toMap();
    classes["strayObjects"] = strayObjects.toMap();
    classes["straySidecars"] = straySidecars.toMap();
    classes["deadPins"] = deadPins.toMap();
    classes["unreadableSidecars"] = unreadableSidecars.toMap();   // kept: not in the counts below
    classes["unplacedSidecars"] = unplacedSidecars.toMap();       // kept: not in the counts below

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
// iris::files::stagingTempPath's shape: "<final>.tmp-<pid>-<serial>".
bool isStagingTemp(const QString &fileName)
{
    return fileName.contains(QLatin1String(".tmp-"));
}

/// `readOk` (when given) is CLEARED by a probe whose read failed: such a probe answers false, never "absent".
bool tableExists(db::Connection &conn, const char *name, bool *readOk = nullptr)
{
    db::Statement q(conn, "SELECT 1 FROM sqlite_master WHERE type IN ('table','view') AND name = ?");
    q.bindText(1, name);
    db::Error e;
    if (q.step(&e)) return true;
    if (e && readOk) *readOk = false;
    return false;
}

int scalar(db::Connection &conn, const char *sql)
{
    db::Statement q(conn, sql);
    db::Error e;
    if (!q.step(&e)) return -1;
    return int(q.int64(0));
}

/// A READ WHOSE EMPTINESS DECIDES A DELETE: `readOk` is CLEARED when it failed (an empty set from a failed read
/// would make every object look unreferenced - the one input under which the sweep deletes a whole library).
QSet<QString> stringSet(db::Connection &conn, const char *sql, bool *readOk)
{
    QSet<QString> out;
    db::Statement q(conn, sql);
    db::Error e;
    while (q.step(&e)) {
        const QString value = dbqt::text(q, 0);
        if (!value.isEmpty()) out.insert(value);
    }
    if (e) *readOk = false;
    return out;
}

void add(ClassReport &report, const QString &id, const QString &path,
         qint64 bytes, const QString &reason)
{
    report.items.append(Item{ id, path, bytes, reason });
    report.bytes += bytes;
}

} // namespace

Report collect(db::Connection &conn, const QString &root, bool force)
{
    Report report;
    report.root = root;
    report.dryRun = true;
    QElapsedTimer timer;
    timer.start();

    if (!conn.isOpen()) {
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
    // A FAILED READ HERE REFUSES THE SWEEP (NOQT-DATABASE-1 stage 2): an empty set it returned would make every
    // object, pin and sidecar look unreferenced.
    bool readsOk = true;
    const QSet<QString> mappedOids = stringSet(conn, "SELECT DISTINCT oid FROM asset_files", &readsOk);
    const QSet<QString> pinnedOids =
        tableExists(conn, "project_assets", &readsOk)   // a FAILED probe refuses the sweep (the audit's finding 5)
            ? stringSet(conn, "SELECT DISTINCT oid_pin FROM project_assets WHERE oid_pin <> ''", &readsOk)
            : QSet<QString>();
    const QSet<QString> assetGuids = stringSet(conn, "SELECT guid FROM assets", &readsOk);
    const QSet<QString> catalogued = stringSet(conn, "SELECT oid FROM files", &readsOk);
    if (!readsOk) {
        report.error = QStringLiteral("refusing to sweep %1: a read of the catalog failed (the log names it)").arg(root);
        return report;
    }

    // --- (a) catalogued objects nothing references -------------------------
    {
        db::Statement q(conn, "SELECT oid, ext, size, refcount FROM files");
        db::Error e;
        while (q.step(&e)) {
            const QString oid = dbqt::text(q, 0);
            const QString ext = dbqt::text(q, 1);
            const qint64  size = q.int64(2);
            const int     refcount = int(q.int64(3));

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
        if (e) {   // a class read short is never swept: the rest of it would look like garbage next time
            report.error = QStringLiteral("refusing to sweep %1: the files read failed: %2").arg(root, dbqt::describe(e));
            return report;
        }
    }

    // --- (b) objects on disk the catalog never recorded --------------------
    if (objectsDir.exists()) {
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
    QSet<QString> listedByUnplaced;   // the oids the kept, unplaced sidecars list (below)
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
                // AN UNREADABLE SIDECAR IS NEVER "NO ASSET" (the lead's ruling (A)): one that cannot be read is
                // KEPT and named - it may be a kept asset a rebuild could not read, and its objects are inside it.
                QFile file(info.absoluteFilePath());
                QJsonParseError parse;
                const QJsonDocument doc = file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll(), &parse)
                                                                         : QJsonDocument();
                const QString why = !file.isOpen() ? QStringLiteral("it could not be read (%1)").arg(file.errorString())
                                  : parse.error != QJsonParseError::NoError
                                      ? QStringLiteral("it is not JSON (%1)").arg(parse.errorString())
                                  : !doc.isObject() || doc.object().value(QStringLiteral("guid")).toString().isEmpty()
                                      ? QStringLiteral("it is not a sidecar (no guid)")
                                      : QString();
                if (!why.isEmpty()) {
                    add(report.unreadableSidecars, guid, info.absoluteFilePath(), info.size(),
                        why + QStringLiteral(": kept, with every unreferenced object - its own cannot be told apart"));
                    continue;
                }
                // ...AND ONE THAT PARSES BUT THAT A REBUILD CANNOT PLACE (the confirm's F6) is a kept asset the
                // rebuild skipped: KEPT, named with the rebuild's own reason, and every object it lists kept too.
                const QString unplaced = AssetMigration::unplacedReason(doc.object());
                if (!unplaced.isEmpty()) {
                    add(report.unplacedSidecars, guid, info.absoluteFilePath(), info.size(),
                        QStringLiteral("a rebuild cannot place it (%1): kept, with the objects it lists").arg(unplaced));
                    for (const char *list : { "files", "pins" })
                        for (const QJsonValue &entry : doc.object().value(QLatin1String(list)).toArray())
                            listedByUnplaced.insert(entry.toObject().value(QStringLiteral("oid")).toString().toLower());
                    continue;
                }
                add(report.straySidecars, guid, info.absoluteFilePath(), info.size(),
                    QStringLiteral("no assets row carries this guid"));
            }
        }
    }
    // ...AND WHILE ONE IS KEPT, SO IS EVERY OBJECT the two object classes listed (their owner may be it).
    if (!report.unreadableSidecars.items.isEmpty()) {
        report.unreferencedObjects = ClassReport();
        report.strayObjects = ClassReport();
    }
    // An unplaced sidecar keeps exactly the objects it lists.
    for (ClassReport *objects : { &report.unreferencedObjects, &report.strayObjects }) {
        ClassReport left;
        for (const Item &item : std::as_const(objects->items))
            if (!listedByUnplaced.contains(item.id)) add(left, item.id, item.path, item.bytes, item.reason);
        *objects = left;
    }

    // --- (f) pins naming a project that no longer exists -------------------
    //
    // Rows, not files. A dead pin is a reference the catalog still honours:
    // it keeps its object out of (a) above, it used to VETO a plain library
    // delete (countAssetPins now ignores it), and nothing in the app can ever
    // reach it again — the project it belongs to is gone. The one input that
    // could make a LIVE pin look dead is a catalog whose projects table is
    // missing, so that case reports nothing at all.
    if (tableExists(conn, "project_assets") && tableExists(conn, "projects")) {
        db::Statement q(conn, "SELECT PA.project_guid, PA.asset_guid FROM project_assets PA "
                              "LEFT JOIN projects P ON P.guid = PA.project_guid "
                              "WHERE P.guid IS NULL");
        db::Error e;
        while (q.step(&e)) {
            const QString project = dbqt::text(q, 0);
            const QString asset = dbqt::text(q, 1);
            add(report.deadPins, project + QLatin1Char('/') + asset, QString(), 0,
                QStringLiteral("project '%1' no longer exists").arg(project));
        }
    }

    report.ok = true;
    report.elapsedMs = timer.elapsed();
    return report;
}

Report sweep(db::Connection &conn, const QString &root, bool dryRun, bool force)
{
    Report report = collect(conn, root, force);
    report.dryRun = dryRun;
    // ONE WARNING A SWEEP naming what it keeps unread (the lead's ruling (A): a fixable error, not a toast).
    if (report.ok && !dryRun && !report.unreadableSidecars.items.isEmpty()) {
        QStringList files;
        for (const Item &item : report.unreadableSidecars.items) files << item.path;
        JahLog::write(JahLog::legacy, JahLog::Level::Warning,
            QStringLiteral("assets.gc: %1 sidecar(s) could not be read and are KEPT, with every unreferenced object of "
                           "the store (their own cannot be told apart) - fix or remove: %2")
                .arg(files.size()).arg(files.join(QStringLiteral(", "))));
    }
    if (report.ok && !dryRun && !report.unplacedSidecars.items.isEmpty()) {
        QStringList named;
        for (const Item &item : report.unplacedSidecars.items) named << item.path + QStringLiteral(" (") + item.reason + QLatin1Char(')');
        JahLog::write(JahLog::legacy, JahLog::Level::Warning,
            QStringLiteral("assets.gc: %1 sidecar(s) a rebuild cannot place are KEPT, with the objects they list: %2")
                .arg(named.size()).arg(named.join(QStringLiteral(", "))));
    }
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
        // A GUARD, NOT A RAW BEGIN (CLOSE-2 round 2, H1). A raw
        // conn.transaction() cannot start one while the editor's gesture batch
        // holds this connection — `inTransaction` reads false, these deletes
        // ride the batch's commit, and a partial failure has nothing to roll
        // back: the dropped `files` rows would be committed while their
        // objects stayed on disk. A DbTransaction makes the batch stand down
        // and hands this sweep a real transaction of its own (database.h).
        DbTransaction tx(conn);
        const bool inTransaction = tx.isActive();
        bool rowsOk = true;
        for (const Item &item : report.unreferencedObjects.items) {
            db::Statement del(conn, "DELETE FROM files WHERE oid = ?");
            dbqt::bindText(del, 1, item.id);
            db::Error e;
            if (!del.run(&e)) {
                rowsOk = false;
                report.failures << QStringLiteral("could not drop the files row for %1: %2")
                                       .arg(item.id, dbqt::describe(e));
            }
        }
        if (inTransaction) {
            if (rowsOk) rowsOk = tx.commit();
            else tx.rollback();
        }
        if (rowsOk) unlinkFiles(report.unreferencedObjects);
        else report.failures << QStringLiteral(
            "the files rows could not be dropped — their objects were left in place");
    }

    // Dead pins go as ROWS, in their own transaction: nothing on disk moves,
    // and a failure here must not stop the file classes below from being
    // reclaimed. The `id` is "<projectGuid>/<assetGuid>" — split once.
    if (!report.deadPins.items.isEmpty()) {
        DbTransaction tx(conn);   // as above (H1)
        const bool inTransaction = tx.isActive();
        bool rowsOk = true;
        for (const Item &item : report.deadPins.items) {
            const int slash = item.id.indexOf(QLatin1Char('/'));
            if (slash <= 0) { rowsOk = false; continue; }
            db::Statement del(conn, "DELETE FROM project_assets WHERE project_guid = ? AND asset_guid = ?");
            dbqt::bindText(del, 1, item.id.left(slash));
            dbqt::bindText(del, 2, item.id.mid(slash + 1));
            db::Error e;
            if (!del.run(&e)) {
                rowsOk = false;
                report.failures << QStringLiteral("could not drop the dead pin %1: %2")
                                       .arg(item.id, dbqt::describe(e));
            }
        }
        if (inTransaction) {
            if (rowsOk) rowsOk = tx.commit();
            else tx.rollback();
        }
        if (rowsOk) {
            report.deadPins.removed = report.deadPins.items.size();
        } else {
            report.failures << QStringLiteral(
                "the dead project pins could not be dropped — they were left in place");
        }
    }

    unlinkFiles(report.strayObjects);
    unlinkFiles(report.straySidecars);

    report.ok = report.failures.isEmpty();
    report.elapsedMs += timer.elapsed();
    return report;
}

} // namespace AssetGc
