/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/scenepropertycommand.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector3D>

#include "irisgl/core/math/qtinterop.h"
#include "irisgl/document/scenegraph/looks.h"
#include "irisgl/document/scenegraph/scene.h"
#include "services/worldmodes.h"

namespace {

using iris::ScenePtr;

QVariant vec(const iris::Vec3 &v) { return QVariant::fromValue(iris::toQt(v)); }
iris::Vec3 vec(const QVariant &v) { return iris::fromQt(v.value<QVector3D>()); }

/// The sky block, whole (see the header for why it is one value).
QVariant captureSky(const ScenePtr &s)
{
    QVariantMap m;
    m["skyType"] = int(s->skyType);
    QJsonObject data;
    for (auto it = s->skyData.constBegin(); it != s->skyData.constEnd(); ++it)
        data.insert(it.key(), it.value());
    m["skyData"] = data;
    m["skyColor"] = s->skyColor;
    m["gradientTop"] = s->gradientTop;
    m["gradientMid"] = s->gradientMid;
    m["gradientBot"] = s->gradientBot;
    m["gradientOffset"] = s->gradientOffset;
    m["luminance"] = s->skyRealistic.luminance;
    m["reileigh"] = s->skyRealistic.reileigh;
    m["mieCoefficient"] = s->skyRealistic.mieCoefficient;
    m["mieDirectionalG"] = s->skyRealistic.mieDirectionalG;
    m["turbidity"] = s->skyRealistic.turbidity;
    m["sunPosX"] = s->skyRealistic.sunPosX;
    m["sunPosY"] = s->skyRealistic.sunPosY;
    m["sunPosZ"] = s->skyRealistic.sunPosZ;
    return m;
}

void applySky(const ScenePtr &s, const QVariant &value)
{
    const QVariantMap m = value.toMap();
    if (m.isEmpty()) return;
    s->skyType = static_cast<iris::SkyType>(m.value("skyType").toInt());
    const QJsonObject data = m.value("skyData").toJsonObject();
    s->skyData.clear();
    for (auto it = data.constBegin(); it != data.constEnd(); ++it)
        s->skyData.insert(it.key(), it.value().toObject());
    s->skyColor = m.value("skyColor").value<QColor>();
    s->gradientTop = m.value("gradientTop").value<QColor>();
    s->gradientMid = m.value("gradientMid").value<QColor>();
    s->gradientBot = m.value("gradientBot").value<QColor>();
    s->gradientOffset = m.value("gradientOffset").toFloat();
    s->skyRealistic.luminance = m.value("luminance").toFloat();
    s->skyRealistic.reileigh = m.value("reileigh").toFloat();
    s->skyRealistic.mieCoefficient = m.value("mieCoefficient").toFloat();
    s->skyRealistic.mieDirectionalG = m.value("mieDirectionalG").toFloat();
    s->skyRealistic.turbidity = m.value("turbidity").toFloat();
    s->skyRealistic.sunPosX = m.value("sunPosX").toFloat();
    s->skyRealistic.sunPosY = m.value("sunPosY").toFloat();
    s->skyRealistic.sunPosZ = m.value("sunPosZ").toFloat();
}

QVector<sceneprops::Field> buildFields()
{
    QVector<sceneprops::Field> f;
    auto add = [&f](const char *id, sceneprops::Getter get, sceneprops::Setter set) {
        f.append({ QLatin1String(id), std::move(get), std::move(set) });
    };

    // ---- World section ----------------------------------------------------
    add("ambientColor", [](const ScenePtr &s) { return QVariant(s->ambientColor); },
        [](const ScenePtr &s, const QVariant &v) { s->setAmbientColor(v.value<QColor>()); });
    // setWorldGravity, never the raw field: it drives the Bullet world too.
    add("gravity", [](const ScenePtr &s) { return QVariant(s->gravity); },
        [](const ScenePtr &s, const QVariant &v) { s->setWorldGravity(v.toFloat()); });
    add("playMode", [](const ScenePtr &s) { return QVariant(int(s->getPlayMode())); },
        [](const ScenePtr &s, const QVariant &v) {
            s->setPlayMode(static_cast<iris::ScenePlayMode>(v.toInt()));
        });
    add("ambientMusicVolume", [](const ScenePtr &s) { return QVariant(s->ambientMusicVolume); },
        [](const ScenePtr &s, const QVariant &v) { s->setAmbientMusicVolume(v.toFloat()); });

    // ---- Fog section ------------------------------------------------------
    add("fogEnabled", [](const ScenePtr &s) { return QVariant(s->fogEnabled); },
        [](const ScenePtr &s, const QVariant &v) { s->fogEnabled = v.toBool(); });
    add("fogColor", [](const ScenePtr &s) { return QVariant(s->fogColor); },
        [](const ScenePtr &s, const QVariant &v) { s->fogColor = v.value<QColor>(); });
    add("fogDensity", [](const ScenePtr &s) { return QVariant(s->fogDensity); },
        [](const ScenePtr &s, const QVariant &v) { s->fogDensity = v.toFloat(); });
    add("fogHeightDensity", [](const ScenePtr &s) { return QVariant(s->fogHeightDensity); },
        [](const ScenePtr &s, const QVariant &v) { s->fogHeightDensity = v.toFloat(); });
    add("fogHeightFalloff", [](const ScenePtr &s) { return QVariant(s->fogHeightFalloff); },
        [](const ScenePtr &s, const QVariant &v) { s->fogHeightFalloff = v.toFloat(); });
    add("fogHeightLevel", [](const ScenePtr &s) { return QVariant(s->fogHeightLevel); },
        [](const ScenePtr &s, const QVariant &v) { s->fogHeightLevel = v.toFloat(); });
    add("fogBreakMinBrightness",
        [](const ScenePtr &s) { return QVariant(s->fogBreakMinBrightness); },
        [](const ScenePtr &s, const QVariant &v) { s->fogBreakMinBrightness = v.toFloat(); });
    add("fogBreakFalloff", [](const ScenePtr &s) { return QVariant(s->fogBreakFalloff); },
        [](const ScenePtr &s, const QVariant &v) { s->fogBreakFalloff = v.toFloat(); });
    // The retired LINEAR pair — no panel row writes them, but world.fog's
    // `start`/`end` still do, and a verb records through this table too.
    add("fogStart", [](const ScenePtr &s) { return QVariant(s->fogStart); },
        [](const ScenePtr &s, const QVariant &v) { s->fogStart = v.toFloat(); });
    add("fogEnd", [](const ScenePtr &s) { return QVariant(s->fogEnd); },
        [](const ScenePtr &s, const QVariant &v) { s->fogEnd = v.toFloat(); });
    add("shadowEnabled", [](const ScenePtr &s) { return QVariant(s->shadowEnabled); },
        [](const ScenePtr &s, const QVariant &v) { s->shadowEnabled = v.toBool(); });

    // ---- Rayon (the rows the tier does NOT own) ---------------------------
    add("giLightGuid", [](const ScenePtr &s) { return QVariant(s->giLightGuid); },
        [](const ScenePtr &s, const QVariant &v) { s->giLightGuid = v.toString(); });
    add("giBoundsMin", [](const ScenePtr &s) { return vec(s->giBoundsMin); },
        [](const ScenePtr &s, const QVariant &v) { s->giBoundsMin = vec(v); });
    add("giBoundsMax", [](const ScenePtr &s) { return vec(s->giBoundsMax); },
        [](const ScenePtr &s, const QVariant &v) { s->giBoundsMax = vec(v); });
    add("giPccGrid", [](const ScenePtr &s) { return vec(s->giPccGrid); },
        [](const ScenePtr &s, const QVariant &v) { s->giPccGrid = vec(v); });
    add("giUpdateBudget", [](const ScenePtr &s) { return QVariant(s->giUpdateBudget); },
        [](const ScenePtr &s, const QVariant &v) { s->giUpdateBudget = v.toInt(); });
    add("giDdgiIntensity", [](const ScenePtr &s) { return QVariant(s->giDdgiIntensity); },
        [](const ScenePtr &s, const QVariant &v) { s->giDdgiIntensity = v.toFloat(); });
    add("giDdgiAmbient", [](const ScenePtr &s) { return QVariant(s->giDdgiAmbient); },
        [](const ScenePtr &s, const QVariant &v) { s->giDdgiAmbient = v.toFloat(); });
    add("giDdgiSource", [](const ScenePtr &s) { return QVariant(s->giDdgiSource); },
        [](const ScenePtr &s, const QVariant &v) { s->giDdgiSource = v.toInt(); });
    // Verb-only integrator knobs (world.gi): no panel row, but the verb's one
    // undo step records them through this table like every other world field.
    add("giAutoBoundsMax", [](const ScenePtr &s) { return QVariant(s->giAutoBoundsMax); },
        [](const ScenePtr &s, const QVariant &v) { s->giAutoBoundsMax = v.toFloat(); });
    add("giRayMarchStepScale", [](const ScenePtr &s) { return QVariant(s->giRayMarchStepScale); },
        [](const ScenePtr &s, const QVariant &v) { s->giRayMarchStepScale = v.toFloat(); });
    add("giProbeCaptureSize", [](const ScenePtr &s) { return QVariant(s->giProbeCaptureSize); },
        [](const ScenePtr &s, const QVariant &v) { s->giProbeCaptureSize = v.toInt(); });
    add("giProbeHdr", [](const ScenePtr &s) { return QVariant(s->giProbeHdr); },
        [](const ScenePtr &s, const QVariant &v) { s->giProbeHdr = v.toInt(); });
    add("giProbeShadows", [](const ScenePtr &s) { return QVariant(s->giProbeShadows); },
        [](const ScenePtr &s, const QVariant &v) { s->giProbeShadows = v.toInt(); });
    add("giProbeOverlap", [](const ScenePtr &s) { return QVariant(s->giProbeOverlap); },
        [](const ScenePtr &s, const QVariant &v) { s->giProbeOverlap = v.toFloat(); });
    add("giProbeSnapDeviation", [](const ScenePtr &s) { return QVariant(s->giProbeSnapDeviation); },
        [](const ScenePtr &s, const QVariant &v) { s->giProbeSnapDeviation = v.toFloat(); });
    add("giProbeSnapSidesMin", [](const ScenePtr &s) { return QVariant(s->giProbeSnapSidesMin); },
        [](const ScenePtr &s, const QVariant &v) { s->giProbeSnapSidesMin = v.toFloat(); });
    add("giProbeSnapSidesMax", [](const ScenePtr &s) { return QVariant(s->giProbeSnapSidesMax); },
        [](const ScenePtr &s, const QVariant &v) { s->giProbeSnapSidesMax = v.toFloat(); });

    // ---- Post Process: the looks stack ------------------------------------
    add("looks", [](const ScenePtr &s) { return QVariant(s->looks); },
        [](const ScenePtr &s, const QVariant &v) {
            s->looks = iris::normalizeLookStack(v.toJsonArray());
        });

    // ---- Post Process: the continuous parameters --------------------------
    // Generated from the registry, so a new parameter is undoable the day it
    // is declared — and clamped by the registry's own range on the way back in.
    for (const worldmodes::ParamRow &p : worldmodes::postFxParams()) {
        const QString id = p.id;
        f.append({ QStringLiteral("postFx.") + id,
                   [id](const ScenePtr &s) -> QVariant {
                       const worldmodes::ParamRow *r = worldmodes::postFxParam(id);
                       return (r && r->get) ? QVariant(r->get(s)) : QVariant();
                   },
                   [id](const ScenePtr &s, const QVariant &v) {
                       const worldmodes::ParamRow *r = worldmodes::postFxParam(id);
                       if (r && r->set) r->set(s, qBound(r->minValue, v.toDouble(), r->maxValue));
                   } });
    }

    // ---- The sky block ----------------------------------------------------
    add("sky", captureSky, applySky);

    return f;
}

}   // namespace

