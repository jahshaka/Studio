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
#include "data/database/dbqt.h"
#include "data/db/connection.h"
#include "data/db/statement.h"
#include "data/db/transaction.h"

#include "data/database/casschema.h"
#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assetstore.h"
#include "services/assetstorepaths.h"
#include "irisgl/core/guid.h"

namespace AssetMigration
{

QVariantMap VerifyReport::toMap() const
{
    QVariantMap map;
    map["ok"] = ok;
    if (!error.isEmpty()) map["error"] = error;
    map["objects"] = objects;
    map["bytes"] = bytes;
    map["corrupt"] = corrupt;
    map["missing"] = missing;
    map["missingSidecars"] = missingSidecars;
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
    map["pins"] = pins;
    map["edges"] = edges;
    map["skipped"] = skipped;
    map["otherHomes"] = otherHomes;
    map["unlistedDropped"] = unlistedDropped;
    map["unreadable"] = unreadable;
    map["unreadableFiles"] = unreadableFiles;
    map["failedRows"] = failedRows;
    map["failedGuids"] = failedGuids;
    map["elapsedMs"] = elapsedMs;
    return map;
}

namespace
{
// Every tool opens its OWN connection on the explicit db path, OPENED BY THE LAYER (NOQT-DATABASE-1 stage 2:
// its ScopedConnection copy deleted) - never the app's default connection (rehearsal isolation).
db::Connection openOwn(const QString &dbPath, QString *errorOut)
{
    db::Options options;
    options.busyMs = db::kEditorBusyMs;
    options.name = "AssetMigration";
    db::Error error;
    db::Connection conn = db::Connection::open(dbPath.toStdString(), options, &error);
    if (!conn.isOpen()) *errorOut = QStringLiteral("cannot open %1 (%2)").arg(dbPath, dbqt::describe(error));
    return conn;
}
} // namespace

VerifyReport verify(const QString &dbPath, const QString &storeRoot)
{
    VerifyReport report;
    QElapsedTimer timer;
    timer.start();

    db::Connection conn = openOwn(dbPath, &report.error);
    if (!conn.isOpen()) return report;

    db::Statement query(conn, "SELECT oid, size, ext FROM files");
    db::Error e;
    while (query.step(&e)) {
        const QString oid = dbqt::text(query, 0);
        const qint64 size = query.int64(1);
        const QString ext = dbqt::text(query, 2);
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
    if (e) {
        report.error = QStringLiteral("no files table — migrate first (%1)").arg(dbqt::describe(e));
        return report;
    }

    // EVERY STORAGE ROW HAS ITS SIDECAR (ASSETS-HOME-1): a row of Assets, the
    // Materials or the Avatar storage without one is a row a Clear Database or
    // a format bump cannot rebuild — it would be lost, so it is a finding.
    db::Statement rows(conn, "SELECT guid FROM assets WHERE view_filter IN (?, ?, ?)");
    rows.bindInt(1, static_cast<int>(assethome::StoredAssets));
    rows.bindInt(2, static_cast<int>(assethome::StoredMaterialsLibrary));
    rows.bindInt(3, static_cast<int>(assethome::StoredAvatarLibrary));
    while (rows.step(&e)) {
        const QString guid = dbqt::text(rows, 0);
        if (!QFileInfo::exists(AssetStorePaths::sidecarPathIn(storeRoot, guid)))
            report.missingSidecars << guid;
    }
    if (e) {   // the sidecar finding from a failed read is no finding: said
        report.error = QStringLiteral("the storage rows could not be read: %1").arg(dbqt::describe(e));
        return report;
    }

    report.elapsedMs = timer.elapsed();
    // `ok` stays the BYTES' verdict (every object present and bit-identical);
    // a missing sidecar is its own finding, reported beside it.
    report.ok = report.corrupt.isEmpty() && report.missing.isEmpty();
    return report;
}

namespace
{
/// The sidecar's home, when it records one in this format; false otherwise.
bool sidecarHome(const QJsonObject &sidecar, assethome::Home *out)
{
    if (sidecar.value(QStringLiteral("formatVersion")).toInt() < 3) return false;
    assethome::Kind kind;
    if (!assethome::kindFromName(sidecar.value(QStringLiteral("home")).toString(), &kind))
        return false;
    out->kind = kind;
    out->projectGuid = kind == assethome::Kind::Project
                           ? sidecar.value(QStringLiteral("projectGuid")).toString()
                           : QString();
    return !(kind == assethome::Kind::Project && out->projectGuid.isEmpty());
}

bool wanted(const assethome::Home &home, const RebuildOptions &options)
{
    return options.homes.isEmpty() || options.homes.contains(home.kind);
}

bool isDerivedRole(const QString &role) { return role == QLatin1String("bake"); }
}   // namespace

QString unplacedReason(const QJsonObject &sidecar)
{
    if (sidecar.value("guid").toString().isEmpty()) return QStringLiteral("no guid");
    assethome::Home home;
    if (!sidecarHome(sidecar, &home))
        return QStringLiteral("no home in this format (formatVersion %1)").arg(sidecar.value("formatVersion").toInt());
    // THE ONE DOOR'S RULE, HERE TOO (ASSETS-HOME-1): a rebuild is not a second way into a home. A sidecar whose
    // (home, origin) the door would refuse is not restored.
    assethome::Origin origin;
    if (!assethome::originFromName(sidecar.value("origin").toString(), &origin))
        return QStringLiteral("no origin in this format");
    return assethome::refusal(home, origin);
}

namespace
{

/// Every sidecar a rebuild with `options` restores — parsed, tombstones and
/// unreadable ones out, with the counts.
QVector<QJsonObject> restorable(const QString &storeRoot, const RebuildOptions &options,
                                RebuildReport *report)
{
    QVector<QJsonObject> out;
    QSet<QString> dropped;
    const QDir sidecarDir(QDir(storeRoot).filePath(QStringLiteral("sidecar")));
    QDirIterator it(sidecarDir.path(), { "*.json" }, QDir::Files);
    while (it.hasNext()) {
        const QString path = it.next();
        // NOTHING IS SKIPPED SILENTLY: a sidecar that cannot be opened, parsed,
        // or placed in a home is COUNTED and NAMED (the log and the report), so
        // a rebuild that keeps less than the store holds says so.
        const auto unreadable = [&](const QString &why) {
            qWarning("rebuildCatalog: sidecar %s not read: %s", qUtf8Printable(path),
                     qUtf8Printable(why));
            if (report) {
                ++report->unreadable;
                report->unreadableFiles.append(path);
            }
        };
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) { unreadable(QStringLiteral("cannot open it")); continue; }
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
            unreadable(QStringLiteral("not JSON (%1)").arg(parseError.errorString()));
            continue;
        }
        const QJsonObject sidecar = doc.object();
        // ONE ANSWER TO "CAN THE REBUILD PLACE IT" (unplacedReason): the sweep reads the same one.
        const QString unplaced = unplacedReason(sidecar);
        if (!unplaced.isEmpty()) { unreadable(unplaced); continue; }
        assethome::Home home;
        sidecarHome(sidecar, &home);
        if (!wanted(home, options)) { if (report) ++report->otherHomes; continue; }
        // AN UNLISTED ROW lives only for the projects that pin it (a library
        // delete those projects vetoed). A rebuild that restores no project has
        // no pin to keep it for: it is dropped, and the collector takes its
        // objects — never an invisible row holding bytes for ever.
        const bool restoresProjects =
            options.homes.isEmpty() || options.homes.contains(assethome::Kind::Project);
        if (!sidecar.value("listed").toBool(true) && !restoresProjects) {
            if (report) ++report->unlistedDropped;
            dropped.insert(sidecar.value("guid").toString());
            continue;
        }

        // TOMBSTONE GUARD (deep audit 2026-09, area 6): a sidecar whose
        // recorded objects are ALL gone from the store describes an asset the
        // library no longer has. "All gone" is deliberately not "none
        // recorded": a file-less row (a DB-only asset) records an EMPTY
        // manifest and is perfectly recoverable. A pin counts as a recorded
        // object (a COW-edited asset's live bytes are pin-only, item 1c').
        const QJsonArray recorded = sidecar.value("files").toArray();
        const QJsonArray recordedPins = sidecar.value("pins").toArray();
        bool anyPresent = recorded.isEmpty() && recordedPins.isEmpty();
        for (const QJsonArray &list : { recorded, recordedPins }) {
            for (const auto &value : list) {
                const QJsonObject obj = value.toObject();
                if (QFileInfo::exists(AssetStorePaths::objectPathIn(
                        storeRoot, obj.value("oid").toString(), obj.value("ext").toString()))) {
                    anyPresent = true;
                    break;
                }
            }
            if (anyPresent) break;
        }
        if (!anyPresent) { if (report) ++report->skipped; continue; }
        out.append(sidecar);
    }
    // A MEMBER GOES WITH ITS OWNER: a row whose parent was dropped above (a
    // model's mesh, a material's baked map) would otherwise come back as a
    // loose tile of its own. Repeated until nothing more falls.
    for (bool more = !dropped.isEmpty(); more;) {
        more = false;
        for (int i = out.size() - 1; i >= 0; --i) {
            const QString parent = out.at(i).value("parent").toString();
            if (parent.isEmpty() || !dropped.contains(parent)) continue;
            dropped.insert(out.at(i).value("guid").toString());
            if (report) ++report->unlistedDropped;
            out.removeAt(i);
            more = true;
        }
    }
    return out;
}
}   // namespace

