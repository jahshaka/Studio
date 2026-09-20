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
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QVector>
#include <atomic>

#ifdef Q_OS_UNIX
#include <unistd.h>   // link(2) — hardlink migration, preflight §3.3
#endif

#include "irisgl/core/logger.h"
#include "data/database/casschema.h"
#include "data/database/database.h"   // DbTransaction
#include "data/project.h"          // ModelTypes — the asset `type` column
#include "services/assetstorepaths.h"
#include "services/filewriteatomic.h"

namespace AssetCas
{

namespace {
/// See `deviceWaits()`. Relaxed: it is a counter nobody orders anything by.
std::atomic<quint64> sDeviceWaits{0};
} // namespace

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
        noteDeviceWait();
    }

    return FileWrite::atomicRename(tmpPath, dstPath, errorOut);
}

// --- The two-phase ingest (FSYNC-1; contract in the header) ---------------

bool stage(const QString &root, Staged &file)
{
    const QFileInfo info(file.srcPath);
    if (!info.exists() || !info.isFile()) {
        file.error = QStringLiteral("no such file %1").arg(file.srcPath);
        return false;
    }
    file.size = info.size();
    file.ext  = info.suffix().toLower();
    file.oid  = file.knownOid.isEmpty() ? hashFile(file.srcPath) : file.knownOid;
    if (file.oid.isEmpty()) {
        file.error = QStringLiteral("cannot hash %1").arg(file.srcPath);
        return false;
    }

    // THE DEDUP, WITHOUT THE DATABASE. The store's extension for a known oid
    // lives in the files table, which this thread may not touch — but the
    // object's DIRECTORY depends only on the oid (objects/<ab>/<oid>.<ext>), so
    // "is this content already here, under whatever name" is one directory
    // listing. A match of the right size means there is nothing to copy; the
    // commit resolves the real extension and confirms it.
    const QString dir = QFileInfo(AssetStorePaths::objectPathIn(root, file.oid, file.ext)).absolutePath();
    const auto existing = QDir(dir).entryInfoList({ file.oid.toLower() + QStringLiteral(".*"),
                                                    file.oid.toLower() }, QDir::Files);
    for (const QFileInfo &candidate : existing) {
        if (candidate.size() != file.size) continue;
        file.present = true;
        return true;
    }

    if (!QDir().mkpath(dir)) {
        file.error = QStringLiteral("cannot create %1").arg(dir);
        return false;
    }
    // The temp is named for the OID, not for the final path: the extension is
    // the store's to decide (commitStaged), and the rename that publishes this
    // must stay inside one directory.
    const QString tmpPath = FileWrite::stagingTempPath(dir + QLatin1Char('/') + file.oid.toLower());
    QFile::remove(tmpPath);
#ifdef Q_OS_UNIX
    // Hardlink first: same filesystem = ~0 extra bytes, instant, and nothing to
    // flush (the bytes are the source's). A copy is the fallback across devices
    // (EXDEV) — which is the archive import's normal case, since the archive is
    // extracted into a QTemporaryDir and /tmp is a different filesystem.
    file.copied = ::link(QFile::encodeName(file.srcPath).constData(),
                         QFile::encodeName(tmpPath).constData()) != 0;
#else
    file.copied = true;
#endif
    if (file.copied && !QFile::copy(file.srcPath, tmpPath)) {
        QFile::remove(tmpPath);
        file.error = QStringLiteral("copy failed: %1 -> %2").arg(file.srcPath, tmpPath);
        return false;
    }
    file.tmpPath = tmpPath;
    return true;
}

void flushStaged(QVector<Staged> &files)
{
    // The batch's ONE waiting point. Every copy, then nothing else: a hardlink
    // has no bytes of its own, and the directory entry is deliberately not
    // flushed (losing a rename loses an object, which is a re-ingest — losing
    // an object's CONTENT is a corruption, which is what this prevents).
    for (Staged &file : files)
        if (file.copied && !file.tmpPath.isEmpty()) {
            FileWrite::fsyncPath(file.tmpPath);
            noteDeviceWait();
        }
}

quint64 deviceWaits()
{
    return sDeviceWaits.load(std::memory_order_relaxed);
}

void noteDeviceWait()
{
    sDeviceWaits.fetch_add(1, std::memory_order_relaxed);
}

void discardStaged(QVector<Staged> &files)
{
    for (Staged &file : files) {
        if (!file.tmpPath.isEmpty()) QFile::remove(file.tmpPath);
        file.tmpPath.clear();
    }
}

