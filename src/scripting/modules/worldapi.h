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
#include "services/worldmodes.h"

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
    Q_INVOKABLE int shadowResolution();
    Q_INVOKABLE int setShadowResolution(int pixels);
    Q_INVOKABLE bool ambientFromSky(bool enabled);
    Q_INVOKABLE QVariantMap planarReflections();
    Q_INVOKABLE QVariantMap setPlanarReflections(const QVariantMap &params);
    Q_INVOKABLE bool sky(const QString &type, const QVariantMap &params = QVariantMap());
    Q_INVOKABLE QVariantMap get();

    // ---- World Modes (POST_CHAIN_SPEC.md §9.6) -----------------------------
    // Every quality row is reached through these five verbs; there is no
    // per-effect verb and there never will be. The rows themselves come from
    // the worldmodes registry, so a new row is a table entry, not new API.
    Q_INVOKABLE QString mode(const QVariantMap &params = QVariantMap());
    Q_INVOKABLE QVariantMap settings();
    Q_INVOKABLE QVariantMap override(const QVariantMap &params);
    Q_INVOKABLE QVariantMap clearOverride(const QVariantMap &params);
    Q_INVOKABLE QVariantMap clearOverrides();
    Q_INVOKABLE QVariantMap modeTable();
    Q_INVOKABLE QVariantMap postFx(const QVariantMap &params = QVariantMap());

    // ---- set* aliases (AI_SURFACE_PROGRAM_SPEC §3.A item #10, owner D5) ----
    // Nine of this module's verbs are NOUNS that write (world.fog({...}) sets
    // the fog) while the rest of the surface spells a write set* — so an agent
    // reaches for world.setFog, gets a TypeError, and burns a turn. These are
    // one-line delegations, never a second implementation; each doc string
    // names its twin so api_docs does not read as eighteen unrelated verbs.
    // Both spellings are supported forever: the nouns are what every existing
    // script and skill already calls.
    Q_INVOKABLE bool setAmbient(const QVariant &color) { return ambient(color); }
    Q_INVOKABLE bool setGravity(double value) { return gravity(value); }
    Q_INVOKABLE bool setFog(const QVariantMap &params) { return fog(params); }
    Q_INVOKABLE bool setShadows(const QVariantMap &params) { return shadows(params); }
    Q_INVOKABLE bool setGi(const QVariantMap &params) { return gi(params); }
    Q_INVOKABLE bool setAmbientFromSky(bool enabled) { return ambientFromSky(enabled); }
    Q_INVOKABLE bool setSky(const QString &type, const QVariantMap &params = QVariantMap())
    { return sky(type, params); }
    Q_INVOKABLE QString setMode(const QVariantMap &params = QVariantMap()) { return mode(params); }
    Q_INVOKABLE QVariantMap setPostFx(const QVariantMap &params = QVariantMap()) { return postFx(params); }

private:
    iris::ScenePtr sceneOrFail(const QString &verb);
    /// Resolves a texture reference (asset guid, or a file name/path matched by
    /// name in the project DB) to {guid, absolute path}; empty on failure.
    bool resolveTexture(const QVariant &ref, QString &guidOut, QString &pathOut);
    /// One World Mode row, in the shape world.settings() reports.
    static QVariantMap rowState(const iris::ScenePtr &scene, const worldmodes::Row &r);
};

#endif // SCRIPTING_WORLDAPI_H
