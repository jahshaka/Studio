/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef TESTS_SUPPORT_SEEDSETTINGS_H
#define TESTS_SUPPORT_SEEDSETTINGS_H

// SEEDING THE PREFERENCES AN APP-SPAWNING SUITE NEEDS, into the file the app it
// is about to spawn will ACTUALLY READ.
//
// Four suites need the same two keys before they can drive a real binary:
//
//   ddialog_seen  the donate dialog is modal at shutdown. Unseeded, `app.quit()`
//                 puts it on screen and the process never exits — the suite then
//                 fails on its exit budget with ZERO shutdown steps recorded,
//                 which is not a shutdown-ordering failure at all.
//   auto_save     the unsaved-changes prompt, same shape, same consequence.
//
// They each had their own copy of the same block, and every copy wrote
// `<binary dir>/jahsettings.ini` — the file that IS shared by every run of a
// build tree under QT_DEBUG (applicationDirPath), i.e. the developer's own. So
// the suites that exist to prove the app shuts down cleanly were themselves an
// instance of ENGINEERING_DEBT_SPEC ADDENDUM 6.
//
// PRECEDENCE MIRRORS AppPaths (src/services/apppaths.h) EXACTLY, and it has to:
// seeding a file the app will not read is indistinguishable from not seeding at
// all, and the failure it produces (a hang at exit) points at the wrong code.
//   1. JAHSHAKA_DATA_ROOT set  ->  <root>/jahsettings.ini, and NOTHING ELSE.
//                                  The run is hermetic and the shared file is
//                                  not touched.
//   2. otherwise               ->  the historical locations, exactly as before:
//                                  beside the binary, plus AppDataLocation in a
//                                  non-Debug build.
//
// `--data-root` is not consulted because a suite passes the environment
// variable; if one ever passes the flag instead, it must seed the same
// directory it passes.

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace testsupport {

/// The settings file(s) a spawned Jahshaka will read, given this environment.
inline QStringList settingsFilesForSpawnedApp(const QString &binary)
{
    const QString dataRoot = QProcessEnvironment::systemEnvironment()
                                 .value(QStringLiteral("JAHSHAKA_DATA_ROOT"))
                                 .trimmed();
    if (!dataRoot.isEmpty()) {
        const QString abs = QDir::cleanPath(QDir::current().absoluteFilePath(dataRoot));
        QDir().mkpath(abs);
        return { QDir(abs).filePath(QStringLiteral("jahsettings.ini")) };
    }

    QStringList inis;
    inis << QFileInfo(binary).dir().filePath(QStringLiteral("jahsettings.ini"));
#ifndef QT_DEBUG
    // Evaluate AppDataLocation exactly as the app does — under ITS application
    // name, not this test binary's. Only in non-Debug builds, so a Debug tree
    // does not gain a stray settings file in the developer's data directory.
    const QString testName = QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QStringLiteral("Jahshaka"));
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QCoreApplication::setApplicationName(testName);
    if (!appData.isEmpty() && QDir().mkpath(appData))
        inis << QDir(appData).filePath(QStringLiteral("jahsettings.ini"));
#endif
    return inis;
}

/// Seeds the two keys every app-spawning suite needs to reach a clean quit.
inline void seedSettingsForSpawnedApp(const QString &binary)
{
    const QStringList inis = settingsFilesForSpawnedApp(binary);
    for (const QString &ini : inis) {
        QSettings settings(ini, QSettings::IniFormat);
        settings.setValue(QStringLiteral("ddialog_seen"), true);
        settings.setValue(QStringLiteral("auto_save"), true);
        settings.sync();
    }
}

}   // namespace testsupport

#endif // TESTS_SUPPORT_SEEDSETTINGS_H