bool commitStagedObject(QSqlDatabase conn, const QString &root, Staged &file, QString *errorOut)
{
    // The object half of commitStaged — the bytes into the store and the
    // `files` row — with NO asset_files link (ARCHIVE-GUIDS-1: an archive's
    // object for a row this library already holds is stored so the project
    // can PIN it, and is never linked as a second `source` of that row).
    return commitStaged(conn, root, QString(), file, errorOut);
}

bool commitStaged(QSqlDatabase conn, const QString &root, const QString &guid,
                  Staged &file, QString *errorOut)
{
    if (file.oid.isEmpty()) {
        if (errorOut) *errorOut = file.error.isEmpty()
                                      ? QStringLiteral("nothing staged for %1").arg(file.srcPath)
                                      : file.error;
        return false;
    }

    // Known content keeps its recorded extension (jpeg/jpg aliasing — one
    // object per oid, never a sibling copy under another name). This is the one
    // question the staging thread could not answer.
    QString ext = file.ext;
    {
        QSqlQuery known(conn);
        known.prepare("SELECT ext FROM files WHERE oid = ?");
        known.addBindValue(file.oid);
        if (known.exec() && known.next()) ext = known.value(0).toString();
    }

    const QString dstPath = AssetStorePaths::objectPathIn(root, file.oid, ext);
    const QFileInfo dst(dstPath);
    if (dst.exists() && dst.size() == file.size) {
        // Already stored under the name the store uses. Whatever we staged (if
        // anything) is redundant.
        if (!file.tmpPath.isEmpty()) { QFile::remove(file.tmpPath); file.tmpPath.clear(); }
    } else if (!file.tmpPath.isEmpty()) {
        if (dst.exists())
            iris::Logger::getSingleton()->warn(
                QStringLiteral("asset store: replacing a %1-byte object that claims %2 bytes of "
                               "content (%3) — a torn write from an interrupted run")
                    .arg(dst.size()).arg(file.size).arg(dstPath));
        // The bytes are already durable (flushStaged); this is the publish.
        if (!FileWrite::atomicRename(file.tmpPath, dstPath, errorOut)) return false;
        file.tmpPath.clear();
    } else {
        // The staging thread found this content under a DIFFERENT extension
        // than the store records, or the object vanished between the two
        // phases. Rare, and the repair is the synchronous store — on this
        // thread, because correctness beats latency in a case that means the
        // store disagrees with itself.
        if (!storeObject(file.srcPath, root, file.oid, ext, errorOut)) return false;
    }

    QSqlQuery insertFile(conn);
    insertFile.prepare("INSERT OR IGNORE INTO files (oid, size, ext, refcount) VALUES (?, ?, ?, 0)");
    insertFile.addBindValue(file.oid);
    insertFile.addBindValue(file.size);
    insertFile.addBindValue(ext);
    if (!insertFile.exec()) {
        if (errorOut) *errorOut = QStringLiteral("files row refused: %1").arg(insertFile.lastError().text());
        return false;
    }

    if (guid.isEmpty()) return true;   // commitStagedObject: the object only
    QSqlQuery insertLink(conn);
    insertLink.prepare("INSERT OR IGNORE INTO asset_files (asset_guid, role, oid, name) VALUES (?, ?, ?, ?)");
    insertLink.addBindValue(guid);
    insertLink.addBindValue(file.role);
    insertLink.addBindValue(file.oid);
    insertLink.addBindValue(file.name.isEmpty() ? QFileInfo(file.srcPath).fileName() : file.name);
    if (!insertLink.exec()) {
        if (errorOut) *errorOut = QStringLiteral("asset_files row refused: %1").arg(insertLink.lastError().text());
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
    if (!insertFile.exec()) {
        if (errorOut) *errorOut = QStringLiteral("files row refused: %1").arg(insertFile.lastError().text());
        return false;
    }

    QSqlQuery insertLink(conn);
    insertLink.prepare("INSERT OR IGNORE INTO asset_files (asset_guid, role, oid, name) VALUES (?, ?, ?, ?)");
    insertLink.addBindValue(guid);
    insertLink.addBindValue(role);
    insertLink.addBindValue(oid);
    insertLink.addBindValue(name.isEmpty() ? info.fileName() : name);
    if (!insertLink.exec()) {
        if (errorOut) *errorOut = QStringLiteral("asset_files row refused: %1").arg(insertLink.lastError().text());
        return false;
    }
    return true;
}

QString sourceOid(QSqlDatabase conn, const QString &guid)
{
    QSqlQuery query(conn);
    query.prepare("SELECT oid FROM asset_files WHERE asset_guid = ? "
                  "ORDER BY CASE role WHEN 'source' THEN 0 ELSE 1 END, name");
    query.addBindValue(guid);
    if (query.exec() && query.next()) return query.value(0).toString();
    return QString();
}

bool moveSourcePointer(QSqlDatabase conn, const QString &guid, const QString &oid,
                       const QString &name, QString *errorOut)
{
    if (oid.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("no content id to point at");
        return false;
    }

    // Which row: the named one, else the asset's single source row.
    QString targetName = name;
    if (targetName.isEmpty()) {
        QSqlQuery pick(conn);
        pick.prepare("SELECT name FROM asset_files WHERE asset_guid = ? AND role = 'source'");
        pick.addBindValue(guid);
        if (pick.exec() && pick.next()) targetName = pick.value(0).toString();
    }
    if (targetName.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("asset %1 has no source file row").arg(guid);
        return false;
    }

    // DELETE + INSERT, not UPDATE: the refcount triggers are AFTER INSERT and
    // AFTER DELETE on asset_files, so an UPDATE would move the pointer while
    // leaving the OLD object's refcount inflated (it is then never collectable)
    // and the new one's at zero (assets.gc would reap the bytes the library
    // just published). One transaction, so a reader never sees no source row.
    DbTransaction tx(conn);
    QSqlQuery drop(conn);
    drop.prepare("DELETE FROM asset_files WHERE asset_guid = ? AND role = 'source' AND name = ?");
    drop.addBindValue(guid);
    drop.addBindValue(targetName);
    if (!drop.exec()) {
        if (errorOut) *errorOut = drop.lastError().text();
        return false;
    }
    QSqlQuery add(conn);
    add.prepare("INSERT OR REPLACE INTO asset_files (asset_guid, role, oid, name) "
                "VALUES (?, 'source', ?, ?)");
    add.addBindValue(guid);
    add.addBindValue(oid);
    add.addBindValue(targetName);
    if (!add.exec()) {
        if (errorOut) *errorOut = add.lastError().text();
        return false;
    }
    return tx.commit();
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
        return QString();
    }

    // NO asset_files row at all. That is not only "a DB-only asset": writers
    // outside the ONE import pipeline still drop a file straight into
    // <root>/<guid>/ and create the row (the materials module's texture
    // import does exactly this), and until the retired legacy view is swept
    // those bytes are the asset's only copy. The resolver is now the single
    // place that knows about them — which is what let every call site stop
    // building <root>/<guid>/<name> for itself.
    QSqlQuery byName(conn);
    byName.prepare("SELECT name FROM assets WHERE guid = ?");
    byName.addBindValue(guid);
    if (byName.exec() && byName.next()) {
        const QString name = byName.value(0).toString();
        if (name.isEmpty()) return QString();
        const QString legacy = QDir(AssetStorePaths::legacyFolderIn(root, guid)).filePath(name);
        if (QFileInfo::exists(legacy)) {
            if (nameOut) *nameOut = name;
            return legacy;
        }
    }
    return QString();
}

