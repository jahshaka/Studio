/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "services/assetcas.h"

#include "data/assethomekind.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QVector>
#include <atomic>

#ifdef Q_OS_UNIX
#include <unistd.h>   // link(2) — hardlink migration, preflight §3.3
#endif

#include "services/jahlog.h"
#include "data/database/casschema.h"
#include "data/database/database.h"   // DbTransaction
#include "data/database/dbqt.h"
#include "data/db/statement.h"
#include "data/project.h"          // ModelTypes — the asset `type` column
#include "services/assetstorepaths.h"
#include "app/qtstd.h"
#include "irisgl/core/files.h"
#include "irisgl/core/guid.h"

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
        JahLog::write(JahLog::legacy, JahLog::Level::Warning,
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
    // (iris::files::stagingTempPath + iris::files::atomicRename — the same tail
    // sidecars, store.json and the baked maps now share; deep audit area 6).
    // rename(2) is atomic within a filesystem and REPLACES an existing target,
    // so the remove disappears too. The temp must be in the destination's own
    // directory for that ("same filesystem"), which mkpath above guarantees
    // exists.
    //
    // The staging step itself is NOT a byte write (hardlink first, copy only
    // as the fallback), which is why storeObject drives the pieces rather than
    // calling iris::files::writeAtomic.
    //
    // (Found while building this: Qt 6.10 on Linux already gets QFile::copy
    // atomic by accident — it opens the destination with O_TMPFILE and linkat()s
    // it into place, so the tear could not be reproduced on this box. That is a
    // fast path, not a contract: it needs a filesystem that supports O_TMPFILE,
    // and QFile::copy on macOS and Windows has no such property. The guarantee
    // is ours now, on every platform.)
    const QString tmpPath = qtstd::fromPath(iris::files::stagingTempPath(qtstd::toPath(dstPath)));
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
        const bool flushed = iris::files::fsyncPath(qtstd::toPath(tmpPath));
        noteDeviceWait();
        if (!flushed) {   // bytes that may not be on the device are never published under their content's name
            QFile::remove(tmpPath);
            if (errorOut) *errorOut = QStringLiteral("cannot flush %1 to its device (fsync failed): nothing stored").arg(dstPath);
            return false;
        }
    }

    std::string renameError;
    if (iris::files::atomicRename(qtstd::toPath(tmpPath), qtstd::toPath(dstPath), &renameError)) return true;
    if (errorOut) *errorOut = qtstd::toQString(renameError);
    return false;
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
    const QString tmpPath = qtstd::fromPath(iris::files::stagingTempPath(qtstd::toPath(dir + QLatin1Char('/') + file.oid.toLower())));
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
    // A COPY WHOSE FLUSH FAILED IS DISCARDED, never published: its temp is removed, its oid cleared and its error set, so
    // its commitStaged fails by that name.
    for (Staged &file : files)
        if (file.copied && !file.tmpPath.isEmpty()) {
            const bool flushed = iris::files::fsyncPath(qtstd::toPath(file.tmpPath));
            noteDeviceWait();
            if (!flushed) {
                QFile::remove(file.tmpPath);
                file.error = QStringLiteral("cannot flush %1 to its device (fsync failed): nothing stored").arg(file.srcPath);
                file.tmpPath.clear();
                file.oid.clear();
            }
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

bool commitStagedObject(db::Connection &conn, const QString &root, Staged &file, QString *errorOut)
{
    // The object half of commitStaged — the bytes into the store and the
    // `files` row — with NO asset_files link (ARCHIVE-GUIDS-1: an archive's
    // object for a row this library already holds is stored so the project
    // can PIN it, and is never linked as a second `source` of that row).
    return commitStaged(conn, root, QString(), file, errorOut);
}

bool commitStaged(db::Connection &conn, const QString &root, const QString &guid,
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
        db::Statement known(conn, "SELECT ext FROM files WHERE oid = ?");
        dbqt::bindText(known, 1, file.oid);
        db::Error e;
        if (known.step(&e)) ext = dbqt::text(known, 0);
    }

    const QString dstPath = AssetStorePaths::objectPathIn(root, file.oid, ext);
    const QFileInfo dst(dstPath);
    if (dst.exists() && dst.size() == file.size) {
        // Already stored under the name the store uses. Whatever we staged (if
        // anything) is redundant.
        if (!file.tmpPath.isEmpty()) { QFile::remove(file.tmpPath); file.tmpPath.clear(); }
    } else if (!file.tmpPath.isEmpty()) {
        if (dst.exists())
            JahLog::write(JahLog::legacy, JahLog::Level::Warning,
                QStringLiteral("asset store: replacing a %1-byte object that claims %2 bytes of "
                               "content (%3) — a torn write from an interrupted run")
                    .arg(dst.size()).arg(file.size).arg(dstPath));
        // The bytes are already durable (flushStaged); this is the publish.
        std::string renameError;
        if (!iris::files::atomicRename(qtstd::toPath(file.tmpPath), qtstd::toPath(dstPath), &renameError)) {
            if (errorOut) *errorOut = qtstd::toQString(renameError);
            return false;
        }
        file.tmpPath.clear();
    } else {
        // The staging thread found this content under a DIFFERENT extension
        // than the store records, or the object vanished between the two
        // phases. Rare, and the repair is the synchronous store — on this
        // thread, because correctness beats latency in a case that means the
        // store disagrees with itself.
        if (!storeObject(file.srcPath, root, file.oid, ext, errorOut)) return false;
    }

    db::Statement insertFile(conn, "INSERT OR IGNORE INTO files (oid, size, ext, refcount) VALUES (?, ?, ?, 0)");
    dbqt::bindText(insertFile, 1, file.oid);
    insertFile.bindInt(2, file.size);
    dbqt::bindText(insertFile, 3, ext);
    db::Error e;
    if (!insertFile.run(&e)) {
        if (errorOut) *errorOut = QStringLiteral("files row refused: %1").arg(dbqt::describe(e));
        return false;
    }

    if (guid.isEmpty()) return true;   // commitStagedObject: the object only
    db::Statement insertLink(conn, "INSERT OR IGNORE INTO asset_files (asset_guid, role, oid, name) VALUES (?, ?, ?, ?)");
    dbqt::bindText(insertLink, 1, guid);
    dbqt::bindText(insertLink, 2, file.role);
    dbqt::bindText(insertLink, 3, file.oid);
    dbqt::bindText(insertLink, 4, file.name.isEmpty() ? QFileInfo(file.srcPath).fileName() : file.name);
    if (!insertLink.run(&e)) {
        if (errorOut) *errorOut = QStringLiteral("asset_files row refused: %1").arg(dbqt::describe(e));
        return false;
    }
    return true;
}

