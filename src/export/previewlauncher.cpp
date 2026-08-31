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
#include <QFileInfo>
#include <QGuiApplication>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QUrl>

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

QProcess *PreviewLauncher::launchKiosk(const QString &indexHtml, QObject *parent)
{
    const QString browser = findChromiumBrowser();
    if (browser.isEmpty()) return nullptr;

    const QFileInfo info(indexHtml);
    if (!info.exists()) return nullptr;

    // --user-data-dir keeps the preview isolated from the user's profile and
    // avoids "Chrome is already running" single-instance ties (audit §7.3).
    const QString profileDir = info.absolutePath() + QStringLiteral("/.preview-profile");
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
    process->start();
    return process;
}

bool PreviewLauncher::openInBrowser(const QString &indexHtml)
{
    return QDesktopServices::openUrl(QUrl::fromLocalFile(indexHtml));
}