bool writeSidecar(QSqlDatabase conn, const QString &root, const QString &guid,
                  QString *errorOut)
{
    QSqlQuery assetQuery(conn);
    assetQuery.prepare("SELECT name, type, view_filter, collection, author, license, properties, tags, listed, "
                       "parent, asset "
                       "FROM assets WHERE guid = ?");
    assetQuery.addBindValue(guid);
    if (!assetQuery.exec() || !assetQuery.next()) {
        if (errorOut) *errorOut = QStringLiteral("no asset row for %1").arg(guid);
        return false;
    }

    QJsonObject sidecar;
    // FORMAT 2 (bundles audit G5): a sidecar carries the asset's RELATIONS,
    // not only its bytes. Version 1 recorded identity, organization and the
    // file manifest and nothing else — the comment that claimed otherwise was
    // wrong — so after `assets.rebuildCatalog` an Object had no material
    // bindings, a bundle's members showed as loose tiles and every closure
    // walk came back empty. A reader of a v1 sidecar simply finds the three
    // keys below absent, which is exactly what it meant before.
    sidecar["formatVersion"] = 2;
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
    // LIBRARY VISIBILITY (library delete keeps project pins): recorded so a
    // rebuildCatalog does not resurrect an unlisted row as a library tile.
    // Absent in older sidecars — the reader defaults to listed.
    sidecar["listed"] = assetQuery.value(8).toInt() != 0;

    // MEMBERSHIP (G5). `parent` is what makes a Mesh row the inside of its
    // model and a baked map the inside of its material — the one relation
    // both browsers hide by — and it lived in no sidecar at all.
    const QString parent = assetQuery.value(9).toString();
    if (!parent.isEmpty()) sidecar["parent"] = parent;

    // THE DB-ONLY PAYLOAD (G5 + the material spec's F12). A Material's, a
    // Sky's and a ParticleSystem's meaning is the `asset` blob, so a rebuild
    // without it restores a row that resolves to nothing. A material's
    // definition is ALSO a store object now (MATERIAL_BUNDLE_SPEC D-2) and so
    // rides the file manifest above; the blob is its cache, and carrying it
    // costs a few hundred bytes and closes the gap for the kinds that have no
    // file at all.
    {
        const QJsonDocument blob = QJsonDocument::fromJson(assetQuery.value(10).toByteArray());
        if (blob.isObject()) sidecar["asset"] = blob.object();
    }

    // INTRINSIC DEPENDENCY EDGES (G5): the ones with NO project stamp — a
    // bundle's own membership, derived from its definition
    // (MaterialBundle::reconcileEdges) and true wherever the asset goes. A
    // project-stamped edge is that project's record of a USE and belongs to
    // the project, not to this asset.
    {
        QJsonArray edges;
        QSqlQuery depQuery(conn);
        depQuery.prepare("SELECT dependee, dependee_type FROM dependencies "
                         "WHERE depender = ? AND project_guid IS NULL "
                         "ORDER BY dependee");
        depQuery.addBindValue(guid);
        if (depQuery.exec()) {
            while (depQuery.next()) {
                QJsonObject edge;
                edge["dependee"] = depQuery.value(0).toString();
                edge["dependeeType"] = depQuery.value(1).toInt();
                edges.append(edge);
            }
        }
        if (!edges.isEmpty()) sidecar["dependencies"] = edges;
    }

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

    // PIN-ONLY OBJECTS (item 1c', ENGINEERING_DEBT_SPEC §1 item 1c point 4).
    // The manifest above enumerates asset_files, and a copy-on-write edit does
    // NOT produce an asset_files row: ProjectAssets::copyOnWrite ingests the
    // edited bytes and moves the project's pin, while the link insert
    // (INSERT OR IGNORE, PK (asset_guid, role, name)) is IGNORED because the
    // edited file keeps its name. The bytes the project actually renders were
    // therefore recorded in NO sidecar, so assets.rebuildCatalog — the
    // "somebody deleted the database" recovery path — could not bring a
    // COW-edited asset back: it rebuilt the row pointing at the pre-edit
    // object and dropped the pin entirely.
    //
    // Pins are recorded as their own array rather than folded into `files`
    // because they are a per-PROJECT fact, not a library one: rebuildCatalog
    // restores the files row plus the project_assets pin, and the asset's
    // library mapping stays exactly where the manifest put it.
    QJsonArray pins;
    QSqlQuery pinQuery(conn);
    pinQuery.prepare("SELECT PA.project_guid, PA.oid_pin, F.size, F.ext "
                     "FROM project_assets PA LEFT JOIN files F ON F.oid = PA.oid_pin "
                     "WHERE PA.asset_guid = ? AND PA.oid_pin IS NOT NULL AND PA.oid_pin <> '' "
                     "ORDER BY PA.project_guid");
    pinQuery.addBindValue(guid);
    pinQuery.exec();
    while (pinQuery.next()) {
        QJsonObject pin;
        pin["projectGuid"] = pinQuery.value(0).toString();
        pin["oid"] = pinQuery.value(1).toString();
        pin["size"] = pinQuery.value(2).toDouble();
        pin["ext"] = pinQuery.value(3).toString();
        pins.append(pin);
    }
    sidecar["pins"] = pins;

    // Atomic, like every other artifact the store owns: this used to truncate
    // the live sidecar and then write, so an interrupted import left a
    // zero/half-length JSON that rebuildCatalog would read as an asset with no
    // files at all (deep audit 2026-09, area 6).
    //
    // DERIVED, so no fsync (FSYNC-2). Everything above this line is a SELECT:
    // the sidecar is a projection of catalog rows that SQLite has already made
    // durable, and re-writing it is this function. Flushing it cost 167-1 153
    // ms of frozen UI on this box's store device, once per touched guid, and
    // bought only "the last sidecar is not lost" — while the rename it still
    // does is what buys "no reader ever sees half of one". A sidecar that a
    // power cut leaves torn does not parse, and the rebuild skips what does not
    // parse (assetmigration.cpp's guid.isEmpty() guard), so the failure mode is
    // a re-write, never a wrong row.
    const QString path = AssetStorePaths::sidecarPathIn(root, guid);
    return FileWrite::writeFileAtomic(
        path, QJsonDocument(sidecar).toJson(QJsonDocument::Indented), errorOut,
        FileWrite::Durability::Derived);
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
                         const QString &projectGuid, GuidPreference prefer)
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
    // the store), so the row has to be CHOSEN, not taken. Schema facts the
    // ordering below rests on:
    //
    //   * asset_files is (asset_guid, role, oid, name), PK (guid, role, name),
    //     with the only index on oid — so an unordered lookup by oid answers
    //     in index/rowid order, which is INSERTION order, which is an accident
    //     of the importer's append sequence.
    //   * An imported model writes its textures twice: {Object, 'texture'}
    //     first, {member Texture, 'source'} second (assetimporters.cpp).
    //   * ProjectAssets::addToProject pins the WHOLE dependency closure, so
    //     both of those rows are pinned by the project — pinnedness cannot
    //     break that tie (the GLB texture-loss defect: the Object won, the
    //     writer stored the .glb's guid in every map slot, and the reader
    //     resolved it straight back to the .glb).
    //
    // Order: pinned first (a project's own content beats a library sibling's),
    // then the KIND the caller asked for, then role='source' — the row the
    // readers resolve through (resolveSource) — then the guid itself, so two
    // otherwise equal candidates always answer the same way on every machine.
    const int wantType = (prefer == GuidPreference::Texture)
                             ? static_cast<int>(ModelTypes::Texture) : -1;
    QSqlQuery query(conn);
    query.prepare("SELECT AF.asset_guid FROM asset_files AF "
                  "LEFT JOIN project_assets PA ON PA.asset_guid = AF.asset_guid "
                  "                           AND PA.project_guid = ? "
                  "LEFT JOIN assets A ON A.guid = AF.asset_guid "
                  "WHERE AF.oid = ? "
                  "ORDER BY (PA.asset_guid IS NOT NULL) DESC, "
                  "         CASE WHEN ? >= 0 AND A.type = ? THEN 0 ELSE 1 END, "
                  "         CASE AF.role WHEN 'source' THEN 0 ELSE 1 END, "
                  "         AF.asset_guid");
    query.addBindValue(projectGuid);
    query.addBindValue(oid);
    query.addBindValue(wantType);
    query.addBindValue(wantType);
    if (query.exec() && query.next()) return query.value(0).toString();
    return QString();
}

