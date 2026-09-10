/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_TEXTUREAPI_H
#define SCRIPTING_TEXTUREAPI_H

// texture.* — LIVE TEXTURES (MATERIAL_GAPS_SPEC ADDENDUM A-1).
//
// The producer half of "pixels as an asset": a script makes a live texture,
// writes RGBA into it, and binds it with the material.set it already knows.
// Everything downstream — the material row, the mirror's upload, the
// renderer's sampling — is the path an imported image already takes; the only
// new idea is that the pixels have no file behind them.
//
// Needs::Document throughout, deliberately. A live texture is document state:
// it can be created, written and bound with no engine at all (which is what
// makes the headless suite possible), and the moment an engine exists the
// mirror uploads whatever generation it finds.

#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"

class TextureApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("texture"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariant createLive(const QString &name, int width, int height,
                                    const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool write(const QString &guid, int width, int height,
                           const QVariant &rgba,
                           const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariant info(const QString &guid);
    Q_INVOKABLE QVariantList list();
    Q_INVOKABLE bool remove(const QString &guid);
};

#endif // SCRIPTING_TEXTUREAPI_H
