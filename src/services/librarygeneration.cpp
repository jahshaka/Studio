/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/librarygeneration.h"

#include <QDir>
#include <QFile>
#include <QSqlQuery>

#include "data/constants.h"
#include "data/database/casschema.h"
#include "data/database/database.h"
#include "data/settingkeys.h"
#include "data/settingsmanager.h"
#include "irisgl/core/logger.h"
#include "services/apppaths.h"
#include "services/assetstore.h"
#include "services/libraryreset.h"

namespace librarygeneration
{
namespace {
Result sLast;

bool isInside(const QString &path, const QString &root)
{
    if (path.isEmpty() || root.isEmpty()) return false;
    const QString p = QDir::cleanPath(QDir(path).absolutePath());
    const QString r = QDir::cleanPath(QDir(root).absolutePath());
    return p == r || p.startsWith(r + QLatin1Char('/'));
}
}   // namespace

int generation() { return CasSchema::userVersion(); }

Result checkAndWipe(const QString &dbPath, const QString &dataRoot, SettingsManager *settings)
{
    Result result;
    if (!QFile::exists(dbPath)) { sLast = result; return result; }

    Database library;
    if (!library.initializeDatabase(dbPath)) {
        result.outcome = Outcome::Current;   // nothing readable to judge: never wipe on a guess
        sLast = result;
        return result;
    }
    {
        QSqlQuery version(library.getDb());
        if (version.exec(QStringLiteral("PRAGMA user_version")) && version.next())
            result.generationOnDisk = version.value(0).toInt();
    }
    const bool columnsMatch = library.schemaMatchesFresh();
    if (result.generationOnDisk == generation() && columnsMatch) {
        result.outcome = Outcome::Current;
        library.closeDatabase();
        sLast = result;
        return result;
    }
    // A NEWER library is never wiped (D6): an older build started over a newer
    // build's data root must not destroy it.
    if (result.generationOnDisk > generation()) {
        result.outcome = Outcome::Refused;
        result.reason = QStringLiteral("This library was written by a newer build of Jahshaka "
                                       "(library generation %1; this build reads %2). Use the "
                                       "newer build, or another data folder.")
                            .arg(result.generationOnDisk).arg(generation());
        library.closeDatabase();
        sLast = result;
        return result;
    }
    result.reason = columnsMatch
        ? QStringLiteral("library generation %1, this build reads %2")
              .arg(result.generationOnDisk).arg(generation())
        : QStringLiteral("the library's tables are not this build's");

    // THE DATA ROOT MUST BE OURS: another instance holding it would have its
    // store objects and project folders deleted out from under it.
    if (!LibraryLock::acquire(dbPath)) {
        result.outcome = Outcome::Refused;
        result.reason = QStringLiteral("another Jahshaka instance is using this data root (%1) — "
                                       "close it and start again; the library (%2) must be reset "
                                       "and cannot be while it is open")
                            .arg(dataRoot, result.reason);
        library.closeDatabase();
        sLast = result;
        return result;
    }
    const QString refusal = libraryreset::refusalReason();
    if (!refusal.isEmpty()) {
        result.outcome = Outcome::Refused;
        result.reason = refusal;
        library.closeDatabase();
        sLast = result;
        return result;
    }

    // Project folders are removed only INSIDE the data root: a preference or a
    // project `location` pointing elsewhere keeps its folders (only unlisted).
    const QString projectsRoot = settings
        ? AppPaths::projectsRoot(settings->get(settingkeys::defaultDirectory),
                                 Constants::PROJECT_FOLDER)
        : QString();
    const QString root = dataRoot;
    // THE USER'S STORAGES ARE KEPT ACROSS A BUMP, their derived files re-derived.
    libraryreset::Keep keep;
    keep.assets = true;
    keep.materials = true;
    keep.dropDerived = true;
    const libraryreset::Result wiped = libraryreset::reset(
        &library, settings, isInside(projectsRoot, root) ? projectsRoot : QString(),
        [&library, root](const QString &guid) -> QString {
            // The folder a project row records, only when it is inside the data root.
            const QString location = library.projectLocation(guid);
            if (location.isEmpty() || !isInside(location, root)) return QString();
            return QDir(location).filePath(QStringLiteral("Projects/") + guid);
        },
        /*seedPresets*/ false, keep);
    library.closeDatabase();
    result.outcome = wiped.ok ? Outcome::Wiped : Outcome::Failed;
    result.keptRows = wiped.kept.rows;
    result.unreadable = wiped.kept.unreadable;
    if (!wiped.ok) result.reason = wiped.error;
    irisLog(wiped.ok ? QStringLiteral("library: RESET (%1) — no migrations exist; %2 storage rows "
                                      "rebuilt from their sidecars, %3 sidecars of an older format "
                                      "not read")
                           .arg(result.reason).arg(wiped.kept.rows).arg(wiped.kept.unreadable)
                     : QStringLiteral("library: the reset failed: %1").arg(wiped.error));
    sLast = result;
    return result;
}

Result lastResult() { return sLast; }
bool wipedAtStartup() { return sLast.outcome == Outcome::Wiped; }

QString refusalText(const Result &result)
{
    if (result.outcome == Outcome::Failed)
        return QStringLiteral("Your library from the previous build could not be reset, so "
                              "Jahshaka cannot start on it:\n%1").arg(result.reason);
    return QStringLiteral("Jahshaka cannot open this library:\n%1").arg(result.reason);
}

QString noticeText(const Result &result)
{
    // ONE LINE FOR WHAT HAPPENED TO THE USER'S STORAGES (the lead, 2026-10-05):
    // what was cleared, and that a later update keeps them.
    const QString storages =
        (result.keptRows == 0 && result.unreadable > 0)
            ? QStringLiteral("Your Assets and Materials from the previous build were cleared too: "
                             "that build did not record where they belong. From this build on, an "
                             "update keeps your Assets, Materials and Avatars.")
            : QStringLiteral("Your Assets, Materials and Avatars were kept (%1 items rebuilt); "
                             "their bakes rebuild in the background, and Assets > Library > "
                             "Rebuild missing thumbnails redraws their tiles. Later updates keep "
                             "them the same way.").arg(result.keptRows);
    return QStringLiteral("Your library was updated for this build: projects from the previous "
                          "build were removed (project folders stored outside the app's data "
                          "folder were left on disk, no longer listed). ") + storages;
}

QString noticeText() { return noticeText(sLast); }
}   // namespace librarygeneration