void ensureCasSchema(db::Connection &conn, bool stampGeneration)
{
    // Each statement as it always ran: on its own, its failure unread (an existing schema answers "already
    // exists" to nothing here: every statement is IF NOT EXISTS).
    db::Error e;
    for (const char *sql : { CasSchema::kFilesTable, CasSchema::kAssetFilesTable, CasSchema::kAssetFilesOidIndex,
                             CasSchema::kRefcountInsertTrigger, CasSchema::kRefcountDeleteTrigger,
                             CasSchema::kProjectAssetsTable })
        conn.exec(sql, &e);
    // A RESET's REBUILD CREATES ITS SCHEMA UNSTAMPED (the second confirm's finding 1): the reset stamps once whole.
    if (!stampGeneration) return;

    // PRAGMA user_version arrives with the CAS (spec §3.1.3) — informational
    // at this phase; never lowered.
    // A FAILED READ NEVER STAMPS (NOQT-DATABASE-1 stage 2, the sweep's h; as Database::createCasTables): a library
    // whose generation is unknown is not made this build's.
    int current = 0;
    {
        db::Statement versionQuery(conn, "PRAGMA user_version");
        db::Error read;
        if (!versionQuery.step(&read)) {
            qWarning("ensureCasSchema: the library's generation could not be read (%s) - it was not stamped",
                     qUtf8Printable(dbqt::describe(read)));
            return;
        }
        current = int(versionQuery.int64(0));
    }
    if (current != CasSchema::kUserVersion)
        conn.exec("PRAGMA user_version = " + std::to_string(CasSchema::kUserVersion), &e);
}

