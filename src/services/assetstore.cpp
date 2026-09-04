/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "services/assetstore.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLockFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/settingsmanager.h"
#include "irisgl/core/logger.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include "services/filewriteatomic.h"

const char *AssetStoreService::kSettingKey = "assets/storeRoot";
const char *AssetStoreService::kStoreIdKey = "assets/storeId";

void AssetStoreService::bootstrapFromSettings(SettingsManager *settings)
{
    if (!settings) return;
    const QString root = settings->getValue(kSettingKey, QString()).toString();
    AssetStorePaths::setRootOverride(root);   // empty = default

    // store.json is written here and nowhere else at startup, so every root
    // this build owns identifies itself (it had NO production caller before —
    // deep audit 2026-09, area 6). Only for a root that already exists: an
    // OFFLINE store must stay offline, not be recreated as an empty local
    // directory by the mere act of launching.
    const QString active = AssetStorePaths::root();
    if (!QDir(active).exists()) return;

    QString error;
    AssetCas::writeStoreInfo(active, &error);

    // And the id is now load-bearing: a root that has been swapped underneath
    // us (a different store mounted at the same path) is a real accident, and
    // this is the only place it is cheap to notice.
    QString storeId;
    int formatVersion = 0;
    if (!AssetCas::readStoreInfo(active, &storeId, &formatVersion)) return;
    const QString remembered = settings->getValue(kStoreIdKey, QString()).toString();
    if (!remembered.isEmpty() && !storeId.isEmpty() && remembered != storeId) {
        iris::Logger::getSingleton()->warn(
            QString("asset store: %1 now holds a DIFFERENT store (%2, was %3) — the library's "
                    "rows may not describe the files under it")
                .arg(active, storeId, remembered));
    }
    settings->setValue(kStoreIdKey, storeId);
}

bool AssetStoreService::online()
{
    return QDir(AssetStorePaths::root()).exists();
}

int AssetStoreService::missingCount(Database *db)
{
    if (!db || !online()) return 0;
    const QString root = AssetStorePaths::root();
    QSqlDatabase conn = QSqlDatabase::database();
    if (!conn.isOpen()) return 0;

    // Keyed on the CAS, not on the retired per-guid view (deep audit 2026-09,
    // area 6): an asset is "missing from this store" when the catalog records
    // bytes for it and NONE of those objects are on disk. A row with no
    // asset_files at all (a DB-only asset — a material definition, a folder
    // row) never had files and is not missing.
    int missing = 0;
    for (const QString &guid : db->fetchLibraryAssetGuids()) {
        QSqlQuery q(conn);
        q.prepare("SELECT AF.oid, F.ext FROM asset_files AF "
                  "LEFT JOIN files F ON AF.oid = F.oid WHERE AF.asset_guid = ?");
        q.addBindValue(guid);
        if (!q.exec()) continue;

        bool recorded = false, present = false;
        while (q.next()) {
            recorded = true;
            if (QFileInfo::exists(AssetStorePaths::objectPathIn(
                    root, q.value(0).toString(), q.value(1).toString()))) {
                present = true;
                break;
            }
        }
        if (recorded && !present) ++missing;
    }
    return missing;
}

QVariantMap AssetStoreService::status(Database *db)
{
    QVariantMap out;
    const QString root = AssetStorePaths::root();
    out["root"] = root;
    out["online"] = online();
    out["missing"] = missingCount(db);

    QString storeId;
    int formatVersion = 0;
    if (online() && AssetCas::readStoreInfo(root, &storeId, &formatVersion)) {
        out["storeId"] = storeId;
        out["formatVersion"] = formatVersion;
    }
    return out;
}

namespace
{
// A staging temp from an interrupted write — never part of the store's
// content, so it is not copied and not verified (it would otherwise travel to
// every new root forever, and its absence at the destination would fail the
// verification pass).
bool isStagingTemp(const QString &fileName)
{
    return fileName.contains(QLatin1String(".tmp-"));
}

// Non-object artifacts (sidecars, store.json, a surviving legacy folder) get
// the SAME staged-temp-then-rename tail objects get: a move interrupted
// halfway must leave the destination with whole files or no file, never a
// truncated one that later passes an existence check.
bool copyFileAtomic(const QString &srcPath, const QString &dstPath, QString *errorOut)
{
    if (!QDir().mkpath(QFileInfo(dstPath).absolutePath())) {
        if (errorOut) *errorOut = QStringLiteral("cannot create %1").arg(QFileInfo(dstPath).absolutePath());
        return false;
    }
    const QString tmpPath = FileWrite::stagingTempPath(dstPath);
    QFile::remove(tmpPath);
    if (!QFile::copy(srcPath, tmpPath)) {
        QFile::remove(tmpPath);
        if (errorOut) *errorOut = QStringLiteral("copy failed: %1 -> %2").arg(srcPath, dstPath);
        return false;
    }
    FileWrite::fsyncPath(tmpPath);
    return FileWrite::atomicRename(tmpPath, dstPath, errorOut);
}
} // namespace

