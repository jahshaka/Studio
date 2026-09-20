/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIBRARYRESET_H
#define LIBRARYRESET_H

// "RESET THE LIBRARY" — the whole of it, in one place (owner review R10.2).
//
// WHAT WAS THERE BEFORE. Preferences → World → "Clear Database" called
// `Database::wipeDatabase()` — the TABLES, and nothing else — and then
// `qApp->quit()` followed by a detached spawn of `arguments()[0]` (the string
// the shell used to launch us, resolved against the NEW process's working
// directory: it comes back when the app was started by a path that still
// resolves there, and silently does not when it was not).
//
// Three things were wrong with what it left behind, all three measured on the
// owner's box (push #52, 2026-09-20):
//
//   * THE BYTES STAYED. The asset store (objects/, sidecars, derived caches,
//     the store's own identity), every project folder under the projects
//     root, and the abandoned staging temps all survived a "clear", so the
//     library reopened onto a store full of orphaned objects and a projects
//     folder full of scenes no row named any more.
//   * THE TABLES WERE NEVER CREATED AGAIN. `wipeDatabase` DROPs; nothing in
//     that path re-ran the create. The restarted app therefore ran its schema
//     check against a database with no tables in it, and logged exactly that:
//     "There was an error fetching db metadata No query", "080SchemaUpdate
//     query failed to execute!", "updateMetadataVersion query failed to
//     execute! Parameter count mismatch". (The tables did come back later —
//     `MainWindow::setupProjectDB` creates them — but only after the upgrader
//     had already failed against the empty file.)
//   * AND A SESSION THAT DID NOT RESTART carried on with no tables at all.
//
// THE RULE THIS IMPLEMENTS: a reset leaves the installation exactly as a
// FIRST LAUNCH finds it — an empty catalog with its tables created, an empty
// store with a fresh identity, no project folders, and the same first-run
// seed a first launch runs. Nothing else on the machine is touched: the store
// ROOT directory itself survives (the user may have chosen where it lives),
// the settings file survives (it is preferences, not library content), and a
// project the user filed somewhere else is removed by the location its own
// row records — never by guessing at a directory tree.
//
// It is UI-FREE on purpose. The one verb (`app.resetLibrary`) closes the open
// project through the shell and restarts the process; everything that touches
// rows and bytes is here, which is what lets the headless suite drive the
// whole of it.

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <functional>

class Database;
class SettingsManager;

namespace libraryreset {

/// What the reset removed. Counted BEFORE anything is deleted, so the numbers
/// describe the library that was there rather than what happened to be left.
struct Removed
{
    int objects = 0;      ///< stored objects under the store's objects/
    int sidecars = 0;     ///< sidecar/<guid>.json files
    int projects = 0;     ///< project FOLDERS removed (rows go with the tables)
    int thumbnails = 0;   ///< stored thumbnails (catalog BLOBS, not files)
    int staging = 0;      ///< abandoned `*.tmp-<pid>-<n>` staging temps

    QVariantMap toMap() const;
};

struct Result
{
    bool ok = false;
    QString error;        ///< empty when ok
    Removed removed;
};

/// Why a reset must not run right now, or empty when it may.
///
/// THE THREE WRITERS THAT OWN BYTES WE ARE ABOUT TO DELETE: a threaded import
/// (ImportBatchRunner — the Assets page's drop, the avatar import), the
/// first-run preset seed (which is an import), and the thumbnail sweep (which
/// writes rows for assets this call is about to drop). Each of them would
/// otherwise finish INTO a library that no longer exists, and the seed would
/// re-create half a catalog underneath the wipe.
///
/// AND THE FOURTH REASON, which is not about writers: an OFFLINE store. The
/// root is a user-chosen path and may be an unmounted drive; wiping the
/// catalog while the bytes are unreachable leaves the whole store orphaned on
/// that drive with nothing left that names it.
QString refusalReason();

/// THE RESET.
///
///   1. counts what is there (the numbers above);
///   2. removes every project FOLDER — each one resolved through
///      `folderForProject`, so a project the user filed on another drive is
///      taken by its recorded location, not by a guess — plus the leftovers
///      under `<projectsRoot>/Projects/` WHOSE NAME IS A GUID. That root comes
///      from the `default_directory` preference, which is a free-form folder
///      picker: a user who points it at ~/Documents must not lose what they
///      keep in ~/Documents/Projects. A folder this app did not name is not
///      this app's to delete, and is not counted;
///   3. removes THE STORE'S OWN LAYOUT inside the store root — `objects/`,
///      `sidecar/`, `derived/`, `store.json`, the staging temps beside them
///      and the legacy per-guid folders — and NOTHING ELSE in that directory.
///      Never the root itself and never a wildcard: the root is a path the
///      user chose (`assets/storeRoot` accepts any absolute directory, and
///      "Use Existing Store" with force accepts any existing one), so it can
///      be ~/Documents or the top of a memory stick, and every other file in
///      it belongs to somebody else;
///   4. drops the tables (`Database::wipeDatabase`) and CREATES THEM AGAIN —
///      with the metadata version row `createMetadataTable` writes, which is
///      what the NEXT BOOT's schema check reads and what the old path left it
///      without;
///   5. re-runs the fresh-install bootstrap: the store's identity
///      (`AssetStoreService::bootstrapFromSettings`, which writes a new
///      store.json) and the first-run preset seed under the same rule a
///      launch uses (a DRIVEN session — a suite, a script, an MCP client —
///      seeds nothing, exactly as `MainWindow` does not seed one).
///
/// `seedPresets` false skips step 5's seed: the caller is about to RESTART,
/// the child will seed at its own launch, and a seeder started in a process
/// that is already quitting either races the shutdown join or gives the store
/// two importers at once.
///
/// Nothing here follows a symlink: a project folder (or a store entry) that is
/// itself a link is unlinked, never walked — `QDir::removeRecursively` skips
/// links it meets inside a tree but follows one handed to it at the top, and
/// the target of that link is by definition somebody else's data.
///
/// The caller closes the open project first; this function never touches a
/// document, a window or the engine.
Result reset(Database *db, SettingsManager *settings, const QString &projectsRoot,
             const std::function<QString(const QString &)> &folderForProject,
             bool seedPresets = true);

}   // namespace libraryreset

#endif   // LIBRARYRESET_H
