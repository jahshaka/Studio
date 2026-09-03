/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/particlesapi.h"

#include <algorithm>

#include "scripting/modules/moduleshared.h"
#include "services/sceneeditservice.h"
#include "services/services.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/assets/texture2d.h"
#include "data/database/database.h"

using namespace scriptmod;

QVector<VerbInfo> ParticlesApi::verbs() const
{
    return {
        { "presets", "particles.presets() -> [name]",
          "Every emitter recipe particles.preset accepts: custom, fire, embers, smoke, rain, "
          "snow, steadyFlow, sparks.",
          Needs::Document },
        { "preset", "particles.preset(id, name) -> bool",
          "Stamps a whole emitter recipe onto a particle node — rate, velocity, lifetime, size, "
          "cone, forces, turbulence, blend mode, quota and the over-life ramps, in one call. "
          "Leaves the node's identity, transform and texture alone. Fire and the other emissive "
          "recipes carry HDR colour keys (values above 1) and only look like fire with HDR and "
          "bloom on in the view's post chain.",
          Needs::Document },
        { "describe", "particles.describe(id) -> {rate, velocity, lifetime, size, shape, "
                      "orientation, quota, additive, colourKeys, scaleKeys, ...}",
          "The emitter's resolved authoring state, including the ramps that node.properties "
          "cannot carry. Read-only.",
          Needs::Document },
        { "colourKeys", "particles.colourKeys(id) -> [{time, r, g, b, a}]",
          "The colour-over-life ramp, in ascending time. Empty means no ramp.",
          Needs::Document },
        { "setColourKeys", "particles.setColourKeys(id, [{time, r, g, b, a}]) -> bool",
          "Replaces the colour-over-life ramp (up to 6 keys, times as life fractions in 0..1). "
          "Channels are LINEAR and may exceed 1 — that is what makes fire bloom. An empty list "
          "clears the ramp. This is the single lever that turns quads into fire.",
          Needs::Document },
        { "scaleKeys", "particles.scaleKeys(id) -> [{time, scale}]",
          "The scale-over-life ramp, in ascending time. Empty means constant size.",
          Needs::Document },
        { "setScaleKeys", "particles.setScaleKeys(id, [{time, scale}]) -> bool",
          "Replaces the scale-over-life ramp (up to 6 keys, times as life fractions in 0..1, "
          "scales as multipliers of particleScale). An empty list clears it. Note a system with "
          "a scale ramp draws SQUARE particles: the renderer's scale affector replaces both "
          "dimensions rather than multiplying them.",
          Needs::Document },
        { "timeScale", "particles.timeScale(scale?) -> number",
          "The scene's particle simulation clock: 1 is real time, 0 freezes every emitter, 2 is "
          "double speed. Called with no argument it reads. SCENE-wide and, in the renderer, "
          "process-wide — there is exactly one frame-time source, so no per-emitter clock exists.",
          Needs::Document },
    };
}

iris::ParticleSystemNodePtr ParticlesApi::emitterOrFail(const QString &id, const QString &verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene()
                                                             : iris::ScenePtr();
    if (!scene) {
        fail(QStringLiteral("%1: no scene is open").arg(verb));
        return iris::ParticleSystemNodePtr();
    }
    auto node = findNodeByGuid(scene->getRootNode(), id);
    if (!node) {
        fail(QStringLiteral("%1: no node with id '%2'").arg(verb, id));
        return iris::ParticleSystemNodePtr();
    }
    if (node->getSceneNodeType() != iris::SceneNodeType::ParticleSystem) {
        fail(QStringLiteral("%1: '%2' is a %3, not a particle system")
                 .arg(verb, node->getName(), nodeTypeName(node->getSceneNodeType())));
        return iris::ParticleSystemNodePtr();
    }
    return node.staticCast<iris::ParticleSystemNode>();
}

QStringList ParticlesApi::presets()
{
    return iris::ParticleSystemNode::presetNames();
}

bool ParticlesApi::preset(const QString &id, const QString &name)
{
    auto ps = emitterOrFail(id, QStringLiteral("particles.preset"));
    if (!ps) return false;
    const QStringList known = iris::ParticleSystemNode::presetNames();
    // Unknown names would silently fall through to Custom and wipe the emitter,
    // which is a nasty way to learn you typed "smoke2".
    const bool ok = std::any_of(known.begin(), known.end(), [&](const QString &k) {
        return k.compare(name, Qt::CaseInsensitive) == 0;
    });
    if (!ok)
        return fail(QStringLiteral("particles.preset: unknown preset '%1' (try: %2)")
                        .arg(name, known.join(", ")));
    ps->applyPreset(iris::ParticleSystemNode::presetFromName(name));
    return true;
}

