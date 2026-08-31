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
#include <QStandardPaths>

#include "data/constants.h"
#include "data/database/database.h"
#include "data/settingsmanager.h"
#include "services/assetstorepaths.h"

const char *AssetStoreService::kSettingKey = "assets/storeRoot";

void AssetStoreService::bootstrapFromSettings(SettingsManager *settings)
{
    if (!settings) return;
    const QString root = settings->getValue(kSettingKey, QString()).toString();
    AssetStorePaths::setRootOverride(root);   // empty = default
}

bool AssetStoreService::online()
{
    return QDir(AssetStorePaths::root()).exists();
}

int AssetStoreService::missingCount(Database *db)
{
    if (!db || !online()) return 0;
    int missing = 0;
    for (const QString &guid : db->fetchLibraryAssetGuids()) {
        if (!QDir(AssetStorePaths::legacyFolder(guid)).exists()) ++missing;
    }
    return missing;
}

QVariantMap AssetStoreService::status(Database *db)
{
    QVariantMap out;
    out["root"] = AssetStorePaths::root();
    out["online"] = online();
    out["missing"] = missingCount(db);
    return out;
}

bool AssetStoreService::copyStoreContents(const QString &fromRoot, const QString &toRoot,
                                          QString *errorOut)
{
    // Copy everything under the current root into the new one, then verify
    // by relative-path presence + size (phase-1 verification; phase 2 adds
    // hashes). Existing identical-size files in the target are tolerated —
    // re-running an interrupted move completes it.
    QDir from(fromRoot);
    qint64 copied = 0;
    QDirIterator it(fromRoot, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString srcPath = it.next();
        const QString rel = from.relativeFilePath(srcPath);
        const QString dstPath = QDir(toRoot).filePath(rel);
        QDir().mkpath(QFileInfo(dstPath).absolutePath());

        if (QFileInfo::exists(dstPath)) {
            if (QFileInfo(dstPath).size() == QFileInfo(srcPath).size()) { ++copied; continue; }
            QFile::remove(dstPath);   // partial from an interrupted move
        }
        if (!QFile::copy(srcPath, dstPath)) {
            if (errorOut) *errorOut = QStringLiteral("copy failed: %1 -> %2").arg(srcPath, dstPath);
            return false;
        }
        ++copied;
    }

    // Verification pass: every source file present at the destination with
    // the same size.
    QDirIterator vit(fromRoot, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (vit.hasNext()) {
        const QString srcPath = vit.next();
        const QString dstPath = QDir(toRoot).filePath(from.relativeFilePath(srcPath));
        if (!QFileInfo::exists(dstPath) || QFileInfo(dstPath).size() != QFileInfo(srcPath).size()) {
            if (errorOut) *errorOut = QStringLiteral("verification failed for %1").arg(dstPath);
            return false;
        }
    }
    Q_UNUSED(copied)
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
        if (!force && db) {
            // Sanity check against the catalog: when the library knows
            // store-backed assets, at least one must be present there.
            const QStringList guids = db->fetchLibraryAssetGuids();
            int present = 0, expected = 0;
            for (const QString &guid : guids) {
                if (QDir(AssetStorePaths::legacyFolderIn(oldRoot, guid)).exists()) ++expected;
                if (QDir(AssetStorePaths::legacyFolderIn(newRoot, guid)).exists()) ++present;
            }
            if (expected > 0 && present == 0) {
                if (errorOut) *errorOut = QStringLiteral(
                    "%1 does not look like this library's asset store (none of %2 known asset folders found); pass force to use it anyway")
                    .arg(newRoot).arg(expected);
                return false;
            }
        }
    }

    if (settings) settings->setValue(kSettingKey, newRoot);
    AssetStorePaths::setRootOverride(newRoot);
    return true;
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