bool ingestFile(db::Connection &conn, const QString &root, const QString &srcPath,
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
        db::Statement known(conn, "SELECT ext FROM files WHERE oid = ?");
        dbqt::bindText(known, 1, oid);
        db::Error e;
        if (known.step(&e)) ext = dbqt::text(known, 0);
    }

    if (!storeObject(srcPath, root, oid, ext, errorOut)) return false;

    db::Statement insertFile(conn, "INSERT OR IGNORE INTO files (oid, size, ext, refcount) VALUES (?, ?, ?, 0)");
    dbqt::bindText(insertFile, 1, oid);
    insertFile.bindInt(2, info.size());
    dbqt::bindText(insertFile, 3, ext);
    db::Error e;
    if (!insertFile.run(&e)) {
        if (errorOut) *errorOut = QStringLiteral("files row refused: %1").arg(dbqt::describe(e));
        return false;
    }

    db::Statement insertLink(conn, "INSERT OR IGNORE INTO asset_files (asset_guid, role, oid, name) VALUES (?, ?, ?, ?)");
    dbqt::bindText(insertLink, 1, guid);
    dbqt::bindText(insertLink, 2, role);
    dbqt::bindText(insertLink, 3, oid);
    dbqt::bindText(insertLink, 4, name.isEmpty() ? info.fileName() : name);
    if (!insertLink.run(&e)) {
        if (errorOut) *errorOut = QStringLiteral("asset_files row refused: %1").arg(dbqt::describe(e));
        return false;
    }
    return true;
}

namespace {
/// THE ONE ORDER that names an asset's library version among its files: the 'source' row first.
constexpr const char *kSourceFirst = "CASE role WHEN 'source' THEN 0 ELSE 1 END, name";
}   // namespace

QString sourceOid(db::Connection &conn, const QString &guid, bool *readOk)
{
    db::Statement query(conn, dbqt::utf8(QStringLiteral("SELECT oid FROM asset_files WHERE asset_guid = ? ORDER BY %1")
                                             .arg(QString::fromLatin1(kSourceFirst))));
    dbqt::bindText(query, 1, guid);
    db::Error e;
    if (query.step(&e)) return dbqt::text(query, 0);
    if (e && readOk) *readOk = false;   // a failed read is no "no bytes"
    return QString();
}

QHash<QString, MemberPin> memberPins(db::Connection &conn, const QString &projectGuid, const QStringList &guids,
                                     bool *readOk)
{
    QHash<QString, MemberPin> out;
    if (guids.isEmpty()) return out;
    QString marks;
    for (int i = 0; i < guids.size(); ++i) marks += i ? QStringLiteral(",?") : QStringLiteral("?");
    // The library version is sourceOid's own choice (kSourceFirst), never a second transcription of it.
    db::Statement query(conn, dbqt::utf8(QStringLiteral(
        "SELECT A.guid, PA.asset_guid IS NOT NULL, PA.oid_pin, "
        "(SELECT AF.oid FROM asset_files AF WHERE AF.asset_guid = A.guid ORDER BY %1 LIMIT 1) "
        "FROM assets A LEFT JOIN project_assets PA ON PA.asset_guid = A.guid AND PA.project_guid = ? "
        "WHERE A.guid IN (%2)").arg(QString::fromLatin1(kSourceFirst), marks)));
    dbqt::bindText(query, 1, projectGuid);
    for (int i = 0; i < guids.size(); ++i) dbqt::bindText(query, i + 2, guids.at(i));
    db::Error e;
    while (query.step(&e)) {
        MemberPin row;
        row.held = query.int64(1) != 0;
        row.pin = dbqt::text(query, 2);
        row.source = dbqt::text(query, 3);
        out.insert(dbqt::text(query, 0), row);
    }
    if (e) {   // a short answer is no answer: a member it missed would be pinned at the library's version
        if (readOk) *readOk = false;
        return {};
    }
    return out;
}

