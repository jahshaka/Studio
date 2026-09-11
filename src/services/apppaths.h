/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef APPPATHS_H
#define APPPATHS_H

#include <QCoreApplication>
#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QString>

/**
 * THE single authority for "where this run keeps its data"
 * (SPECS/WINDOWS_BUILD_SPEC.md §6.2 W9, SPECS/ENGINEERING_DEBT_SPEC.md
 * ADDENDUM 6).
 *
 * Before this existed the answer was spelled out inline at ~18 call sites as
 * `QStandardPaths::writableLocation(AppDataLocation)`, and the settings file
 * did not follow it at all: under QT_DEBUG `jahsettings.ini` lives beside the
 * binary (`applicationDirPath()`), which is SHARED between every run of a
 * build tree. That is how an automated suite came to rewrite the developer's
 * `[assets] storeId` and geometry in `build-linux/bin/jahsettings.ini` — a
 * scratch `HOME` moves the library DB and the asset store but cannot move a
 * path derived from the executable's location.
 *
 * THE OVERRIDE, in precedence order:
 *
 *   1. `--data-root <dir>` on the command line (CliOptions::dataRoot)
 *   2. the `JAHSHAKA_DATA_ROOT` environment variable
 *   3. nothing — every path stays EXACTLY where it is today
 *
 * (3) is the important one: with no override this class is a pure rename of
 * the expression it replaced, so no existing user's data moves and no path in
 * a release build changes. The override is what makes a test run, a CI run or
 * a second developer profile hermetic — SETTINGS INCLUDED, which is the part
 * `HOME=` could never buy.
 *
 * Resolved ONCE, in main(), before the Upgrader, before SettingsManager and
 * before AssetStoreService::bootstrapFromSettings — that ordering is already
 * load-bearing for the store root (src/app/main.cpp) and this must not
 * disturb it. `initialize()` is idempotent and cheap; anything that reads a
 * path before it runs gets the un-overridden answer, which is why the call
 * sits as early as it possibly can.
 */
// HEADER-ONLY, deliberately. Eighteen call sites across src/ read the data
// root, and they are compiled into roughly twenty test targets that each list
// their own sources; a .cpp here would mean adding one more line to every one
// of those targets, and a link error for whoever forgets. The state is a
// function-local static in an inline function, which the standard guarantees is
// ONE object across every translation unit.

namespace AppPaths {

namespace detail {
struct State {
    QString root;
    bool    overridden = false;
};
inline State &state() { static State s; return s; }

inline QString cleaned(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}
}   // namespace detail

/// Resolve the data root for this process. `cliOverride` is the parsed
/// `--data-root` value ("" when the flag was not given). Creates the
/// directory when an override is in force, so every later join can assume it
/// exists exactly as AppDataLocation does.
inline void initialize(const QString &cliOverride = QString())
{
    detail::State &s = detail::state();
    // The CLI wins over the environment, and the environment over nothing;
    // whitespace-only values are treated as absent so an empty variable in a
    // shell profile cannot silently relocate a user's library.
    QString chosen = cliOverride.trimmed();
    if (chosen.isEmpty()) {
        chosen = QProcessEnvironment::systemEnvironment()
                     .value(QStringLiteral("JAHSHAKA_DATA_ROOT"))
                     .trimmed();
    }

    if (chosen.isEmpty()) {
        s.overridden = false;
        s.root = detail::cleaned(
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
        return;
    }

    s.overridden = true;
    // Relative roots resolve against the CURRENT WORKING DIRECTORY, not against
    // the executable: a suite that sets JAHSHAKA_DATA_ROOT=data with its own
    // WORKING_DIRECTORY means "beside my scratch", which is the only reading
    // that makes a relative value useful at all.
    s.root = detail::cleaned(QDir::current().absoluteFilePath(chosen));
    // Created eagerly: every caller joins onto it and then opens a file, and
    // QStandardPaths creates AppDataLocation for them.
    QDir().mkpath(s.root);
}

/// Where this run keeps the library database, the asset store, the shader
/// cache and (when overridden) the settings file. Never empty.
inline QString dataRoot()
{
    // Un-initialised (a unit test that links a path-reading TU without a
    // main(), an early static reader) answers exactly what the inline
    // expression this replaced answered. Never empty, never a surprise.
    const detail::State &s = detail::state();
    if (s.root.isEmpty())
        return detail::cleaned(
            QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    return s.root;
}

/// True when `--data-root` or `JAHSHAKA_DATA_ROOT` chose the root. The
/// settings file follows the root ONLY in this case — an un-overridden run
/// keeps its historical location (applicationDirPath under QT_DEBUG,
/// AppDataLocation otherwise) so nobody's preferences move.
inline bool isOverridden() { return detail::state().overridden; }

/// THE PROJECTS ROOT for this run — the folder that holds the `Projects/`
/// directory every project's own folder is created under.
///
/// It follows the data root when one is forced, and that is the whole point
/// (lighting-audit item 15, smoke 2026-09-11): `--data-root` moved the library
/// database, the asset store, the shader cache and the settings file, but NOT
/// the projects, so every scripted run, every app-spawning suite and every
/// agent on the rig created project folders in the DEVELOPER'S
/// `~/Documents/Jahshaka/Projects` — nineteen empty ones in a single night.
/// A hermetic run has to be hermetic on disk as well as in the database.
///
/// `configuredDirectory` is the `default_directory` preference (empty when
/// unset) and `documentsSubFolder` is the historical Documents child
/// (`Constants::PROJECT_FOLDER`) — both passed IN, because this header is
/// included by ~20 test targets and must stay free of Studio's constants and
/// its settings manager. With no override the answer is exactly what every
/// call site computed inline before: the preference, else Documents.
inline QString projectsRoot(const QString &configuredDirectory,
                            const QString &documentsSubFolder)
{
    // The override WINS over the preference: a `default_directory` carried in
    // by a copied settings file would otherwise write a sandboxed run's
    // projects straight back into the user's Documents, which is the defect.
    if (isOverridden()) return dataRoot();
    const QString configured = configuredDirectory.trimmed();
    if (!configured.isEmpty()) return detail::cleaned(configured);
    return detail::cleaned(
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + documentsSubFolder);
}

/// The settings file for this run: `<dataRoot>/jahsettings.ini` when
/// overridden, otherwise the historical location. SettingsManager is the only
/// caller; it lives here so "the override redirects settings AND data
/// together" is one statement in one place.
inline QString settingsFilePath(const QString &fileName)
{
    // THE OVERRIDDEN CASE. This is the whole reason the override covers
    // settings and not only data: under QT_DEBUG the settings file lives beside
    // the BINARY, so a scratch HOME (or any AppDataLocation redirect) leaves
    // every run of a build tree writing the same jahsettings.ini — the shared
    // file an automated suite rewrote out from under the owner
    // (ENGINEERING_DEBT_SPEC ADDENDUM 6).
    if (isOverridden())
        return QDir::cleanPath(QDir(dataRoot()).filePath(fileName));

    // THE HISTORICAL CASE, unchanged and deliberately spelled out the way
    // SettingsManager used to spell it: applicationDirPath in a Debug build
    // (which is what "run it from build-linux/bin and it picks up your
    // settings" means to everyone working in this tree), AppDataLocation
    // otherwise.
#ifdef QT_DEBUG
    return QDir::cleanPath(
        QDir(QCoreApplication::applicationDirPath()).filePath(fileName));
#else
    return QDir::cleanPath(
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
            .filePath(fileName));
#endif
}

}   // namespace AppPaths

#endif // APPPATHS_H
