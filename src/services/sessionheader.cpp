/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/sessionheader.h"

#include "data/constants.h"
#include "services/jahlog.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QStandardPaths>
#include <QVector>

#ifndef GIT_COMMIT_HASH
#define GIT_COMMIT_HASH "0000"
#endif
#ifndef GIT_COMMIT_DATE
#define GIT_COMMIT_DATE "unknown"
#endif
#ifndef JAHSHAKA_VERSION
#define JAHSHAKA_VERSION "unknown"
#endif

namespace SessionHeader {

namespace {

struct Group
{
    QString name;
    Provider provider;
};

QVector<Group> &providers()
{
    static QVector<Group> g;
    return g;
}

}   // namespace

void addProvider(const QString &group, Provider provider)
{
    if (provider) providers().append(Group { group, std::move(provider) });
}

Rows baseRows()
{
    Rows r;
    r << Row { QStringLiteral("app version"), Constants::CONTENT_VERSION };
    r << Row { QStringLiteral("build version"), QStringLiteral(JAHSHAKA_VERSION) };
    // THE SAME TRIPLE the shader-cache fingerprint uses (enginehost.cpp
    // resolveConfig's appBuildId) — reused verbatim so a log and a cache
    // directory can be correlated by eye.
    r << Row { QStringLiteral("build id"),
               QStringLiteral("%1/%2/%3").arg(Constants::CONTENT_VERSION,
                                              QStringLiteral(GIT_COMMIT_HASH),
                                              QStringLiteral(GIT_COMMIT_DATE)) };
#ifdef QT_DEBUG
    r << Row { QStringLiteral("build type"), QStringLiteral("Development (QT_DEBUG)") };
#else
    r << Row { QStringLiteral("build type"), QStringLiteral("Production") };
#endif
    // Compiled AND runtime: the drift between them is a real support question.
    r << Row { QStringLiteral("qt"),
               QStringLiteral("%1 (compiled) / %2 (runtime)")
                   .arg(QStringLiteral(QT_VERSION_STR), QString::fromLatin1(qVersion())) };

    QString platform = QStringLiteral("(no gui application)");
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        platform = gui->platformName();
    else if (QCoreApplication::instance())
        platform = QStringLiteral("(core application)");
    // XWayland is worth naming: it is where the screenshot rule, the shortcut
    // oddities and half the input surprises on this box come from.
    if (platform == QLatin1String("xcb") && !qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        platform += QStringLiteral(" (XWayland: WAYLAND_DISPLAY is set)");
    r << Row { QStringLiteral("platform / qpa"), platform };

    r << Row { QStringLiteral("command line"),
               QCoreApplication::arguments().join(QLatin1Char(' ')) };
    r << Row { QStringLiteral("working dir"), QDir::currentPath() };
    r << Row { QStringLiteral("data root"),
               QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) };
    r << Row { QStringLiteral("process id"),
               QString::number(QCoreApplication::applicationPid()) };
    r << Row { QStringLiteral("session log"), JahLog::sessionFilePath() };
    r << Row { QStringLiteral("ogre log"), JahLog::ogreFilePath() };
    r << Row { QStringLiteral("started"),
               QDateTime::currentDateTime().toString(Qt::ISODate) };
    return r;
}

Rows allRows()
{
    Rows all = baseRows();
    for (const Group &g : providers()) {
        const Rows rows = g.provider ? g.provider() : Rows {};
        for (const Row &row : rows)
            all << Row { g.name + QLatin1Char('.') + row.first, row.second };
    }
    return all;
}

void emitBlock()
{
    JahLog::writeHeaderBlock(QStringLiteral("=== SESSION START ==="), allRows());
}

}   // namespace SessionHeader
