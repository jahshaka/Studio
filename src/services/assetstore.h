/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef ASSETSTORESERVICE_H
#define ASSETSTORESERVICE_H

// Relocatable asset-store root (ASSET_PIPELINE_SPEC §3.1.1/§3.1.2, phase 1).
//
// The "assets/storeRoot" setting points the whole AssetStore at any
// drive/RAID; an absent setting means the default AppData root — exactly the
// historical behavior, zero migration for existing users. Identity survives
// absence (git-annex's lesson): an unreachable root opens the library
// normally (thumbnails/metadata/drawers are DB blobs) with byte-needing
// actions disabled — nothing is deleted, no rows are "healed" away.
//
// AssetStorePaths stays pure path math; THIS is the behavior layer the
// assets.storeRoot/setStoreRoot/storeStatus verbs and the Preferences →
// Assets page both call (API-first: one implementation).

#include <QString>
#include <QVariantMap>

class Database;
class SettingsManager;

class AssetStoreService
{
public:
    static const char *kSettingKey;   // "assets/storeRoot"
    static const char *kStoreIdKey;   // "assets/storeId" — the adopted store's identity

    /// Startup bootstrap: read the setting and point AssetStorePaths at it.
    /// Called from main() BEFORE anything derives a store path. Never creates
    /// a custom root's directory — a missing custom root means OFFLINE, not
    /// "make an empty store on the dead mount point".
    static void bootstrapFromSettings(SettingsManager *settings);

    /// Is the active root reachable right now?
    static bool online();

    /// {root, online, missing, storeId?, formatVersion?} — missing = library
    /// rows (view_filter 2 AND 3, preflight §1.6) that RECORD stored bytes
    /// (asset_files) and have none of those objects under the active root.
    /// Rows with no stored bytes exist legitimately (node-JSON-only assets)
    /// and are not missing; offline reporting keys on `online`, not on
    /// missing alone. storeId/formatVersion come from the root's store.json
    /// when it has one.
    static QVariantMap status(Database *db);

    /// Change the store root. Empty path = back to the default AppData root.
    /// moveContents: copy the current store's contents into the new root
    /// first (verify by count+size — phase-1 verification level), flip the
    /// setting only after the copy verifies; the OLD tree is retained (the
    /// UI may offer deletion separately — never automatic).
    /// !moveContents: "Use Existing Store" — the target must exist and, when
    /// the catalog knows store-backed assets, contain at least one of them
    /// (sanity check; {force} in the options map of the verb bypasses it).
    /// Returns false with errorOut set on any failure; on failure nothing
    /// changed (setting, override and files are untouched).
    static bool setRoot(const QString &path, bool moveContents, bool force,
                        SettingsManager *settings, Database *db, QString *errorOut);

private:
    static int missingCount(Database *db);
    static bool copyStoreContents(const QString &fromRoot, const QString &toRoot,
                                  QString *errorOut);
    /// Write (or keep) store.json at the ACTIVE root and remember its id.
    static void adoptStoreIdentity(SettingsManager *settings);
};

/// One QLockFile beside JahLibrary.db (preflight §6.2 / amendment 4): the app
/// holds it for its lifetime; the phase-2 migration/verify tools REFUSE to
/// run while any instance holds it ("close Jahshaka first" instead of a
/// corrupted library). Acquisition is non-blocking and non-fatal — a second
/// app instance simply runs without the lock, exactly as before.
class LibraryLock
{
public:
    /// Try to take the lock beside the given database file (or the default
    /// AppData JahLibrary.db when empty). Returns whether THIS process holds it.
    static bool acquire(const QString &dbPath = QString());

    /// Is some OTHER process holding the lock beside this database file?
    /// (For migration tools: refuse while true.)
    static bool heldElsewhere(const QString &dbPath);

    static void release();
};

#endif // ASSETSTORESERVICE_H
