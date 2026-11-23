/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

// THE RESET's CATALOG STEPS (4 to the stamp) and its first write, in a file of their own so db.failed_opens links the
// real order on a throwaway library and records what it DID (the wipe, the rebuild, the sweep, the stamp) - not a
// copy of its conditions. The file steps and the bootstrap's body are libraryreset.cpp's.

#include "services/libraryreset.h"

#include <QDir>

#include "data/database/database.h"
#include "services/assetgc.h"

namespace libraryreset {

QSet<assethome::Kind> Keep::homes() const
{
    QSet<assethome::Kind> out;
    if (assets) out << assethome::Kind::Assets << assethome::Kind::AvatarLibrary;
    if (materials) out << assethome::Kind::MaterialsLibrary;
    return out;
}

QString unfinishedText()
{
    return QStringLiteral("the reset did not finish: Jahshaka will close and finish it at the next start");
}

bool markPending(Database *db, Result &result)
{
    // DURABLE BEFORE ANYTHING IS REMOVED (the third confirm's finding 1): a script run's gesture batch is an open
    // transaction on this connection - a 0 written inside it would sit uncommitted while steps 2-3 delete, and roll
    // back with a failed batch. So the batch commits first (its answer read), and the 0 is its own committed unit.
    if (!db->commitBatchNow()) {
        result.error = QStringLiteral("the session's own library writes could not be committed (the log says why) - "
                                      "the reset did not start, nothing was removed");
        return false;
    }
    DbTransaction unit(db->connection());
    if (!db->clearGeneration() || !unit.commit()) {
        result.error = QStringLiteral("the library could not be marked for its reset (the log says why) - the reset "
                                      "did not start, nothing was removed");
        return false;
    }
    result.pending = true;
    return true;
}

void finishCatalog(Database *db, SettingsManager *settings, const QString &storeRoot, const Keep &keep,
                   bool seedPresets, Result &result)
{
    // ---- 4. THE CATALOG: DROP THE TABLES, AND CREATE THEM AGAIN, UNSTAMPED ----------
    // The generation is 0 since the reset's first write (markPending) and is stamped by step 6 alone, once every step
    // was whole: a reset that fails anywhere is read as not this build's at the next boot, which finishes it.
    if (!db->wipeDatabase() || !db->createAllTables(/*stampGeneration*/ false)) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("the library's tables could not be dropped and created again (the log says "
                                          "why)");
    } else {
        // ---- 4b. THE KEPT STORAGES, REBUILT FROM THEIR SIDECARS, UNSTAMPED ----------
        // Never from the old tables (forward-only): the content-addressed objects and their sidecars ARE the storage.
        // Then the collector takes what no kept row names, ONLY after a whole rebuild (keepAfterRebuild). A store with
        // NO sidecar folder holds no record to rebuild: nothing kept is no failure (a Clear Database that kept nothing
        // and was cut short is finished by the next boot, which keeps both storages).
        // ON THE CALLING (UI) THREAD, deliberately: the button's reset is followed by a restart and the bump's runs
        // before any window exists, so nothing is waiting on a frame while it works.
        if (keep.any() && !storeRoot.isEmpty() && QDir(QDir(storeRoot).filePath(QStringLiteral("sidecar"))).exists()) {
            // The rebuild writes through its OWN connection: the main one must hold no transaction (a script run's
            // gesture batch would hold the write lock) - the batch commits and stands down.
            db->commitBatchNow();
            AssetMigration::RebuildOptions options;
            options.homes = keep.homes();
            options.dropDerived = keep.dropDerived;
            options.stampGeneration = false;
            keepAfterRebuild(db, storeRoot, AssetMigration::rebuildCatalog(db->filePath(), storeRoot, options), options,
                             result);
        }

        // ---- 5. A REFUSED RESET SEEDS NOTHING (the second confirm's finding 1) ----------
        // After any failure the store stays exactly as the failure left it: primitives seeded into a catalog a
        // refused rebuild left empty would make the store's sweep take every kept sidecar as garbage.
        if (result.error.isEmpty()) bootstrap(db, settings, storeRoot, seedPresets, result);

        // ---- 6. ONLY A WHOLE RESET IS STAMPED THIS BUILD's ----------
        if (result.error.isEmpty()) {
            if (db->stampGeneration()) result.pending = false;
            else result.error = QStringLiteral("the library was reset, but its generation could not be stamped (the log "
                                               "says why)");
        }
    }
    // A FAILED RESET OVER A PENDING LIBRARY SAYS SO, in one sentence from one place (the third confirm's finding 2).
    if (!result.error.isEmpty() && result.pending)
        result.error = QStringLiteral("%1 - %2").arg(result.error, unfinishedText());
    result.ok = result.error.isEmpty();
}

void keepAfterRebuild(Database *db, const QString &storeRoot, const AssetMigration::RebuildReport &rebuilt,
                      const AssetMigration::RebuildOptions &options, Result &result)
{
    // ONLY AFTER A WHOLE REBUILD (sweepAllowedAfter): else the reset fails by name, the store as it is.
    if (!sweepAllowedAfter(rebuilt.ok, rebuilt.failedRows, rebuilt.committed)) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("the kept storages could not be rebuilt: %1 - the store was left as "
                                          "it is (nothing collected)").arg(rebuilt.error);
        return;
    }
    result.kept.rows = rebuilt.assets;
    result.kept.edges = rebuilt.edges;
    result.kept.unreadable = rebuilt.unreadable;
    result.kept.objects = AssetMigration::keptObjects(storeRoot, options).size();
    const AssetGc::Report collected = AssetGc::sweep(db->connection(), storeRoot, /*dryRun*/ false, /*force*/ true,
                                                     AssetGc::PendingReset::ThisReset);
    if (!collected.ok && result.error.isEmpty() && !collected.error.isEmpty())
        result.error = QStringLiteral("the store could not be collected: %1").arg(collected.error);
}

}   // namespace libraryreset
