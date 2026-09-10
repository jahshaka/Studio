/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_VIDEOAPI_H
#define SCRIPTING_VIDEOAPI_H

// video.* — a video asset decoded into a LIVE TEXTURE (ASSET_MEDIA_SPEC §3
// Option A, MATERIAL_GAPS_SPEC ADDENDUM A-1).
//
// The verbs are a transport (play/pause/seek/loop) plus the one thing that
// makes a clip a MATERIAL rather than a preview: bind(videoGuid,
// textureGuid). After that the texture is an ordinary live texture — every
// decoded frame is a write, every write is a generation bump, and the material
// that binds the texture animates with no further help from anyone.
//
// Needs::Document: Qt Multimedia's ffmpeg backend decodes with no display
// (proven by the thumbnail grabber and the video preview), so this whole
// surface works headless — which is what lets a suite assert on frames instead
// of on screenshots.

#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"

class VideoApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("video"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE bool bind(const QString &videoGuid, const QString &textureGuid);
    Q_INVOKABLE bool unbind(const QString &videoGuid);
    Q_INVOKABLE bool play(const QString &videoGuid);
    Q_INVOKABLE bool pause(const QString &videoGuid);
    Q_INVOKABLE bool stop(const QString &videoGuid);
    Q_INVOKABLE bool seek(const QString &videoGuid, double positionMs);
    Q_INVOKABLE bool loop(const QString &videoGuid, bool on = true);
    Q_INVOKABLE bool step(const QString &videoGuid, int timeoutMs = 4000);
    Q_INVOKABLE QVariant state(const QString &videoGuid);
    Q_INVOKABLE QVariantList list();

private:
    /// The file behind a video guid — a path that exists is taken as-is, a
    /// guid resolves through the CAS exactly as material.set resolves a
    /// texture guid. Empty on failure, with the reason already reported.
    QString sourceFor(const QString &videoGuid);
};

#endif // SCRIPTING_VIDEOAPI_H
