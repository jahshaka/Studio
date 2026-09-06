/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/previewlauncher.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>
#include <QUuid>

QString PreviewLauncher::findChromiumBrowser()
{
    // Ordered probes (audit §7.3): the big-name Chromium family first.
    const QStringList names = {
        QStringLiteral("google-chrome"),
        QStringLiteral("google-chrome-stable"),
        QStringLiteral("chromium"),
        QStringLiteral("chromium-browser"),
        QStringLiteral("brave-browser"),
        QStringLiteral("microsoft-edge"),
#ifdef Q_OS_WIN
        QStringLiteral("chrome"),
        QStringLiteral("msedge"),
#endif
    };
    for (const QString &name : names) {
        const QString path = QStandardPaths::findExecutable(name);
        if (!path.isEmpty()) return path;
    }
#ifdef Q_OS_MAC
    const QStringList macApps = {
        QStringLiteral("/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"),
        QStringLiteral("/Applications/Microsoft Edge.app/Contents/MacOS/Microsoft Edge"),
        QStringLiteral("/Applications/Chromium.app/Contents/MacOS/Chromium"),
        QStringLiteral("/Applications/Brave Browser.app/Contents/MacOS/Brave Browser"),
    };
    for (const QString &path : macApps)
        if (QFileInfo::exists(path)) return path;
#endif
    return QString();
}

QString PreviewLauncher::newProfileDir()
{
    // --user-data-dir keeps the preview isolated from the user's profile and
    // avoids "Chrome is already running" single-instance ties (audit §7.3).
    //
    // It must be UNIQUE PER RUN and OUTSIDE the export folder (PUBLISH_AUDIT
    // #3). Beside index.html it was two bugs at once: the profile shipped in
    // the published deliverable (the folder the user uploads), and a shared
    // fixed path let a previous run's Chrome — live or half-dead — singleton-
    // capture this launch and merely forward the URL to its own old window,
    // the exact defect embeddedpreview.cpp already fixed the same way.
    // Cleanup is on QProcess::finished; a crash leaves at most one directory
    // in the OS temp location, which the OS reaps.
    return QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("jah-preview-profile-") +
                  QUuid::createUuid().toString(QUuid::Id128).left(12));
}

QProcess *PreviewLauncher::launchKiosk(const QString &indexHtml, QObject *parent)
{
    const QString browser = findChromiumBrowser();
    if (browser.isEmpty()) return nullptr;

    const QFileInfo info(indexHtml);
    if (!info.exists()) return nullptr;

    const QString profileDir = newProfileDir();
    auto *process = new QProcess(parent);
    process->setProgram(browser);
    QStringList args = {
        QStringLiteral("--app=%1").arg(QUrl::fromLocalFile(info.absoluteFilePath()).toString()),
        QStringLiteral("--window-size=1280,800"),
        QStringLiteral("--user-data-dir=%1").arg(profileDir),
        QStringLiteral("--no-first-run"),
        QStringLiteral("--no-default-browser-check"),
    };
#ifdef Q_OS_LINUX
    // The app itself always runs on xcb (Ogre rule). Chrome left to its own
    // backend choice picks Wayland from the session type — which EXITS
    // immediately on an X-only display (rig, ssh-forwarded X). Pin it to X11
    // like the embedded preview does; on a Wayland desktop that is XWayland,
    // proven by the embed spike.
    if (QGuiApplication::platformName() == QLatin1String("xcb"))
        args.append(QStringLiteral("--ozone-platform=x11"));
#endif
    process->setArguments(args);
    QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     process, [profileDir](int, QProcess::ExitStatus) {
                         QDir(profileDir).removeRecursively();
                     });
    process->start();
    // The caller is told "a browser is running" — so make that true. start() is
    // asynchronous and a browser that dies immediately (missing libs, refused
    // display, bad profile) reported success from here, and previewWeb then
    // returned mode:"kiosk" for a window nobody ever saw (PUBLISH_AUDIT #11).
    // 1500 ms is start-up, not page load: Chrome forks its zygote well inside
    // it, and the caller's fallback (open in the default browser) is cheap.
    if (!process->waitForStarted(1500)) {
        QDir(profileDir).removeRecursively();
        delete process;
        return nullptr;
    }
    return process;
}

bool PreviewLauncher::openInBrowser(const QString &indexHtml)
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(indexHtml));
}
