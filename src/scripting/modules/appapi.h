/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_APPAPI_H
#define SCRIPTING_APPAPI_H

// app.* — window-level navigation (SCRIPTING_SPEC §1.1): spaces and desktops.
// Wraps, doesn't extract — these are inherently window verbs.

#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"

class AppApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("app"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE int desktop(int n = 0);
    Q_INVOKABLE bool space(const QString &name);
    Q_INVOKABLE bool quit();
    Q_INVOKABLE QVariantList openTimings();
    Q_INVOKABLE QVariantMap openStats(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool heartbeat(int intervalMs = 250);
    Q_INVOKABLE QVariantMap heartbeatStats();
    Q_INVOKABLE QVariantMap watchdogStats();
    Q_INVOKABLE QVariantMap watchdog(int stallMs);
    Q_INVOKABLE bool blockUiThread(int ms);
    Q_INVOKABLE QVariantMap shaderCache();
    Q_INVOKABLE bool clearShaderCache();
    Q_INVOKABLE bool saveShaderCache();
    Q_INVOKABLE bool flushShaderCache(int budgetMs = 20000);
    Q_INVOKABLE QVariantMap warmUpSet(const QString &action = QString());
    Q_INVOKABLE QVariantMap engineErrors(bool reset = false);
    Q_INVOKABLE QVariantMap frameStats(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap pacing(const QString &mode = QString());
    Q_INVOKABLE QVariantMap renderStats();
    Q_INVOKABLE QVariantMap engineObjects();
    Q_INVOKABLE QVariantMap threading();
    Q_INVOKABLE QVariantMap memoryStats();
    Q_INVOKABLE QVariantMap textureMemory(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap reclaimMemory();
    Q_INVOKABLE QVariantMap textureStreaming();
    Q_INVOKABLE QVariantMap waitForTextures();
    Q_INVOKABLE QVariantList ogreSamples();
    Q_INVOKABLE QVariantMap launchOgreSample(const QString &name,
                                             const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantList apiProblems();
    /// THE THIRD-PARTY NOTICES (NOTICES-1). `{id}` adds that component's
    /// full licence text; with no argument it is the list.
    Q_INVOKABLE QVariantList notices(const QVariant &which = QVariant());
    Q_INVOKABLE QVariantMap dataRoot();
    /// RESET THE LIBRARY to a first launch (owner review R10.2). The rows,
    /// the store's contents, every project folder — and then the fresh-install
    /// bootstrap. `{restart: true}` brings the app back up.
    Q_INVOKABLE QVariantMap resetLibrary(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap mcpLogging(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap window();
    Q_INVOKABLE QVariantMap resizeWindow(int width, int height);
    Q_INVOKABLE QVariantMap columns();
    Q_INVOKABLE QVariantList docks();
    Q_INVOKABLE QVariant lastError();
    Q_INVOKABLE QVariantMap scriptPolicy(const QString &mode = QString());
    Q_INVOKABLE QVariantMap theme();
    Q_INVOKABLE QVariantMap styleSheets(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantList dialogs();
    Q_INVOKABLE QVariantMap dialog(const QString &name,
                                   const QVariant &openOrOptions = QVariant(true));
};

#endif // SCRIPTING_APPAPI_H