QSet<QString> keptObjects(const QString &storeRoot, const RebuildOptions &options)
{
    QSet<QString> oids;
    for (const QJsonObject &sidecar : restorable(storeRoot, options, nullptr)) {
        for (const auto &value : sidecar.value("files").toArray()) {
            const QJsonObject obj = value.toObject();
            if (options.dropDerived && isDerivedRole(obj.value("role").toString())) continue;
            oids.insert(obj.value("oid").toString());
        }
        // A project's pin survives only with its project: a storage-only
        // rebuild keeps no pin's bytes.
        if (options.homes.isEmpty() || options.homes.contains(assethome::Kind::Project))
            for (const auto &value : sidecar.value("pins").toArray())
                oids.insert(value.toObject().value("oid").toString());
    }
    return oids;
}

RebuildReport rebuildCatalog(const QString &dbPath, const QString &storeRoot,
                             const RebuildOptions &options)
{
    RebuildReport report;
    QElapsedTimer timer;
    timer.start();

    const QDir sidecarDir(QDir(storeRoot).filePath(QStringLiteral("sidecar")));
    if (!sidecarDir.exists()) {
        report.error = QStringLiteral("no sidecar directory under %1").arg(storeRoot);
        return report;
    }

    db::Connection conn = openOwn(dbPath, &report.error);
    if (!conn.isOpen()) return report;

    const QVector<QJsonObject> sidecars = restorable(storeRoot, options, &report);
    const bool keepPins = options.homes.isEmpty() || options.homes.contains(assethome::Kind::Project);

    // ONE UNIT (the layer's door: source.no_raw_transaction), named as the lock's holder - the schema (and, for a
    // recovery, its generation stamp) INSIDE it (the confirm's F7): a rebuild that does not commit leaves nothing. A
    // library reset's rebuild stamps nothing (options.stampGeneration false): the reset stamps once whole.
    db::Transaction unit(conn, "the catalog rebuild");
    // A fresh recovery DB needs the assets and dependencies tables too (IF NOT EXISTS: an existing one answers
    // nothing).
    db::Error schemaError;
    conn.exec(CasSchema::kAssetsTableForRebuild, &schemaError);
    conn.exec(CasSchema::kDependenciesTableForRebuild, &schemaError);
    AssetCas::ensureCasSchema(conn, options.stampGeneration);
    QSet<QString> restored;
    // EVERY ROW's ANSWER IS READ (the audit's finding 1): a sidecar one of whose rows failed is COUNTED and NAMED,
    // and the rebuild is then not ok - a catalog missing a row must never look whole to the sweep after it.
    QString firstFailure;
    const auto failed = [&report, &firstFailure](const QString &guid, const db::Error &e) {
        if (firstFailure.isEmpty()) firstFailure = QStringLiteral("%1: %2").arg(guid, dbqt::describe(e));
        if (report.failedGuids.contains(guid)) return;
        report.failedGuids.append(guid);
        ++report.failedRows;
    };
    for (const QJsonObject &sidecar : sidecars) {
        const QString guid = sidecar.value("guid").toString();
        assethome::Home home;
        sidecarHome(sidecar, &home);

        // The blob WITH its stamp (Database::blobStamp, `hash`), as every writer of the blob stores it:
        // a preview's key reads the stamp (services/previewkey.h).
        const QByteArray blob = sidecar.contains("asset")
            ? QJsonDocument(sidecar.value("asset").toObject()).toJson(QJsonDocument::Compact)
            : QByteArray();
        db::Statement insertAsset(conn, "INSERT OR IGNORE INTO assets (guid, name, type, view_filter, project_guid, "
                            "origin, parent, asset, hash, collection, author, license, properties, tags, "
                            "listed, date_created, last_updated, times_used) "
                            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime(), ?)");
        dbqt::bindText(insertAsset, 1, guid);
        dbqt::bindText(insertAsset, 2, sidecar.value("name").toString());
        insertAsset.bindInt(3, sidecar.value("type").toInt());
        insertAsset.bindInt(4, home.stored());
        dbqt::bindText(insertAsset, 5, home.isProject() ? home.projectGuid : QString());
        dbqt::bindText(insertAsset, 6, sidecar.value("origin").toString());
        dbqt::bindText(insertAsset, 7, sidecar.value("parent").toString());
        dbqt::bindBytes(insertAsset, 8, blob);
        dbqt::bindText(insertAsset, 9, Database::blobStamp(blob));
        insertAsset.bindInt(10, sidecar.value("collection").toInt());
        dbqt::bindText(insertAsset, 11, sidecar.value("author").toString());
        dbqt::bindText(insertAsset, 12, sidecar.value("license").toString());
        dbqt::bindBytes(insertAsset, 13, sidecar.contains("properties")
            ? QJsonDocument(sidecar.value("properties").toObject()).toJson(QJsonDocument::Compact)
            : QByteArray());
        dbqt::bindBytes(insertAsset, 14, sidecar.contains("tags")
            ? QJsonDocument(sidecar.value("tags").toObject()).toJson(QJsonDocument::Compact)
            : QByteArray());
        insertAsset.bindInt(15, sidecar.value("listed").toBool(true) ? 1 : 0);
        dbqt::bindText(insertAsset, 16, sidecar.value("dateCreated").toString());
        insertAsset.bindInt(17, sidecar.value("timesUsed").toInt());
        db::Error e;
        if (!insertAsset.run(&e)) { failed(guid, e); continue; }   // its files, pins and edges name nothing
        if (conn.changes() > 0) ++report.assets;
        restored.insert(guid);

        for (const auto &value : sidecar.value("files").toArray()) {
            const QJsonObject fileObj = value.toObject();
            if (options.dropDerived && isDerivedRole(fileObj.value("role").toString())) continue;

            db::Statement insertFile(conn, "INSERT OR IGNORE INTO files (oid, size, ext, refcount) VALUES (?, ?, ?, 0)");
            dbqt::bindText(insertFile, 1, fileObj.value("oid").toString());
            insertFile.bindInt(2, qint64(fileObj.value("size").toDouble()));
            dbqt::bindText(insertFile, 3, fileObj.value("ext").toString());
            if (!insertFile.run(&e)) failed(guid, e);
            else if (conn.changes() > 0) ++report.files;

            db::Statement insertLink(conn, "INSERT OR IGNORE INTO asset_files (asset_guid, role, oid, name) VALUES (?, ?, ?, ?)");
            dbqt::bindText(insertLink, 1, guid);
            dbqt::bindText(insertLink, 2, fileObj.value("role").toString());
            dbqt::bindText(insertLink, 3, fileObj.value("oid").toString());
            dbqt::bindText(insertLink, 4, fileObj.value("name").toString());
            if (!insertLink.run(&e)) failed(guid, e);
            else if (conn.changes() > 0) ++report.links;
        }

        // PINS (item 1c'): a project's version of this asset — restored only
        // when projects are (a storage-only rebuild has none to pin into).
        if (!keepPins) continue;
        for (const auto &value : sidecar.value("pins").toArray()) {
            const QJsonObject pinObj = value.toObject();
            const QString oid = pinObj.value("oid").toString();
            const QString projectGuid = pinObj.value("projectGuid").toString();
            if (oid.isEmpty() || projectGuid.isEmpty()) continue;
            if (!QFileInfo::exists(AssetStorePaths::objectPathIn(
                    storeRoot, oid, pinObj.value("ext").toString())))
                continue;   // the pinned bytes are gone; do not pin at a hole

            db::Statement insertFile(conn, "INSERT OR IGNORE INTO files (oid, size, ext, refcount) VALUES (?, ?, ?, 0)");
            dbqt::bindText(insertFile, 1, oid);
            insertFile.bindInt(2, qint64(pinObj.value("size").toDouble()));
            dbqt::bindText(insertFile, 3, pinObj.value("ext").toString());
            db::Error e;
            if (!insertFile.run(&e)) failed(guid, e);
            else if (conn.changes() > 0) ++report.files;

            db::Statement insertPin(conn, "INSERT OR IGNORE INTO project_assets (project_guid, asset_guid, oid_pin) "
                                          "VALUES (?, ?, ?)");
            dbqt::bindText(insertPin, 1, projectGuid);
            dbqt::bindText(insertPin, 2, guid);
            dbqt::bindText(insertPin, 3, oid);
            if (!insertPin.run(&e)) failed(guid, e);
            else if (conn.changes() > 0) ++report.pins;
        }
    }

    // THE INTRINSIC EDGES, after every row is back: an edge whose dependee did
    // not come back (another home, a tombstone) would name nothing.
    for (const QJsonObject &sidecar : sidecars) {
        const QString guid = sidecar.value("guid").toString();
        const int type = sidecar.value("type").toInt();
        for (const auto &value : sidecar.value("dependencies").toArray()) {
            const QJsonObject edge = value.toObject();
            const QString dependee = edge.value("dependee").toString();
            if (dependee.isEmpty() || !restored.contains(dependee)) continue;
            bool known = false;
            {
                // Block-scoped: finished before the write.
                db::Statement have(conn, "SELECT 1 FROM dependencies WHERE depender = ? AND dependee = ? "
                                         "AND project_guid IS NULL");
                dbqt::bindText(have, 1, guid);
                dbqt::bindText(have, 2, dependee);
                db::Error e;
                known = have.step(&e);
                if (e) { failed(guid, e); continue; }   // a failed read never decides the insert
            }
            if (known) continue;
            db::Statement insertEdge(conn, "INSERT INTO dependencies (depender_type, dependee_type, project_guid, "
                                           "depender, dependee, id) VALUES (?, ?, NULL, ?, ?, ?)");
            insertEdge.bindInt(1, type);
            insertEdge.bindInt(2, edge.value("dependeeType").toInt());
            dbqt::bindText(insertEdge, 3, guid);
            dbqt::bindText(insertEdge, 4, dependee);
            dbqt::bindText(insertEdge, 5, QString::fromStdString(iris::newGuid()));
            db::Error e;
            if (insertEdge.run(&e)) ++report.edges;
            else failed(guid, e);
        }
    }

    // A REBUILD WITH A FAILED ROW COMMITS NOTHING (the confirm's F7): its unit rolls back, so the catalog is as it was
    // before it and nothing is stamped. (The counts above are then what was
    // written before the rollback.)
    report.elapsedMs = timer.elapsed();
    if (report.failedRows > 0) {
        unit.rollback();
        report.error = QStringLiteral("%1 stored item(s) could not be rebuilt (%2; first failure %3) - nothing was "
                                      "committed")
                           .arg(report.failedRows).arg(report.failedGuids.join(QStringLiteral(", ")), firstFailure);
        return report;
    }
    db::Error commitError;
    if (!unit.commit(&commitError)) {
        report.error = QStringLiteral("commit failed: %1").arg(dbqt::describe(commitError));
        return report;
    }
    report.committed = true;
    report.ok = true;
    return report;
}

} // namespace AssetMigration
