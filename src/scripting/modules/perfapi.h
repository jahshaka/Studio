/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_PERFAPI_H
#define SCRIPTING_PERFAPI_H

// perf.* — the render-loop monitor's capture verbs (RENDER_LOOP_MONITOR_SPEC §4.6).
//
// API-FIRST, and literally so: Ctrl+F4, the Preferences row and the MCP tool
// all call these four verbs, and the suites drive the verbs rather than the key
// (the WM-less-Xvfb F-key quirk is why the spec says so out loud).
//
// The monitor COLLECTS; it judges nothing. There is no verb here that returns a
// verdict, a budget or a "wasted" number, because the capture bundle is the
// product and the LEAD does the analysis (owner, 2026-09-12).

#include <QVariantMap>

#include "scripting/apimodule.h"

class PerfApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("perf"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantMap capture(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap stop();
    Q_INVOKABLE QVariantMap status();
    Q_INVOKABLE bool mark(const QString &label);
};

#endif // SCRIPTING_PERFAPI_H
