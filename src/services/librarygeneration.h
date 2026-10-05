/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef LIBRARYGENERATION_H
#define LIBRARYGENERATION_H

// THE LIBRARY GENERATION (FORWARD-ONLY-1). There are no migrations: a build
// reads exactly the library its own code writes. Two things can make a library
// on disk not this build's, and one check answers both at startup:
//
//   * the GENERATION — `PRAGMA user_version`, written at create
//     (CasSchema::kUserVersion). It is BUMPED whenever a build starts reading
//     stored data differently even though no column moved (FORWARD-ONLY-1
//     itself: graph blend names, library object blobs, bakes);
//   * the COLUMNS — Database::schemaMatchesFresh().
//
// A mismatch RESETS the library through the one reset (libraryreset::reset),
// KEEPING THE USER'S STORAGES (ASSETS-HOME-1; the owner: "a format bump rebuilds
// derived data from the stored sources; it never deletes the user's Assets or
// Materials"). The catalog is dropped and the Assets, Materials and Avatar
// storages are REBUILT from their own sidecars under this build's schema
// (AssetMigration::rebuildCatalog — never a read of the old tables); their
// bakes are dropped and re-derived from their sources, their thumbnails redrawn.
// Projects, the platform's seeds and the derived caches are cleared. A sidecar
// that records no home (a library older than generation 5) is not read, so
// such a library is cleared whole — once. Project folders filed outside the
// data root are left on disk — only unlisted. The reset REFUSES when another
// instance holds the data root (the `<db>.lock` QLockFile). The GUI shows a
// one-time notice that says what was cleared and what was kept.

#include <QString>

class SettingsManager;

namespace librarygeneration
{
enum class Outcome
{
    NoLibrary,   ///< no database file: a first launch
    Current,     ///< this build's generation and columns: nothing to do
    Wiped,       ///< an older library, wiped
    Refused,     ///< a NEWER library (never wiped), or an older one another instance holds
    Failed       ///< the wipe ran and failed (the app refuses to start on it)
};

struct Result
{
    Outcome outcome = Outcome::NoLibrary;
    int generationOnDisk = 0;   ///< PRAGMA user_version found (0 = none)
    QString reason;             ///< why it was wiped / refused / failed
    int keptRows = 0;           ///< storage rows rebuilt from their sidecars
    int unreadable = 0;         ///< sidecars of an older format, not read (cleared)
};

/// This build's generation (= CasSchema::userVersion()).
int generation();

/// THE CHECK, and the wipe it decides. `dbPath` is the library file;
/// `dataRoot` bounds what the wipe may delete (project folders outside it are
/// only unlisted). Latches the outcome for wipedAtStartup()/lastResult().
Result checkAndWipe(const QString &dbPath, const QString &dataRoot, SettingsManager *settings);

/// The outcome of this process's startup check (Outcome::NoLibrary before it ran).
Result lastResult();
bool wipedAtStartup();

/// The notice the GUI shows once after a reset: what was cleared, what was
/// kept, and that a later update keeps the user's storages.
QString noticeText();
QString noticeText(const Result &result);
/// The message the GUI shows before it refuses to start (Refused / Failed).
QString refusalText(const Result &result);
}

#endif // LIBRARYGENERATION_H