namespace {

/// Every `values` block in a stored asset blob (a serialized scene node: the
/// node's own material, and its children's, at any depth).
void collectMaterialValues(const QJsonValue &value, QVector<QJsonObject> &out)
{
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QJsonValue values = obj.value(QStringLiteral("values"));
        if (values.isObject()) out.append(values.toObject());
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            collectMaterialValues(it.value(), out);
    } else if (value.isArray()) {
        for (const QJsonValue &child : value.toArray()) collectMaterialValues(child, out);
    }
}

/// The words a texture's FILE NAME carries when it fills a given map slot.
/// Last resort only — the blob route above is exact when it answers.
QStringList slotKeywords(const QString &slotName)
{
    const QString slot = slotName.toLower();
    if (slot.contains(QStringLiteral("basecolor")) || slot.contains(QStringLiteral("diffuse"))
        || slot.contains(QStringLiteral("albedo")))
        return { "basecolor", "base_color", "albedo", "diffuse", "_col", "_d.", "color" };
    if (slot.contains(QStringLiteral("normal")))
        return { "normal", "_nrm", "_n.", "_norm" };
    if (slot.contains(QStringLiteral("metal")))
        return { "metal", "metallic", "_m." };
    if (slot.contains(QStringLiteral("rough")))
        return { "rough", "_r.", "gloss", "smooth" };
    if (slot.contains(QStringLiteral("emissi")))
        return { "emissi", "emit", "_e." };
    if (slot.contains(QStringLiteral("occlusion")) || slot == QStringLiteral("aomap"))
        return { "occlusion", "_ao", "ambient" };
    return {};
}

