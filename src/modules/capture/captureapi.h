/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CAPTUREAPI_H
#define CAPTUREAPI_H

// capture.* — the video recorder's verbs (VIDEO-REC-1; SPECS/VIDEO_CAPTURE_SPEC.md
// §2 + §10). The name `video` is the video-TEXTURE transport's; this is the
// recorder. The record button (CaptureModule) calls these same implementations.
// NOTHING HERE THROWS for a refusal: a recording that cannot start answers null
// and says why in app.lastError (the refuse() contract).

#include <QVariant>
#include <QVariantMap>

#include "scripting/apimodule.h"

class CaptureModule;

class CaptureApi : public ApiModule
{
    Q_OBJECT
public:
    CaptureApi(ScriptHost &host, CaptureModule *module);

    QString jsName() const override { return QStringLiteral("capture"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariant start(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariant stop(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantMap status();
    Q_INVOKABLE QVariantMap wait(int timeoutMs = 30000);
    Q_INVOKABLE QVariantMap inspect(const QString &path, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool lastFrame(const QString &path);
    Q_INVOKABLE bool press();
    Q_INVOKABLE QVariantMap button();
    Q_INVOKABLE bool helpers(const QVariant &on = QVariant());
    Q_INVOKABLE bool dismiss();

private:
    CaptureModule *mModule;
};

#endif // CAPTUREAPI_H
