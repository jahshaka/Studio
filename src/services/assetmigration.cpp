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

    // EVERY STORAGE ROW HAS ITS SIDECAR (ASSETS-HOME-1): a row of Assets, the
    // Materials or the Avatar storage without one is a row a Clear Database or
    // a format bump cannot rebuild — it would be lost, so it is a finding.
    QSqlQuery rows(scoped.db);
    rows.prepare("SELECT guid FROM assets WHERE view_filter IN (?, ?, ?)");
    rows.addBindValue(static_cast<int>(assethome::StoredAssets));
    rows.addBindValue(static_cast<int>(assethome::StoredMaterialsLibrary));
    rows.addBindValue(static_cast<int>(assethome::StoredAvatarLibrary));
    if (rows.exec())
        while (rows.next()) {
            const QString guid = rows.value(0).toString();
            if (!QFileInfo::exists(AssetStorePaths::sidecarPathIn(storeRoot, guid)))
                report.missingSidecars << guid;
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
        if (sidecar.value("guid").toString().isEmpty()) { unreadable(QStringLiteral("no guid")); continue; }
        assethome::Home home;
        if (!sidecarHome(sidecar, &home)) {
            unreadable(QStringLiteral("no home in this format (formatVersion %1)")
                           .arg(sidecar.value("formatVersion").toInt()));
            continue;
        }
        if (!wanted(home, options)) { if (report) ++report->otherHomes; continue; }
        // THE ONE DOOR'S RULE, HERE TOO (ASSETS-HOME-1): a rebuild is not a
        // second way into a home. A sidecar whose (home, origin) the door would
        // refuse is not restored — counted, named, logged.
        {
            assethome::Origin origin;
            if (!assethome::originFromName(sidecar.value("origin").toString(), &origin)) {
                unreadable(QStringLiteral("no origin in this format"));
                continue;
            }
            const QString refused = assethome::refusal(home, origin);
            if (!refused.isEmpty()) { unreadable(refused); continue; }
        }
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

    ScopedConnection scoped(dbPath);
    if (!scoped.opened) {
        report.error = QStringLiteral("cannot open %1").arg(dbPath);
        return report;
    }
    QSqlDatabase conn = scoped.db;

    // A fresh recovery DB needs the assets and dependencies tables too.
    QSqlQuery(CasSchema::kAssetsTableForRebuild, conn);
    QSqlQuery(CasSchema::kDependenciesTableForRebuild, conn);
    AssetCas::ensureCasSchema(conn);

    const QVector<QJsonObject> sidecars = restorable(storeRoot, options, &report);
    const bool keepPins = options.homes.isEmpty() || options.homes.contains(assethome::Kind::Project);

    conn.transaction();
    QSet<QString> restored;
    for (const QJsonObject &sidecar : sidecars) {
        const QString guid = sidecar.value("guid").toString();
        assethome::Home home;
        sidecarHome(sidecar, &home);

        QSqlQuery insertAsset(conn);
        insertAsset.prepare("INSERT OR IGNORE INTO assets (guid, name, type, view_filter, project_guid, "
                            "origin, parent, asset, collection, author, license, properties, tags, "
                            "listed, date_created, last_updated, times_used) "
                            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime(), ?)");
        insertAsset.addBindValue(guid);
        insertAsset.addBindValue(sidecar.value("name").toString());
        insertAsset.addBindValue(sidecar.value("type").toInt());
        insertAsset.addBindValue(home.stored());
        insertAsset.addBindValue(home.isProject() ? QVariant(home.projectGuid)
                                                  : QVariant(QMetaType(QMetaType::QString)));
        insertAsset.addBindValue(sidecar.value("origin").toString());
        insertAsset.addBindValue(sidecar.value("parent").toString());
        insertAsset.addBindValue(sidecar.contains("asset")
            ? QJsonDocument(sidecar.value("asset").toObject()).toJson(QJsonDocument::Compact)
            : QByteArray());
        insertAsset.addBindValue(sidecar.value("collection").toInt());
        insertAsset.addBindValue(sidecar.value("author").toString());
        insertAsset.addBindValue(sidecar.value("license").toString());
        insertAsset.addBindValue(sidecar.contains("properties")
            ? QJsonDocument(sidecar.value("properties").toObject()).toJson(QJsonDocument::Compact)
            : QByteArray());
        insertAsset.addBindValue(sidecar.contains("tags")
            ? QJsonDocument(sidecar.value("tags").toObject()).toJson(QJsonDocument::Compact)
            : QByteArray());
        insertAsset.addBindValue(sidecar.value("listed").toBool(true) ? 1 : 0);
        insertAsset.addBindValue(sidecar.value("dateCreated").toString());
        insertAsset.addBindValue(sidecar.value("timesUsed").toInt());
        if (insertAsset.exec() && insertAsset.numRowsAffected() > 0) ++report.assets;
        restored.insert(guid);

        for (const auto &value : sidecar.value("files").toArray()) {
            const QJsonObject fileObj = value.toObject();
            if (options.dropDerived && isDerivedRole(fileObj.value("role").toString())) continue;

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

            QSqlQuery insertFile(conn);
            insertFile.prepare("INSERT OR IGNORE INTO files (oid, size, ext, refcount) VALUES (?, ?, ?, 0)");
            insertFile.addBindValue(oid);
            insertFile.addBindValue(qint64(pinObj.value("size").toDouble()));
            insertFile.addBindValue(pinObj.value("ext").toString());
            if (insertFile.exec() && insertFile.numRowsAffected() > 0) ++report.files;

            QSqlQuery insertPin(conn);
            insertPin.prepare("INSERT OR IGNORE INTO project_assets (project_guid, asset_guid, oid_pin) "
                              "VALUES (?, ?, ?)");
            insertPin.addBindValue(projectGuid);
            insertPin.addBindValue(guid);
            insertPin.addBindValue(oid);
            if (insertPin.exec() && insertPin.numRowsAffected() > 0) ++report.pins;
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
            QSqlQuery have(conn);
            have.prepare("SELECT 1 FROM dependencies WHERE depender = ? AND dependee = ? "
                         "AND project_guid IS NULL");
            have.addBindValue(guid);
            have.addBindValue(dependee);
            if (have.exec() && have.next()) continue;
            QSqlQuery insertEdge(conn);
            insertEdge.prepare("INSERT INTO dependencies (depender_type, dependee_type, project_guid, "
                               "depender, dependee, id) VALUES (?, ?, NULL, ?, ?, ?)");
            insertEdge.addBindValue(type);
            insertEdge.addBindValue(edge.value("dependeeType").toInt());
            insertEdge.addBindValue(guid);
            insertEdge.addBindValue(dependee);
            insertEdge.addBindValue(QUuid::createUuid().toString(QUuid::WithoutBraces));
            if (insertEdge.exec()) ++report.edges;
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
