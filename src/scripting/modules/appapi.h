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
    Q_INVOKABLE bool heartbeat(int intervalMs = 250);
    Q_INVOKABLE QVariantMap heartbeatStats();
    Q_INVOKABLE QVariantMap watchdogStats();
    Q_INVOKABLE bool blockUiThread(int ms);
    Q_INVOKABLE QVariantMap shaderCache();
    Q_INVOKABLE bool clearShaderCache();
    Q_INVOKABLE bool saveShaderCache();
    Q_INVOKABLE QVariantMap warmUpSet(const QString &action = QString());
    Q_INVOKABLE QVariantMap engineErrors(bool reset = false);
    Q_INVOKABLE QVariantMap frameStats();
};

#endif // SCRIPTING_APPAPI_H
