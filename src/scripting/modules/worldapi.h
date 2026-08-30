/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_WORLDAPI_H
#define SCRIPTING_WORLDAPI_H

// world.* — sky, fog, GI, ambient, gravity, shadows (SCRIPTING_SPEC §1.5).
//
// The document fields are the API (SceneMirror polls them per frame); the one
// non-trivial contract is the sky: a change must ALSO rebuild the matching
// scene->skyData[<key>] JSON (SceneWriter serializes ONLY skyData — stale
// skyData means the save loses the change), and the widgets' tail calls
// (switchSkyTexture + queueSkyCapture) keep the legacy renderer in step.

#include <QVariantMap>

#include "scripting/apimodule.h"
#include "irisgl/irisglfwd.h"

class WorldApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("world"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE bool ambient(const QVariant &color);
    Q_INVOKABLE bool gravity(double value);
    Q_INVOKABLE bool fog(const QVariantMap &params);
    Q_INVOKABLE bool shadows(const QVariantMap &params);
    Q_INVOKABLE bool gi(const QVariantMap &params);
    Q_INVOKABLE int antiAliasing();
    Q_INVOKABLE int setAntiAliasing(int samples);
    Q_INVOKABLE bool sky(const QString &type, const QVariantMap &params = QVariantMap());
    Q_INVOKABLE QVariantMap get();

private:
    iris::ScenePtr sceneOrFail(const QString &verb);
    /// Resolves a texture reference (asset guid, or a file name/path matched by
    /// name in the project DB) to {guid, absolute path}; empty on failure.
    bool resolveTexture(const QVariant &ref, QString &guidOut, QString &pathOut);
};

#endif // SCRIPTING_WORLDAPI_H