namespace sceneprops {

const QVector<Field> &fields()
{
    static const QVector<Field> table = buildFields();
    return table;
}

const Field *field(const QString &id)
{
    for (const Field &f : fields())
        if (f.id == id) return &f;
    return nullptr;
}

QStringList ids()
{
    QStringList out;
    for (const Field &f : fields()) out.append(f.id);
    return out;
}

QVariant get(const iris::ScenePtr &scene, const QString &id)
{
    if (!scene) return QVariant();
    const Field *f = field(id);
    return (f && f->get) ? f->get(scene) : QVariant();
}

bool set(const iris::ScenePtr &scene, const QString &id, const QVariant &value)
{
    if (!scene) return false;
    const Field *f = field(id);
    if (!f || !f->set) return false;
    f->set(scene, value);
    return true;
}

}   // namespace sceneprops

ScenePropertyCommand::ScenePropertyCommand(const QString &text, const iris::ScenePtr &scene,
                                           const QString &key, const QVariant &before,
                                           const QVariant &after, QUndoCommand *parent)
    : StudioCommand(parent), mScene(scene), mKey(key), mBefore(before), mAfter(after)
{
    setText(text);
}

void ScenePropertyCommand::apply(const QVariant &value)
{
    auto scene = mScene.lock();
    if (!scene) return;
    sceneprops::set(scene, mKey, value);
    if (mRefresh) mRefresh();
}

void ScenePropertyCommand::undo()
{
    apply(mBefore);
}

void ScenePropertyCommand::redo()
{
    if (mFirstRedo) { mFirstRedo = false; return; }
    apply(mAfter);
}
