/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_LOGAPI_H
#define SCRIPTING_LOGAPI_H

// log.* — the session log's runtime surface (SESSION_LOG_SPEC §7).
//
// API-first: every runtime control and every read-back of the log is a verb
// here BEFORE any UI exists for it. The two that matter most for the workflow
// this whole program was built for — hunting a progressive fps decay — are
// log.write (an agent or a human annotating the record: "I started dragging
// here") and log.since (everything that happened after that mark, filtered by
// severity).
//
// Needs::Document throughout: the log exists with no project, no engine and no
// window, which is exactly when the most interesting failures happen.

#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"

class LogApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("log"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantMap path();
    Q_INVOKABLE QVariant level(const QString &category = QString(),
                               const QString &level = QString(),
                               bool persist = false);
    Q_INVOKABLE QVariantList categories();
    Q_INVOKABLE bool write(const QString &category, const QString &level, const QString &message);
    Q_INVOKABLE QVariant mark(const QString &label = QString());
    Q_INVOKABLE QStringList tail(int n = 50, const QVariantMap &filter = QVariantMap());
    Q_INVOKABLE QStringList since(qint64 marker, const QVariantMap &filter = QVariantMap());
    Q_INVOKABLE QVariantMap counts();
    Q_INVOKABLE bool flush();
    Q_INVOKABLE QVariantMap perf(const QVariant &seconds = QVariant(), bool persist = false);
    Q_INVOKABLE QString sample();
};

#endif // SCRIPTING_LOGAPI_H