bool AssetStoreService::copyStoreContents(const QString &fromRoot, const QString &toRoot,
                                          QString *errorOut)
{
    // Copy everything under the current root into the new one, then verify
    // by relative-path presence + size. Existing identical-size files in the
    // target are tolerated — re-running an interrupted move completes it.
    //
    // Content-addressed objects go through AssetCas::storeObject (deep audit
    // 2026-09, area 6): it is atomic, it DEDUPS against whatever the target
    // root already holds, and it hardlinks when source and target share a
    // filesystem. A raw QFile::copy straight to the final name was the exact
    // torn-write pattern storeObject exists to prevent, on the one operation
    // that touches every byte in the library at once.
    const QDir from(fromRoot);
    const QString objectsPrefix = QStringLiteral("objects/");

    QDirIterator it(fromRoot, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString srcPath = it.next();
        const QFileInfo srcInfo = it.fileInfo();
        if (isStagingTemp(srcInfo.fileName())) continue;

        const QString rel = from.relativeFilePath(srcPath);
        if (rel.startsWith(objectsPrefix)) {
            // objects/<xx>/<oid>[.<ext>] — the name IS the content id.
            const QString oid = srcInfo.completeBaseName().toLower();
            if (oid.length() == 64) {
                if (!AssetCas::storeObject(srcPath, toRoot, oid, srcInfo.suffix().toLower(), errorOut))
                    return false;
                continue;
            }
            // Not an object name; fall through and copy it verbatim.
        }

        const QString dstPath = QDir(toRoot).filePath(rel);
        if (QFileInfo::exists(dstPath)
            && QFileInfo(dstPath).size() == srcInfo.size()) continue;
        if (!copyFileAtomic(srcPath, dstPath, errorOut)) return false;
    }

    // Verification pass: every source file present at the destination with
    // the same size.
    QDirIterator vit(fromRoot, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (vit.hasNext()) {
        const QString srcPath = vit.next();
        if (isStagingTemp(vit.fileInfo().fileName())) continue;
        const QString dstPath = QDir(toRoot).filePath(from.relativeFilePath(srcPath));
        if (!QFileInfo::exists(dstPath) || QFileInfo(dstPath).size() != QFileInfo(srcPath).size()) {
            if (errorOut) *errorOut = QStringLiteral("verification failed for %1").arg(dstPath);
            return false;
        }
    }
    return true;
}

bool AssetStoreService::setRoot(const QString &path, bool moveContents, bool force,
                                SettingsManager *settings, Database *db, QString *errorOut)
{
    const QString oldRoot = AssetStorePaths::root();

    // Empty = back to the default root. Files are NOT moved back — the
    // default root still holds whatever it held (moves always copy-retain).
    if (path.isEmpty()) {
        if (settings) settings->setValue(kSettingKey, QString());
        AssetStorePaths::setRootOverride(QString());
        adoptStoreIdentity(settings);
        return true;
    }

    const QString newRoot = QDir::cleanPath(QDir::fromNativeSeparators(path));
    if (!QDir::isAbsolutePath(newRoot)) {
        if (errorOut) *errorOut = QStringLiteral("store root must be an absolute path");
        return false;
    }
    if (newRoot == oldRoot) {
        if (settings) settings->setValue(kSettingKey,
            newRoot == AssetStorePaths::defaultRoot() ? QString() : newRoot);
        adoptStoreIdentity(settings);
        return true;
    }
    // Nesting either root inside the other makes the copy recurse into
    // itself / the fallback resolution ambiguous — refuse.
    if ((newRoot + "/").startsWith(oldRoot + "/") || (oldRoot + "/").startsWith(newRoot + "/")) {
        if (errorOut) *errorOut = QStringLiteral("new root must not nest inside the current root (or vice versa)");
        return false;
    }

    if (moveContents) {
        // Move Store: the current store must be reachable to copy from.
        if (!QDir(oldRoot).exists()) {
            if (errorOut) *errorOut = QStringLiteral("current store is offline: %1").arg(oldRoot);
            return false;
        }
        if (!QDir().mkpath(newRoot)) {
            if (errorOut) *errorOut = QStringLiteral("cannot create %1").arg(newRoot);
            return false;
        }
        if (!copyStoreContents(oldRoot, newRoot, errorOut)) return false;
        // The old tree is deliberately RETAINED (rollback stays trivial;
        // deletion is a separate, explicit affordance).
    }
    else {
        // Use Existing Store: point at a root that already contains one.
        if (!QDir(newRoot).exists()) {
            if (errorOut) *errorOut = QStringLiteral("no such directory: %1").arg(newRoot);
            return false;
        }
        // store.json is the identity anchor it was always documented to be
        // (deep audit 2026-09, area 6: writeStoreInfo had no production caller
        // and setRoot never read it). A root written by a NEWER Jahshaka has a
        // layout this build does not know how to read — adopting it silently
        // is how a downgrade corrupts a store.
        {
            QString storeId;
            int formatVersion = 0;
            if (AssetCas::readStoreInfo(newRoot, &storeId, &formatVersion)
                && formatVersion > AssetCas::kStoreFormatVersion) {
                if (errorOut) *errorOut = QStringLiteral(
                    "%1 was written by a newer version of Jahshaka (store format %2, this build "
                    "reads %3)").arg(newRoot).arg(formatVersion).arg(AssetCas::kStoreFormatVersion);
                return false;   // NOT force-able: this one is not a guess
            }
        }

        if (!force && db) {
            // Sanity check against the catalog, keyed on the CAS rather than
            // on the retired per-guid view: when the library knows content,
            // at least one of its objects must be present there.
            QSqlDatabase conn = QSqlDatabase::database();
            int present = 0, expected = 0;
            if (conn.isOpen()) {
                QSqlQuery q(conn);
                if (q.exec("SELECT oid, ext FROM files")) {
                    while (q.next()) {
                        ++expected;
                        if (QFileInfo::exists(AssetStorePaths::objectPathIn(
                                newRoot, q.value(0).toString(), q.value(1).toString())))
                            ++present;
                    }
                }
            }
            if (expected > 0 && present == 0) {
                if (errorOut) *errorOut = QStringLiteral(
                    "%1 does not look like this library's asset store (none of %2 known objects found); pass force to use it anyway")
                    .arg(newRoot).arg(expected);
                return false;
            }
        }
    }

    if (settings) settings->setValue(kSettingKey, newRoot);
    AssetStorePaths::setRootOverride(newRoot);
    adoptStoreIdentity(settings);
    return true;
}

void AssetStoreService::adoptStoreIdentity(SettingsManager *settings)
{
    // Every root this build owns gets (or keeps) its store.json, and the
    // adopted id is remembered so bootstrapFromSettings can notice a store
    // swapped underneath the same path.
    const QString root = AssetStorePaths::root();
    if (!QDir(root).exists()) return;
    QString error;
    AssetCas::writeStoreInfo(root, &error);
    if (!settings) return;
    QString storeId;
    if (AssetCas::readStoreInfo(root, &storeId, nullptr) && !storeId.isEmpty())
        settings->setValue(kStoreIdKey, storeId);
}

// ---------------------------------------------------------------------------

namespace {
QLockFile *sLibraryLock = nullptr;

QString lockPathFor(const QString &dbPath)
{
    const QString path = dbPath.isEmpty()
        ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
              .filePath(Constants::JAH_DATABASE)
        : dbPath;
    return path + ".lock";
}
} // namespace

bool LibraryLock::acquire(const QString &dbPath)
{
    if (sLibraryLock) return true;   // already held by this process
    auto *lock = new QLockFile(lockPathFor(dbPath));
    lock->setStaleLockTime(0);       // PID-liveness only; a crashed instance's lock is reclaimed
    if (!lock->tryLock(0)) {
        delete lock;
        return false;
    }
    sLibraryLock = lock;
    return true;
}

bool LibraryLock::heldElsewhere(const QString &dbPath)
{
    // A lock this process holds is not "elsewhere" — in-app tools coordinate
    // with their own instance; the refusal targets OTHER running instances.
    if (sLibraryLock && sLibraryLock->fileName() == lockPathFor(dbPath)) return false;

    QLockFile probe(lockPathFor(dbPath));
    probe.setStaleLockTime(0);
    if (probe.tryLock(0)) {
        probe.unlock();
        return false;
    }
    return true;
}

void LibraryLock::release()
{
    if (!sLibraryLock) return;
    sLibraryLock->unlock();
    delete sLibraryLock;
    sLibraryLock = nullptr;
}