QVariantMap ParticlesApi::describe(const QString &id)
{
    auto ps = emitterOrFail(id, QStringLiteral("particles.describe"));
    if (!ps) return QVariantMap();

    QVariantMap m;
    m["id"] = ps->getGUID();
    m["name"] = ps->getName();
    m["preset"] = iris::ParticleSystemNode::presetName(ps->preset);
    m["shape"] = iris::ParticleSystemNode::shapeName(ps->shape);
    m["orientation"] = iris::ParticleSystemNode::orientationName(ps->orientation);
    m["rate"] = ps->particlesPerSecond;
    m["speed"] = ps->speed;
    m["speedError"] = ps->speedError;
    m["lifeLength"] = ps->lifeLength;
    m["lifeError"] = ps->lifeError;
    m["particleScale"] = ps->particleScale;
    m["scaleError"] = ps->scaleError;
    m["coneAngle"] = ps->coneAngle;
    m["gravityComplement"] = ps->gravityComplement;
    m["turbulence"] = ps->turbulence;
    m["wind"] = vecToJs(ps->wind);
    m["extents"] = vecToJs(ps->extents);
    m["innerExtents"] = vecToJs(ps->innerExtents);
    m["rotationSpeedMin"] = ps->rotationSpeedMin;
    m["rotationSpeedMax"] = ps->rotationSpeedMax;
    m["randomRotation"] = ps->randomRotation;
    m["additive"] = ps->useAdditive;
    m["alphaHash"] = ps->alphaHash;
    m["dissipate"] = ps->dissipate;
    m["dissipateInv"] = ps->dissipateInv;
    m["burstDuration"] = ps->burstDuration;
    m["burstRepeatDelay"] = ps->burstRepeatDelay;
    m["startDelay"] = ps->startDelay;
    m["quota"] = ps->maxParticles;
    m["emitColourStart"] = colorToJs(ps->emitColourStart);
    m["emitColourEnd"] = colorToJs(ps->emitColourEnd);
    m["texture"] = ps->texture ? ps->texture->getSource() : QString();
    m["colourKeys"] = colourKeys(id);
    m["scaleKeys"] = scaleKeys(id);
    return m;
}

QVariantList ParticlesApi::colourKeys(const QString &id)
{
    auto ps = emitterOrFail(id, QStringLiteral("particles.colourKeys"));
    if (!ps) return QVariantList();
    QVariantList out;
    for (const iris::ParticleColourKey &k : ps->colourKeys)
        out.append(QVariantMap{ { "time", k.time }, { "r", k.r },
                                { "g", k.g }, { "b", k.b }, { "a", k.a } });
    return out;
}

bool ParticlesApi::setColourKeys(const QString &id, const QVariant &keys)
{
    auto ps = emitterOrFail(id, QStringLiteral("particles.setColourKeys"));
    if (!ps) return false;
    const QVariantList list = normalizeJs(keys).toList();
    if (list.size() > 6)
        return fail(QStringLiteral("particles.setColourKeys: at most 6 keys (got %1) — "
                                   "the renderer's interpolator has 6 stages")
                        .arg(list.size()));
    QVector<iris::ParticleColourKey> out;
    for (const QVariant &v : list) {
        const QVariantMap m = normalizeJs(v).toMap();
        iris::ParticleColourKey k;
        k.time = m.value("time", 0.0).toFloat();
        // Channels are LINEAR and unclamped on purpose: >1 is HDR, and HDR is
        // the difference between a flame and an orange sticker.
        k.r = m.value("r", 1.0).toFloat();
        k.g = m.value("g", 1.0).toFloat();
        k.b = m.value("b", 1.0).toFloat();
        k.a = m.value("a", 1.0).toFloat();
        out.append(k);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const iris::ParticleColourKey &a, const iris::ParticleColourKey &b) {
                         return a.time < b.time;
                     });
    ps->colourKeys = out;
    return true;
}

QVariantList ParticlesApi::scaleKeys(const QString &id)
{
    auto ps = emitterOrFail(id, QStringLiteral("particles.scaleKeys"));
    if (!ps) return QVariantList();
    QVariantList out;
    for (const iris::ParticleScaleKey &k : ps->scaleKeys)
        out.append(QVariantMap{ { "time", k.time }, { "scale", k.scale } });
    return out;
}

bool ParticlesApi::setScaleKeys(const QString &id, const QVariant &keys)
{
    auto ps = emitterOrFail(id, QStringLiteral("particles.setScaleKeys"));
    if (!ps) return false;
    const QVariantList list = normalizeJs(keys).toList();
    if (list.size() > 6)
        return fail(QStringLiteral("particles.setScaleKeys: at most 6 keys (got %1) — "
                                   "the renderer's interpolator has 6 stages")
                        .arg(list.size()));
    QVector<iris::ParticleScaleKey> out;
    for (const QVariant &v : list) {
        const QVariantMap m = normalizeJs(v).toMap();
        iris::ParticleScaleKey k;
        k.time  = m.value("time", 0.0).toFloat();
        k.scale = std::max(0.0f, m.value("scale", 1.0).toFloat());
        out.append(k);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const iris::ParticleScaleKey &a, const iris::ParticleScaleKey &b) {
                         return a.time < b.time;
                     });
    ps->scaleKeys = out;
    return true;
}

double ParticlesApi::timeScale(const QVariant &scale)
{
    auto sceneEdit = host.services ? host.services->sceneEdit : nullptr;
    auto scene = sceneEdit ? sceneEdit->scene() : iris::ScenePtr();
    if (!scene) {
        fail(QStringLiteral("particles.timeScale: no scene is open"));
        return 0.0;
    }
    const QVariant v = normalizeJs(scale);
    if (v.isValid() && !v.isNull())
        scene->particleTimeScale = std::max(0.0f, v.toFloat());
    return scene->particleTimeScale;
}