int assetTypeOf(QSqlDatabase conn, const QString &guid)
{
    QSqlQuery query(conn);
    query.prepare("SELECT type FROM assets WHERE guid = ?");
    query.addBindValue(guid);
    if (query.exec() && query.next()) return query.value(0).toInt();
    return -1;
}

} // namespace

QString textureGuidForSlot(QSqlDatabase conn, const QString &storedGuid,
                           const QString &slotName)
{
    if (storedGuid.isEmpty()) return QString();
    const int type = assetTypeOf(conn, storedGuid);
    // Nothing to repair: the slot already names a Texture, or names a guid this
    // catalog has never heard of (a path, a foreign scene's asset) — in which
    // case guessing would be inventing content, not healing it.
    if (type < 0 || type == static_cast<int>(ModelTypes::Texture)) return QString();

    // The object's texture members: assets rows whose bytes ALSO hang off the
    // object (role 'texture'), joined back through the shared oid. That join
    // is exactly the ambiguity the writer used to lose, read deliberately.
    struct Member { QString guid, name; };
    QVector<Member> members;
    {
        QSqlQuery query(conn);
        query.prepare("SELECT DISTINCT M.asset_guid, M.name FROM asset_files OBJ "
                      "JOIN asset_files M ON M.oid = OBJ.oid AND M.asset_guid <> OBJ.asset_guid "
                      "JOIN assets A ON A.guid = M.asset_guid "
                      "WHERE OBJ.asset_guid = ? AND A.type = ? "
                      "ORDER BY M.name");
        query.addBindValue(storedGuid);
        query.addBindValue(static_cast<int>(ModelTypes::Texture));
        if (query.exec())
            while (query.next())
                members.append({ query.value(0).toString(), query.value(1).toString() });
    }
    if (members.isEmpty()) return QString();

    // ROUTE 1 — the object's own blob. The importer rewrote every material's
    // texture values to the member texture guids before storing it, so the
    // blob still holds the mapping the scene lost. Accept it only when the
    // slot resolves to ONE member across the whole object (a model whose
    // materials disagree about, say, normalMap cannot be repaired from a guid
    // that no longer says which material it belonged to).
    {
        QSqlQuery query(conn);
        query.prepare("SELECT asset FROM assets WHERE guid = ?");
        query.addBindValue(storedGuid);
        if (query.exec() && query.next()) {
            const QJsonDocument doc = QJsonDocument::fromJson(query.value(0).toByteArray());
            QVector<QJsonObject> valueBlocks;
            collectMaterialValues(doc.isArray() ? QJsonValue(doc.array())
                                                : QJsonValue(doc.object()), valueBlocks);
            QSet<QString> candidates;
            for (const QJsonObject &values : valueBlocks) {
                const QString ref = values.value(slotName).toString();
                if (ref.isEmpty()) continue;
                for (const Member &member : members)
                    if (member.guid == ref) candidates.insert(ref);
            }
            if (candidates.size() == 1) return *candidates.constBegin();
        }
    }

    // ROUTE 2 — the file names. One texture on the whole object can only ever
    // have been this slot's; otherwise the name has to say so.
    if (members.size() == 1) return members.first().guid;
    const QStringList words = slotKeywords(slotName);
    QString match;
    for (const Member &member : members) {
        const QString name = member.name.toLower();
        bool hit = false;
        for (const QString &word : words) if (name.contains(word)) { hit = true; break; }
        if (!hit) continue;
        if (!match.isEmpty() && match != member.guid) return QString();  // ambiguous
        match = member.guid;
    }
    return match;
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
    info["formatVersion"] = kStoreFormatVersion;

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

QString repairTextureSlot(const QString &storedGuid, const QString &slotName, const char *who)
{
    if (storedGuid.isEmpty() || slotName.isEmpty()) return storedGuid;
    const QString repaired = textureGuidForSlot(QSqlDatabase::database(), storedGuid, slotName);
    if (repaired.isEmpty()) return storedGuid;
    irisLog(QString("%1: %2 named the object '%3' instead of a texture - repaired to '%4'")
                .arg(QLatin1String(who), slotName, storedGuid, repaired));
    return repaired;
}

bool readStoreInfo(const QString &root, QString *storeIdOut, int *formatVersionOut)
{
    QFile file(AssetStorePaths::storeInfoPathIn(root));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonObject obj = QJsonDocument::fromJson(file.readAll()).object();
    if (obj.isEmpty()) return false;
    if (storeIdOut) *storeIdOut = obj.value("storeId").toString();
    if (formatVersionOut) *formatVersionOut = obj.value("formatVersion").toInt(0);
    return true;
}

} // namespace AssetCas
