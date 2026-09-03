/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_PARTICLESAPI_H
#define SCRIPTING_PARTICLESAPI_H

// particles.* — emitter recipes, over-life ramps and the simulation clock
// (PARTICLES_FX2_SPEC.md §6).
//
// Deliberately NOT the whole particle surface. Every SCALAR row —
// particlesPerSecond, speed, lifeLength, particleScale, gravityComplement,
// coneAngle, turbulence, shape, orientation, extents, wind, the colours, the
// spreads, maxParticles — is a reflected property on ParticleSystemNode and is
// reached through node.setProperty / node.properties like any other node field.
// Duplicating them here would be two APIs for one model.
//
// What lives here is exactly what node.setProperty cannot express:
//   * preset — a whole recipe in one call, one undo step;
//   * colourKeys / scaleKeys — LISTS, and the Property system has no list kind;
//   * describe — the resolved emitter, for tests, MCP and "what am I looking at";
//   * timeScale — a SCENE value, not a node one (the renderer has one
//     frame-time source for the whole process; §10.3).
//
// The emitter's particle IMAGE is not here either, and that is deliberate: it
// is node.setParticleTexture, beside node.setDecalTexture and
// node.setLightTexture. "Bind an asset texture to a node of type X" was already
// a node.* shape before particles had a namespace at all.

#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"
#include "irisgl/irisglfwd.h"

class ParticlesApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("particles"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QStringList presets();
    Q_INVOKABLE bool preset(const QString &id, const QString &name);
    Q_INVOKABLE QVariantMap describe(const QString &id);
    Q_INVOKABLE QVariantList colourKeys(const QString &id);
    Q_INVOKABLE bool setColourKeys(const QString &id, const QVariant &keys);
    Q_INVOKABLE QVariantList scaleKeys(const QString &id);
    Q_INVOKABLE bool setScaleKeys(const QString &id, const QVariant &keys);
    Q_INVOKABLE double timeScale(const QVariant &scale = QVariant());

private:
    /// The emitter with this guid, or null with a JS error already thrown.
    iris::ParticleSystemNodePtr emitterOrFail(const QString &id, const QString &verb);
};

#endif // SCRIPTING_PARTICLESAPI_H
