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
// A mismatch WIPES the library through the one reset (libraryreset::reset):
// catalog, the store's own layout, and the project folders INSIDE the data root.
// Project folders filed outside the data root (a custom location, a preference
// pointing elsewhere) are left on disk — only unlisted. The wipe REFUSES when
// another instance holds the data root (the `<db>.lock` QLockFile): it would
// delete that instance's files under it. The GUI shows a one-time notice.

#include <QString>

class SettingsManager;

namespace librarygeneration
{
enum class Outcome
{
    NoLibrary,   ///< no database file: a first launch
    Current,     ///< this build's generation and columns: nothing to do
    Wiped,       ///< an older library, wiped
    Refused,     ///< an older library another instance holds (or a reset refusal)
    Failed       ///< the wipe ran and failed
};

struct Result
{
    Outcome outcome = Outcome::NoLibrary;
    int generationOnDisk = 0;   ///< PRAGMA user_version found (0 = none)
    QString reason;             ///< why it was wiped / refused / failed
};

/// This build's generation (= CasSchema::kUserVersion).
int generation();

/// THE CHECK, and the wipe it decides. `dbPath` is the library file;
/// `dataRoot` bounds what the wipe may delete (project folders outside it are
/// only unlisted). Latches the outcome for wipedAtStartup()/lastResult().
Result checkAndWipe(const QString &dbPath, const QString &dataRoot, SettingsManager *settings);

/// The outcome of this process's startup check (Outcome::NoLibrary before it ran).
Result lastResult();
bool wipedAtStartup();

/// The notice the GUI shows once after a wipe.
QString noticeText();
}

#endif // LIBRARYGENERATION_H
