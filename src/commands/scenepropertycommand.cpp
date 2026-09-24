/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/scenepropertycommand.h"

#include <QPointer>
#include <QVector>
#include <algorithm>

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QVector3D>

#include "irisgl/core/math/qtinterop.h"
#include "irisgl/document/scenegraph/looks.h"
#include "irisgl/document/scenegraph/scene.h"
#include "services/vrworld.h"
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
    // THE REALISTIC DIALS ARE ALREADY IN `skyData["Realistic"]` (SKY-WRITE-1).
    // They used to be captured a SECOND time as six more keys beside it, which
    // meant a seventh dial added to SkyRealistic and forgotten here would be
    // reverted by any undo of any sky edit — the two-representations hazard,
    // inside the undo blob. One copy, restored through the one writer.
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
    // ...and back out of the block that was just restored, through the one
    // writer, so the typed fields the renderer reads and the JSON the panels
    // bind from are the same fact by construction.
    s->setSkyRealistic(iris::Scene::skyRealisticFromJson(
        s->skyData.value(QStringLiteral("Realistic"))));
}

QVector<sceneprops::Field> buildFields()
{
    QVector<sceneprops::Field> f;
    auto add = [&f](const char *id, sceneprops::Getter get, sceneprops::Setter set) {
        f.append({ QLatin1String(id), std::move(get), std::move(set) });
    };

    // ---- World section ----------------------------------------------------
    // (`ambientColor` is GONE, SKY_LIGHT_SPEC §5: ambient is the Sky Light's
    // intensity and tint, which are LIGHT-NODE properties and go through
    // SetNodePropertyCommand like every other light's rows.)
    // THE SUN PIN and THE SUN DISC — scene-level rows the World panel writes.
    add("sunLight", [](const ScenePtr &s) { return QVariant(s->sunLightGuid); },
        [](const ScenePtr &s, const QVariant &v) { s->sunLightGuid = v.toString(); });
    add("sunDiscVisible", [](const ScenePtr &s) { return QVariant(s->sunDiscVisible); },
        [](const ScenePtr &s, const QVariant &v) { s->sunDiscVisible = v.toBool(); });
    add("sunDiscInProbes", [](const ScenePtr &s) { return QVariant(s->sunDiscInProbes); },
        [](const ScenePtr &s, const QVariant &v) { s->sunDiscInProbes = v.toBool(); });
    // The disc's angular diameter in degrees, clamped where the verb, the
    // reader and the panel row clamp it (iris::kMin/kMaxSunDiscSize).
    add("sunDiscSize", [](const ScenePtr &s) { return QVariant(s->sunDiscSize); },
        [](const ScenePtr &s, const QVariant &v) {
            s->sunDiscSize = float(qBound(double(iris::kMinSunDiscSize), v.toDouble(),
                                          double(iris::kMaxSunDiscSize)));
        });
    // THE CLOUD LAYER (CLOUDS-2D-1), whole — one value, like the sky block, so
    // one gesture or one world.clouds call is one step. The weather map rides
    // beside the block as the PATH of the pixels that were loaded for its guid:
    // this table has no asset resolver, and restoring a guid without its
    // pixels would leave the renderer showing the other map.
    add("clouds", [](const ScenePtr &s) {
            QVariantMap m = s->clouds.toJson().toVariantMap();
            if (s->cloudWeatherMap) m.insert("weatherPath", s->cloudWeatherMap->source);
            return QVariant(m);
        },
        [](const ScenePtr &s, const QVariant &v) {
            const QVariantMap m = v.toMap();
            s->clouds = iris::CloudLayer::fromJson(QJsonObject::fromVariantMap(m));
            const QString path = m.value("weatherPath").toString();
            if (s->clouds.weatherMapGuid.isEmpty() || path.isEmpty())
                s->cloudWeatherMap.reset();
            else if (!s->cloudWeatherMap || s->cloudWeatherMap->source != path)
                s->cloudWeatherMap = iris::Texture2D::load(path, false);
        });
    // HARDWARE RAY TRACING (ledger §425) — the project's own state, as the
    // enum's int. Auto is 0, so a blob that lost the value restores the
    // documented default rather than the most restrictive state; anything
    // outside the three known states reads as Auto for the same reason.
    add("rayTracing", [](const ScenePtr &s) { return QVariant(int(s->rayTracing)); },
        [](const ScenePtr &s, const QVariant &v) {
            const int i = v.toInt();
            s->rayTracing = (i == int(iris::RayTracingMode::Off))  ? iris::RayTracingMode::Off
                          : (i == int(iris::RayTracingMode::On))   ? iris::RayTracingMode::On
                                                                   : iris::RayTracingMode::Auto;
        });
    // HARD SUN CONTACT SHADOWS (PHOTON-RAYS-1), whole — one value, so one
    // world.sunContact call is one undo step.
    add("sunContact", [](const ScenePtr &s) { return QVariant(s->sunContact.toJson().toVariantMap()); },
        [](const ScenePtr &s, const QVariant &v) {
            s->sunContact = iris::SunContact::fromJson(QJsonObject::fromVariantMap(v.toMap()));
        });
    // THE SCREEN-SPACE MARCH'S PHASE RULE (SSR-RINGS-1), 0..2.
    add("ssrMarch", [](const ScenePtr &s) { return QVariant(s->ssrMarch); },
        [](const ScenePtr &s, const QVariant &v) { s->ssrMarch = qBound(0, v.toInt(), 2); });
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
    add("fogAtmosphere", [](const ScenePtr &s) { return QVariant(s->fogAtmosphere); },
        [](const ScenePtr &s, const QVariant &v) { s->fogAtmosphere = v.toBool(); });
    add("fogHeightFalloff", [](const ScenePtr &s) { return QVariant(s->fogHeightFalloff); },
        [](const ScenePtr &s, const QVariant &v) { s->fogHeightFalloff = v.toFloat(); });
    add("fogHeightLevel", [](const ScenePtr &s) { return QVariant(s->fogHeightLevel); },
        [](const ScenePtr &s, const QVariant &v) { s->fogHeightLevel = v.toFloat(); });
    add("fogBreakMinBrightness",
        [](const ScenePtr &s) { return QVariant(s->fogBreakMinBrightness); },
        [](const ScenePtr &s, const QVariant &v) { s->fogBreakMinBrightness = v.toFloat(); });
    add("fogBreakFalloff", [](const ScenePtr &s) { return QVariant(s->fogBreakFalloff); },
        [](const ScenePtr &s, const QVariant &v) { s->fogBreakFalloff = v.toFloat(); });
    add("shadowEnabled", [](const ScenePtr &s) { return QVariant(s->shadowEnabled); },
        [](const ScenePtr &s, const QVariant &v) { s->shadowEnabled = v.toBool(); });

    // ---- Photon (the rows the tier does NOT own) ---------------------------
    add("giCascadeInstanceCap", [](const ScenePtr &s) { return QVariant(s->giCascadeInstanceCap); },
        [](const ScenePtr &s, const QVariant &v) { s->giCascadeInstanceCap = v.toInt(); });
    add("giDragMoverChannel", [](const ScenePtr &s) { return QVariant(s->giDragMoverChannel); },
        [](const ScenePtr &s, const QVariant &v) { s->giDragMoverChannel = v.toInt() ? 1 : 0; });
    add("giPccGrid", [](const ScenePtr &s) { return vec(s->giPccGrid); },
        [](const ScenePtr &s, const QVariant &v) { s->giPccGrid = vec(v); });
    add("giUpdateBudget", [](const ScenePtr &s) { return QVariant(s->giUpdateBudget); },
        [](const ScenePtr &s, const QVariant &v) { s->giUpdateBudget = v.toInt(); });
    add("giDdgiIntensity", [](const ScenePtr &s) { return QVariant(s->giDdgiIntensity); },
        [](const ScenePtr &s, const QVariant &v) { s->giDdgiIntensity = v.toFloat(); });
    // THE SCREEN-PROBE GATHER's row (GATHER-1a). Verb-only like the two above
    // it, and in this table for the same reason: a world field the verb writes
    // outside WorldEdit is not undoable and is not rolled back when a later key
    // of the same call is refused.
    add("giGather", [](const ScenePtr &s) { return QVariant(s->giGather); },
        [](const ScenePtr &s, const QVariant &v) { s->giGather = v.toInt(); });
    add("giCards", [](const ScenePtr &s) { return QVariant(s->giCards); },
        [](const ScenePtr &s, const QVariant &v) { s->giCards = v.toInt(); });
    add("giCardBudgetTexels", [](const ScenePtr &s) { return QVariant(s->giCardBudgetTexels); },
        [](const ScenePtr &s, const QVariant &v) { s->giCardBudgetTexels = v.toInt(); });
    add("giCardRadius", [](const ScenePtr &s) { return QVariant(s->giCardRadius); },
        [](const ScenePtr &s, const QVariant &v) { s->giCardRadius = v.toFloat(); });
    // Verb-only integrator knobs (world.gi): no panel row, but the verb's one
    // undo step records them through this table like every other world field.
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

    // ---- VR (lane VR-WORLD-1) ---------------------------------------------
    // Generated from the VR table, exactly as the post-process parameters below
    // are: a setting is undoable the day it is declared, and the write goes
    // through the table's own setter — the identical function `world.vr` calls.
    for (const vrworld::Row &vr : vrworld::rows()) {
        const QString id = vr.id;
        f.append({ vrworld::propsKey(id),
                   [id](const ScenePtr &s) -> QVariant {
                       const vrworld::Row *r = vrworld::row(id);
                       return (r && r->get) ? QVariant(r->get(s)) : QVariant();
                   },
                   [id](const ScenePtr &s, const QVariant &v) {
                       const vrworld::Row *r = vrworld::row(id);
                       if (r && r->set) r->set(s, v.toDouble());
                   } });
    }

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

namespace {
struct Observer
{
    QPointer<QObject> context;
    WriteObserver fn;
};
QVector<Observer> &observers()
{
    static QVector<Observer> list;
    return list;
}
}   // namespace

void observeWrites(QObject *context, WriteObserver observer)
{
    if (!context || !observer) return;
    observers().append({ QPointer<QObject>(context), std::move(observer) });
}

bool set(const iris::ScenePtr &scene, const QString &id, const QVariant &value)
{
    if (!scene) return false;
    const Field *f = field(id);
    if (!f || !f->set) return false;
    f->set(scene, value);
    // A COPY is walked: an observer may add another (a panel built in its
    // callback), and a destroyed context is pruned here, not by the caller.
    auto &list = observers();
    list.erase(std::remove_if(list.begin(), list.end(),
                              [](const Observer &o) { return o.context.isNull(); }),
               list.end());
    const QVector<Observer> now = list;
    for (const Observer &o : now)
        if (o.context) o.fn(scene, id);
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