bool moveSourcePointer(db::Connection &conn, const QString &guid, const QString &oid,
                       const QString &name, QString *errorOut)
{
    if (oid.isEmpty()) {
        if (errorOut) *errorOut = QStringLiteral("no content id to point at");
        return false;
    }

    // Which row: the named one, else the asset's single source row.
    QString targetName = name;
    if (targetName.isEmpty()) {
        db::Statement pick(conn, "SELECT name FROM asset_files WHERE asset_guid = ? AND role = 'source'");
        dbqt::bindText(pick, 1, guid);
        db::Error e;
        if (pick.step(&e)) targetName = dbqt::text(pick, 0);
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
    db::Statement drop(conn, "DELETE FROM asset_files WHERE asset_guid = ? AND role = 'source' AND name = ?");
    dbqt::bindText(drop, 1, guid);
    dbqt::bindText(drop, 2, targetName);
    db::Error e;
    if (!drop.run(&e)) {
        if (errorOut) *errorOut = dbqt::describe(e);
        return false;
    }
    db::Statement add(conn, "INSERT OR REPLACE INTO asset_files (asset_guid, role, oid, name) "
                            "VALUES (?, 'source', ?, ?)");
    dbqt::bindText(add, 1, guid);
    dbqt::bindText(add, 2, oid);
    dbqt::bindText(add, 3, targetName);
    if (!add.run(&e)) {
        if (errorOut) *errorOut = dbqt::describe(e);
        return false;
    }
    return tx.commit();
}

QString resolveSource(db::Connection &conn, const QString &root,
                      const QString &guid, QString *nameOut, bool *readOk)
{
    db::Statement query(conn, "SELECT AF.name, AF.oid, F.ext FROM asset_files AF "
                              "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.asset_guid = ? "
                              "ORDER BY CASE AF.role WHEN 'source' THEN 0 ELSE 1 END, AF.name");
    dbqt::bindText(query, 1, guid);
    db::Error e;
    if (query.step(&e)) {
        const QString path = AssetStorePaths::objectPathIn(root, dbqt::text(query, 1), dbqt::text(query, 2));
        if (QFileInfo::exists(path)) {
            if (nameOut) *nameOut = dbqt::text(query, 0);
            return path;
        }
    }
    if (e && readOk) *readOk = false;   // a failed read is no "no bytes"
    // No asset_files row (a DB-only asset) or an object that is not on disk
    // (offline root, purged store): nothing to resolve. The pre-CAS per-guid
    // folders are not read (FORWARD-ONLY-1).
    return QString();
}

bool writeSidecar(db::Connection &conn, const QString &root, const QString &guid,
                  QString *errorOut)
{
    QJsonObject sidecar;
    {   // its last column read: no statement left active
    db::Statement assetQuery(conn, "SELECT name, type, view_filter, collection, author, license, properties, tags, listed, "
                                   "parent, asset, project_guid, origin, date_created, times_used "
                                   "FROM assets WHERE guid = ?");
    dbqt::bindText(assetQuery, 1, guid);
    db::Error e;
    if (!assetQuery.step(&e)) {
        if (errorOut) *errorOut = e ? dbqt::describe(e) : QStringLiteral("no asset row for %1").arg(guid);
        return false;
    }

    // FORMAT 2 (bundles audit G5): a sidecar carries the asset's RELATIONS,
    // not only its bytes. Version 1 recorded identity, organization and the
    // file manifest and nothing else — the comment that claimed otherwise was
    // wrong — so after `assets.rebuildCatalog` an Object had no material
    // bindings, a bundle's members showed as loose tiles and every closure
    // walk came back empty. A reader of a v1 sidecar simply finds the three
    // keys below absent, which is exactly what it meant before.
    //
    // FORMAT 3 (ASSETS-HOME-1): the row's HOME and ORIGIN by name, and its
    // creation time and use count — what a format bump needs to rebuild the
    // user's storages exactly (services/librarygeneration.h). The home replaces
    // the old bare `viewFilter` number.
    sidecar["formatVersion"] = 3;
    sidecar["guid"] = guid;
    sidecar["name"] = dbqt::text(assetQuery, 0);
    sidecar["type"] = int(assetQuery.int64(1));
    {
        const assethome::Home home = assethome::fromStored(int(assetQuery.int64(2)),
                                                           dbqt::text(assetQuery, 11));
        sidecar["home"] = assethome::kindName(home.kind);
        if (home.isProject()) sidecar["projectGuid"] = home.projectGuid;
        sidecar["origin"] = dbqt::text(assetQuery, 12);
        sidecar["dateCreated"] = dbqt::text(assetQuery, 13);
        sidecar["timesUsed"] = int(assetQuery.int64(14));
    }
    sidecar["collection"] = int(assetQuery.int64(3));
    sidecar["author"] = dbqt::text(assetQuery, 4);
    sidecar["license"] = dbqt::text(assetQuery, 5);
    const QJsonDocument props = QJsonDocument::fromJson(dbqt::bytes(assetQuery, 6));
    if (props.isObject()) sidecar["properties"] = props.object();
    const QJsonDocument tags = QJsonDocument::fromJson(dbqt::bytes(assetQuery, 7));
    if (tags.isObject()) sidecar["tags"] = tags.object();
    // LIBRARY VISIBILITY (library delete keeps project pins): recorded so a
    // rebuildCatalog does not resurrect an unlisted row as a library tile.
    // Absent in older sidecars — the reader defaults to listed.
    sidecar["listed"] = int(assetQuery.int64(8)) != 0;

    // MEMBERSHIP (G5). `parent` is what makes a Mesh row the inside of its
    // model and a baked map the inside of its material — the one relation
    // both browsers hide by — and it lived in no sidecar at all.
    const QString parent = dbqt::text(assetQuery, 9);
    if (!parent.isEmpty()) sidecar["parent"] = parent;

    // THE DB-ONLY PAYLOAD (G5 + the material spec's F12). A Material's, a
    // Sky's and a ParticleSystem's meaning is the `asset` blob, so a rebuild
    // without it restores a row that resolves to nothing. A material's
    // definition is ALSO a store object now (MATERIAL_BUNDLE_SPEC D-2) and so
    // rides the file manifest above; the blob is its cache, and carrying it
    // costs a few hundred bytes and closes the gap for the kinds that have no
    // file at all.
    {
        const QJsonDocument blob = QJsonDocument::fromJson(dbqt::bytes(assetQuery, 10));
        if (blob.isObject()) sidecar["asset"] = blob.object();
    }
    }

    // INTRINSIC DEPENDENCY EDGES (G5): the ones with NO project stamp — a
    // bundle's own membership, derived from its definition
    // (MaterialBundle::reconcileEdges) and true wherever the asset goes. A
    // project-stamped edge is that project's record of a USE and belongs to
    // the project, not to this asset.
    {
        QJsonArray edges;
        db::Statement depQuery(conn, "SELECT dependee, dependee_type FROM dependencies "
                                     "WHERE depender = ? AND project_guid IS NULL "
                                     "ORDER BY dependee");
        dbqt::bindText(depQuery, 1, guid);
        db::Error e;
        while (depQuery.step(&e)) {
            QJsonObject edge;
            edge["dependee"] = dbqt::text(depQuery, 0);
            edge["dependeeType"] = int(depQuery.int64(1));
            edges.append(edge);
        }
        // A SIDECAR WRITTEN SHORT IS WORSE THAN NONE (the rebuild trusts it): a failed read writes nothing.
        if (e) {
            if (errorOut) *errorOut = dbqt::describe(e);
            return false;
        }
        if (!edges.isEmpty()) sidecar["dependencies"] = edges;
    }

    QJsonArray files;
    {
        db::Statement filesQuery(conn, "SELECT AF.role, AF.oid, AF.name, F.size, F.ext "
                                       "FROM asset_files AF LEFT JOIN files F ON AF.oid = F.oid "
                                       "WHERE AF.asset_guid = ? ORDER BY AF.role, AF.name");
        dbqt::bindText(filesQuery, 1, guid);
        db::Error e;
        while (filesQuery.step(&e)) {
            QJsonObject file;
            file["role"] = dbqt::text(filesQuery, 0);
            file["oid"] = dbqt::text(filesQuery, 1);
            file["name"] = dbqt::text(filesQuery, 2);
            file["size"] = filesQuery.real(3);
            file["ext"] = dbqt::text(filesQuery, 4);
            files.append(file);
        }
        if (e) {
            if (errorOut) *errorOut = dbqt::describe(e);
            return false;
        }
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
    {
        db::Statement pinQuery(conn, "SELECT PA.project_guid, PA.oid_pin, F.size, F.ext "
                                     "FROM project_assets PA LEFT JOIN files F ON F.oid = PA.oid_pin "
                                     "WHERE PA.asset_guid = ? AND PA.oid_pin IS NOT NULL AND PA.oid_pin <> '' "
                                     "ORDER BY PA.project_guid");
        dbqt::bindText(pinQuery, 1, guid);
        db::Error e;
        while (pinQuery.step(&e)) {
            QJsonObject pin;
            pin["projectGuid"] = dbqt::text(pinQuery, 0);
            pin["oid"] = dbqt::text(pinQuery, 1);
            pin["size"] = pinQuery.real(2);
            pin["ext"] = dbqt::text(pinQuery, 3);
            pins.append(pin);
        }
        if (e) {
            if (errorOut) *errorOut = dbqt::describe(e);
            return false;
        }
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
    const QByteArray bytes = QJsonDocument(sidecar).toJson(QJsonDocument::Indented);
    std::string error;
    if (iris::files::writeAtomic(qtstd::toPath(path), qtstd::toView(bytes), iris::files::Durability::Derived, &error))
        return true;
    if (errorOut) *errorOut = qtstd::toQString(error);
    return false;
}

QString resolveFile(db::Connection &conn, const QString &root,
                    const QString &guid, const QString &name)
{
    // asset_files first (invariant I1)…
    db::Statement query(conn, "SELECT AF.oid, F.ext FROM asset_files AF "
                              "LEFT JOIN files F ON AF.oid = F.oid "
                              "WHERE AF.asset_guid = ? AND AF.name = ?");
    dbqt::bindText(query, 1, guid);
    dbqt::bindText(query, 2, name);
    db::Error e;
    if (query.step(&e)) {
        const QString path = AssetStorePaths::objectPathIn(root, dbqt::text(query, 0), dbqt::text(query, 1));
        if (QFileInfo::exists(path)) return path;
    }
    return QString();
}

bool writePin(db::Connection &conn, const QString &projectGuid,
              const QString &assetGuid, const QString &oid)
{
    db::Statement upsert(conn, "INSERT INTO project_assets (project_guid, asset_guid, oid_pin) "
                               "VALUES (?, ?, ?) "
                               "ON CONFLICT(project_guid, asset_guid) DO UPDATE SET oid_pin = excluded.oid_pin");
    dbqt::bindText(upsert, 1, projectGuid);
    dbqt::bindText(upsert, 2, assetGuid);
    dbqt::bindText(upsert, 3, oid);
    db::Error e;
    return upsert.run(&e);
}

bool ensurePin(db::Connection &conn, const QString &projectGuid,
               const QString &assetGuid, const QString &oid)
{
    db::Statement insert(conn, "INSERT INTO project_assets (project_guid, asset_guid, oid_pin) VALUES (?, ?, ?) "
                               "ON CONFLICT(project_guid, asset_guid) DO NOTHING");
    dbqt::bindText(insert, 1, projectGuid);
    dbqt::bindText(insert, 2, assetGuid);
    dbqt::bindText(insert, 3, oid);
    db::Error e;
    return insert.run(&e);
}

QString pinnedOid(db::Connection &conn, const QString &projectGuid,
                  const QString &assetGuid, bool *readOk)
{
    db::Statement query(conn, "SELECT oid_pin FROM project_assets WHERE project_guid = ? AND asset_guid = ?");
    dbqt::bindText(query, 1, projectGuid);
    dbqt::bindText(query, 2, assetGuid);
    db::Error e;
    if (query.step(&e)) return dbqt::text(query, 0);
    if (e && readOk) *readOk = false;   // a failed read is no "no pin"
    return QString();
}

QString resolvePinned(db::Connection &conn, const QString &root,
                      const QString &projectGuid, const QString &guid,
                      QString *nameOut, bool *readOk)
{
    // A PIN THAT COULD NOT BE READ RESOLVES NOTHING (NOQT-DATABASE-1 stage 2): the library's bytes are not the
    // project's version, and an edit started from them would be written over the project's own.
    bool read = true;
    const QString pin = pinnedOid(conn, projectGuid, guid, &read);
    if (!read) {
        if (readOk) *readOk = false;
        return QString();
    }
    if (!pin.isEmpty()) {
        db::Statement query(conn, "SELECT F.ext, AF.name FROM files F "
                                  "LEFT JOIN asset_files AF ON AF.oid = F.oid AND AF.asset_guid = ? "
                                  "WHERE F.oid = ?");
        dbqt::bindText(query, 1, guid);
        dbqt::bindText(query, 2, pin);
        db::Error e;
        if (query.step(&e)) {
            const QString path = AssetStorePaths::objectPathIn(root, pin, dbqt::text(query, 0));
            if (QFileInfo::exists(path)) {
                if (nameOut) *nameOut = dbqt::text(query, 1);
                return path;
            }
        }
        // ...AND ITS OBJECT's ROW UNREAD IS NO "PURGED" (B4's sweep): the library's bytes are not the project's.
        if (e) {
            if (readOk) *readOk = false;
            return QString();
        }
    }
    return resolveSource(conn, root, guid, nameOut, readOk);
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
    if (storeId.isEmpty()) storeId = QString::fromStdString(iris::newGuid());

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
    const QByteArray bytes = QJsonDocument(info).toJson(QJsonDocument::Indented);
    std::string error;
    if (iris::files::writeAtomic(qtstd::toPath(path), qtstd::toView(bytes), iris::files::Durability::Flush, &error))
        return true;
    if (errorOut) *errorOut = qtstd::toQString(error);
    return false;
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
