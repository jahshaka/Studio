/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/vec.h"
#include "scripting/modules/worldapi.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include "scripting/modules/moduleshared.h"
#include "data/database/database.h"
#include "services/assetcas.h"
#include "services/assetstorepaths.h"
#include <QSqlDatabase>
#include "data/project.h"
#include "io/scenewriter.h"
#include "shell/mainwindow.h"
#include "services/sceneeditservice.h"
#include "services/services.h"
#include "viewport/ieditorviewport.h"
#include "irisgl/document/assets/texture2d.h"
#include "services/worldmodes.h"

using namespace scriptmod;

QVector<VerbInfo> WorldApi::verbs() const
{
    return {
        { "ambient", "world.ambient(color) -> bool",
          "Sets the ambient light colour (\"#rrggbb\" or {r,g,b}).",
          Needs::Document },
        { "gravity", "world.gravity(value) -> bool",
          "Sets world gravity (drives the physics world too).",
          Needs::Document },
        { "fog", "world.fog({enabled, color, density, heightDensity, heightFalloff, heightLevel, breakMinBrightness, breakFalloff, end, start}) -> bool",
          "Sets any subset of the fog settings. Fog is EXPONENTIAL: transmittance = 2^(-distance * density), "
          "so density is the loss per world unit (a surface 1/density away keeps half its colour). "
          "heightDensity > 0 adds a second layer of the same colour whose density falls off with world Y "
          "(density(y) = heightDensity * 2^(-(y - heightLevel) * heightFalloff)). breakMinBrightness/"
          "breakFalloff let bright pixels resist the fog (breakFalloff 0 = pure exponential). "
          "`end` is the retired linear \"fully fogged\" distance, kept as a convenience: setting it "
          "re-derives the density from the start/end pair (density = 2/(start+end), the distance where "
          "both curves are half fogged). `start` no longer affects rendering on its own.",
          Needs::Document },
        { "shadows", "world.shadows({enabled}) -> bool",
          "Toggles shadow rendering.",
          Needs::Document },
        { "gi", "world.gi({mode, quality, bounces, light, boundsMin, boundsMax, pccGrid, autoRefresh}) -> bool",
          "Global illumination: mode off|instant_radiosity|vct|vct_pcc_hybrid, quality low|medium|high, bounces 1-4, light = driving light guid ('' = auto, instant_radiosity only), boundsMin/boundsMax = lit volume corners (equal = fit the scene), pccGrid = {x,y,z} reflection-probe counts 1-8 per axis (hybrid only).",
          Needs::Document },
        { "antiAliasing", "world.antiAliasing() -> int",
          "Reads the anti-aliasing (MSAA) sample count. With the engine viewport live this is the ACHIEVED count (the driver may clamp the request); otherwise the scene's requested value.",
          Needs::Document },
        { "setAntiAliasing", "world.setAntiAliasing(samples) -> int",
          "Sets the scene's anti-aliasing: 1 (off), 2, 4 or 8 MSAA samples. Returns the achieved sample count (the driver may clamp; with no engine viewport, the requested value).",
          Needs::Document },
        { "shadowResolution", "world.shadowResolution() -> int",
          "Reads the shadow-map atlas base resolution in pixels. With the engine viewport live this is the value the renderer is actually using; otherwise the scene's setting, or 0 when it is on Auto with no shadow-casting light to derive from.",
          Needs::Document },
        { "setShadowResolution", "world.setShadowResolution(pixels) -> int",
          "Sets the scene's shadow-map atlas base resolution: 0 = Auto (derive from the largest per-light Shadow Size), otherwise 256..8192 pixels. There is ONE atlas for every light in the scene, sized R x 3.5R at 32-bit depth: 1024 costs ~14 MB, 2048 ~56 MB, 4096 ~224 MB, 8192 ~896 MB of VRAM. Returns the applied value after clamping.",
          Needs::Document },
        { "ambientFromSky", "world.ambientFromSky(enabled) -> bool",
          "Sky-driven ambient light: when on (the default), the ambient hemisphere colours are integrated from the live sky (equirect, gradient, realistic or cubemap) instead of being the flat Ambient Color; the Ambient Color then becomes the per-channel strength/tint of that sky ambient (white = full strength, black = none). Single-colour skies always use the flat colour.",
          Needs::Document },
        { "planarReflections", "world.planarReflections() -> {enabled, budget, resolution, shadows, activeActors}",
          "Reads the scene's planar-reflection settings. 'budget' is how many mirror planes may render (the resolved value: a scene that never set one follows its World Mode). 'resolution' and 'shadows' are the per-plane render-target size and whether shadows are drawn inside the reflections; both report the value in force, derived from the budget when the scene has not pinned them. 'activeActors' is how many planes ACTUALLY rendered in the last frame — planes off screen are culled — and is 0 without a live engine viewport. Individual objects become mirror planes through node.setPlanarReflector.",
          Needs::Document },
        { "setPlanarReflections", "world.setPlanarReflections({budget, resolution, shadows}) -> object",
          "Sets any subset of the planar-reflection settings and returns the new state, as in world.planarReflections(). budget: 0 (off) to 8, or -1 / \"auto\" to follow the World Mode; EACH ACTIVE PLANE IS A WHOLE EXTRA SCENE RENDER EVERY FRAME, and changing the budget recompiles the PBR shaders (expect a pause on the next frame). resolution: 256..2048, or 0 / \"auto\" to follow the budget (1024 from 2 planes up, 512 below). shadows: true/false, or \"auto\" to follow the budget (on from 2 planes up); shadows inside reflections cost a private half-resolution shadow atlas PER PLANE. An explicit value is pinned and survives World Mode switches, exactly like world.override.",
          Needs::Document },
        { "sky", "world.sky(type, {...}) -> bool",
          "Sets the sky. Types: color {color}; gradient {top, mid, bottom, offset}; realistic {luminance, reileigh, mieCoefficient, mieDirectionalG, turbidity, azimuth, elevation | sunPosX, sunPosY, sunPosZ, detail}; equirectangular {texture}; cubemap {front, back, left, right, top, bottom} (textures = asset guids or file names in the project). For the realistic sky, azimuth (degrees clockwise from +Z) and elevation (degrees above the horizon) are the readable way to place the sun and win over raw sunPos*; turbidity is Preetham's 1..20 haze; detail is the equirect bake width (256, 512 or 1024).",
          Needs::Document },
        { "get", "world.get() -> {ambient, gravity, fog, shadows, gi, sky, mode, settings}",
          "Reads the current world settings.",
          Needs::Document },
        { "mode", "world.mode({mode}) -> string",
          "The scene's World Mode — the scalability tier every quality row resolves through: low, medium, high, epic, or custom (no tier; the individual settings are the truth). Called with no argument it reads the current mode; with {mode: \"high\"} it applies that tier, writing each row's tier value into the scene EXCEPT rows the user pinned with world.override (pins survive mode switches). Returns the resulting mode.",
          Needs::Document },
        { "settings", "world.settings() -> { rowId: {value, valueId, label, source, tierValue, available} }",
          "Every World Mode row and its RESOLVED value. 'source' is \"override\" (pinned by the user), \"mode\" (from the tier) or \"custom\" (no tier is applied). 'valueId' is the script-facing spelling the override verb takes; 'tierValue' is what the current mode would give the row; 'available' is false for rows declared but not yet implemented by the renderer.",
          Needs::Document },
        { "override", "world.override({id, value}) -> object",
          "Pins one quality row to a value, whatever the mode says: world.override({id: \"msaa\", value: \"4x\"}). Values may be given as the row's id spelling (\"4x\", \"vct\", \"off\") or as the raw number. The pin survives mode switches until world.clearOverride drops it. Returns the row's new state, as in world.settings().",
          Needs::Document },
        { "clearOverride", "world.clearOverride({id}) -> object",
          "Drops one pinned row and puts the current mode's value back. Returns the row's new state.",
          Needs::Document },
        { "clearOverrides", "world.clearOverrides() -> object",
          "Drops every pinned row and re-applies the current mode. Returns world.settings().",
          Needs::Document },
        { "postFx", "world.postFx({exposure, bloomThreshold, ssaoPower, ssaoRadius}) -> object",
          "The post chain's CONTINUOUS tuning, as opposed to its on/off rows (those are World Mode rows — world.override). exposure is the auto-exposure midpoint, used as e^(exposure-2), so +0.69 is one doubling; bloomThreshold is where the bright pass starts, in tonemapper units (high reads as highlight bloom, low as haze); ssaoPower is the contrast of the occlusion term and ssaoRadius how far it looks, in metres. Called with no argument it reads them.",
          Needs::Document },
        { "modeTable", "world.modeTable() -> object",
          "The World Mode registry itself: every row's id, label, group, type, options, per-tier values, cost note and availability. This is what the World panel and the docs are generated from.",
          Needs::Document },
    };
}

iris::ScenePtr WorldApi::sceneOrFail(const QString &verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene() : iris::ScenePtr();
    if (!scene) fail(QStringLiteral("%1: no scene is open").arg(verb));
    return scene;
}

// F8 (AI_SURFACE_AUDIT): every colour argument on this module used to keep the
// scene's old value and answer `true` when it could not be parsed. They refuse
// now — one shared sentence (scriptmod::colorHelp) says what IS accepted.
bool WorldApi::ambient(const QVariant &color)
{
    auto scene = sceneOrFail(QStringLiteral("world.ambient"));
    if (!scene) return false;
    // An ABSENT argument keeps the current colour, as it always has — only a
    // value that was GIVEN and not understood is refused.
    if (!color.isValid() || color.isNull()) return true;
    bool ok = false;
    const QColor c = colorFromJs(color, scene->ambientColor, &ok);
    if (!ok) return fail(QStringLiteral("world.ambient: %1").arg(colorHelp(color)));
    scene->setAmbientColor(c);
    return true;
}

bool WorldApi::gravity(double value)
{
    auto scene = sceneOrFail(QStringLiteral("world.gravity"));
    if (!scene) return false;
    scene->setWorldGravity(float(value));   // the setter drives the Bullet world too
    return true;
}

bool WorldApi::fog(const QVariantMap &params)
{
    auto scene = sceneOrFail(QStringLiteral("world.fog"));
    if (!scene) return false;
    if (params.contains("enabled")) scene->fogEnabled = params.value("enabled").toBool();
    if (params.contains("color")) {
        bool ok = false;
        const QColor c = colorFromJs(params.value("color"), scene->fogColor, &ok);
        if (!ok) return fail(QStringLiteral("world.fog: %1").arg(colorHelp(params.value("color"))));
        scene->fogColor = c;
    }
    if (params.contains("start"))   scene->fogStart = params.value("start").toFloat();
    // `end` is the retired linear pair's far distance. It still sets the density
    // (that is the whole migration story), so a script written against the linear
    // fog keeps producing fog that looks the same.
    if (params.contains("end")) {
        scene->fogEnd = params.value("end").toFloat();
        scene->fogDensity = iris::Scene::fogDensityFromLinear(scene->fogStart, scene->fogEnd);
    }
    if (params.contains("density"))       scene->fogDensity = params.value("density").toFloat();
    if (params.contains("heightDensity")) scene->fogHeightDensity = params.value("heightDensity").toFloat();
    if (params.contains("heightFalloff")) scene->fogHeightFalloff = params.value("heightFalloff").toFloat();
    if (params.contains("heightLevel"))   scene->fogHeightLevel = params.value("heightLevel").toFloat();
    if (params.contains("breakMinBrightness"))
        scene->fogBreakMinBrightness = params.value("breakMinBrightness").toFloat();
    if (params.contains("breakFalloff")) scene->fogBreakFalloff = params.value("breakFalloff").toFloat();
    return true;
}

bool WorldApi::shadows(const QVariantMap &params)
{
    auto scene = sceneOrFail(QStringLiteral("world.shadows"));
    if (!scene) return false;
    if (params.contains("enabled")) scene->shadowEnabled = params.value("enabled").toBool();
    return true;
}

bool WorldApi::gi(const QVariantMap &params)
{
    auto scene = sceneOrFail(QStringLiteral("world.gi"));
    if (!scene) return false;

    if (params.contains("mode")) {
        const QString m = params.value("mode").toString().trimmed().toLower();
        if (m == "off")                    scene->giMode = iris::GiMode::OFF;
        else if (m == "instant_radiosity") scene->giMode = iris::GiMode::INSTANT_RADIOSITY;
        else if (m == "vct")               scene->giMode = iris::GiMode::VCT;
        else if (m == "vct_pcc_hybrid")    scene->giMode = iris::GiMode::VCT_PCC_HYBRID;
        else return fail(QStringLiteral("world.gi: unknown mode '%1' (off, instant_radiosity, vct, vct_pcc_hybrid)").arg(m));
        worldmodes::pinRowValue(scene, QStringLiteral("giMode"), int(scene->giMode));
    }
    if (params.contains("quality")) {
        const QString q = params.value("quality").toString().trimmed().toLower();
        if (q == "low")         scene->giQuality = iris::GiQuality::LOW;
        else if (q == "medium") scene->giQuality = iris::GiQuality::MEDIUM;
        else if (q == "high")   scene->giQuality = iris::GiQuality::HIGH;
        else return fail(QStringLiteral("world.gi: unknown quality '%1' (low, medium, high)").arg(q));
        worldmodes::pinRowValue(scene, QStringLiteral("giQuality"), int(scene->giQuality));
    }
    if (params.contains("bounces"))
        scene->giNumBounces = qBound(1, params.value("bounces").toInt(), 4);
    if (params.contains("light"))
        scene->giLightGuid = params.value("light").toString();
    if (params.contains("boundsMin"))
        scene->giBoundsMin = vecFromJs(params.value("boundsMin"), scene->giBoundsMin);
    if (params.contains("boundsMax"))
        scene->giBoundsMax = vecFromJs(params.value("boundsMax"), scene->giBoundsMax);
    if (params.contains("pccGrid")) {
        const iris::Vec3 g = vecFromJs(params.value("pccGrid"), scene->giPccGrid);
        scene->giPccGrid = iris::Vec3(qBound(1, qRound(g.x()), 8), qBound(1, qRound(g.y()), 8),
                                     qBound(1, qRound(g.z()), 8));
    }
    if (params.contains("autoRefresh"))
        scene->giAutoRefresh = params.value("autoRefresh").toBool();
    return true;
}

int WorldApi::antiAliasing()
{
    auto scene = sceneOrFail(QStringLiteral("world.antiAliasing"));
    if (!scene) return 0;
    // Achieved beats requested when there is a live viewport to ask: the driver
    // may have clamped (Vulkan only guarantees 1x and 4x).
    if (host.isEngineReady() && host.viewport) return host.viewport->sampleCount();
    return scene->antiAliasing;
}

int WorldApi::setAntiAliasing(int samples)
{
    auto scene = sceneOrFail(QStringLiteral("world.setAntiAliasing"));
    if (!scene) return 0;
    if (samples != 1 && samples != 2 && samples != 4 && samples != 8) {
        fail(QStringLiteral("world.setAntiAliasing: samples must be 1 (off), 2, 4 or 8"));
        return 0;
    }
    scene->antiAliasing = samples;
    // A direct edit of a backing field is a PIN (POST_CHAIN_SPEC §9.1): without
    // this the next world.mode() switch would silently undo it.
    worldmodes::pinRowValue(scene, QStringLiteral("msaa"), samples);
    // SceneMirror pushes the document value at the next sync; step two frames so
    // the pending target rebuild is applied and the achieved count is readable.
    if (host.isEngineReady() && host.viewport) {
        host.viewport->renderFrames(2);
        return host.viewport->sampleCount();
    }
    return samples;
}

int WorldApi::shadowResolution()
{
    auto scene = sceneOrFail(QStringLiteral("world.shadowResolution"));
    if (!scene) return 0;
    // Like world.antiAliasing(): the live renderer beats the request, because
    // Auto (0) resolves against the light list and the engine clamps.
    if (host.isEngineReady() && host.viewport) {
        const int live = host.viewport->shadowResolution();
        if (live > 0) return live;
    }
    return scene->shadowResolution;
}

int WorldApi::setShadowResolution(int pixels)
{
    auto scene = sceneOrFail(QStringLiteral("world.setShadowResolution"));
    if (!scene) return 0;
    if (pixels < 0) {
        fail(QStringLiteral("world.setShadowResolution: pixels must be 0 (Auto) or 256..8192"));
        return 0;
    }
    if (pixels == 0) {
        scene->shadowResolution = 0;
    } else {
        // Non-fatal guard-rails: the value still applies (scripts are allowed
        // the full engine window), but a 4096+ atlas is a VRAM decision the
        // caller should see. The editor UI caps at 4096 instead.
        if (pixels < 256 || pixels > 8192)
            qWarning("world.setShadowResolution: %d clamped to the renderer's 256..8192 window", pixels);
        else if (pixels > 4096)
            qWarning("world.setShadowResolution: %d allocates roughly %d MB of VRAM for the shadow "
                     "atlas; the editor UI caps at 4096",
                     pixels, int(qRound(4.0 * 3.5 * double(pixels) * double(pixels) / (1024.0 * 1024.0))));
        // The shadow atlas is 3.5x the resolution in height. 8192 -> 28672 rows,
        // which Metal hard-aborts on (16384 max texture dimension; NVIDIA's 32768
        // absorbed it). Cap per platform until the engine exposes its real max
        // texture size (recorded debt: capability query on the Engine interface).
#ifdef Q_OS_MACOS
        scene->shadowResolution = qBound(256, pixels, 4096);
#else
        scene->shadowResolution = qBound(256, pixels, 8192);
#endif
    }
    worldmodes::pinRowValue(scene, QStringLiteral("shadowResolution"), scene->shadowResolution);
    // SceneMirror pushes the document value at the next sync; step a frame so
    // the atlas rebuild lands and the readback below is the applied truth.
    if (host.isEngineReady() && host.viewport) {
        host.viewport->renderFrames(2);
        const int live = host.viewport->shadowResolution();
        if (scene->shadowResolution > 0 && live > 0) return live;
    }
    return scene->shadowResolution;
}

bool WorldApi::ambientFromSky(bool enabled)
{
    auto scene = sceneOrFail(QStringLiteral("world.ambientFromSky"));
    if (!scene) return false;
    scene->ambientFromSky = enabled;
    worldmodes::pinRowValue(scene, QStringLiteral("ambientFromSky"), enabled ? 1 : 0);
    return true;
}

// ---------------------------------------------------------------------------
// Planar reflections (PLANAR_REFLECTIONS_SPEC.md §8).
//
// The BUDGET is a World Mode row (worldmodes "planarBudget"), so its resolved
// value comes from the registry and an explicit set pins it exactly as
// world.override would. Resolution and shadows are per-scene only: they follow
// the budget unless pinned, which is how the mode tiers reach them without two
// more dials in the World panel (see SceneMirror::applyEnvironment, which owns
// the same derivation on the engine side — this verb must agree with it).
namespace {

/// The budget-driven defaults SceneMirror applies. Kept in one place here so
/// the verb reports what the renderer will actually do, not what the document
/// literally stores.
int autoPlanarResolution(int budget) { return budget >= 2 ? 1024 : 512; }
bool autoPlanarShadows(int budget)   { return budget >= 2; }

/// Accepts a number, or "auto"/"" for the follow-the-mode sentinel. Returns
/// false when the value is neither.
bool planarAuto(const QVariant &v)
{
    if (v.typeId() == QMetaType::QString) {
        const QString s = v.toString().trimmed().toLower();
        return s == QLatin1String("auto") || s.isEmpty();
    }
    return false;
}

}   // namespace

QVariantMap WorldApi::planarReflections()
{
    auto scene = sceneOrFail(QStringLiteral("world.planarReflections"));
    if (!scene) return QVariantMap();
    const worldmodes::Row *row = worldmodes::row(QStringLiteral("planarBudget"));
    const int budget = row ? worldmodes::resolved(scene, *row) : 0;
    const int resolution = scene->planarReflectionResolution > 0
                               ? scene->planarReflectionResolution
                               : autoPlanarResolution(budget);
    const bool shadows = scene->planarReflectionShadows >= 0
                             ? scene->planarReflectionShadows != 0
                             : autoPlanarShadows(budget);
    // The ACHIEVED count, like world.antiAliasing() and world.shadowResolution():
    // the renderer culls planes that are off screen, so this is usually lower
    // than the budget and that is the point of reporting it.
    const int active = (host.isEngineReady() && host.viewport)
                           ? host.viewport->activePlanarReflectors() : 0;
    return QVariantMap{ { QStringLiteral("enabled"), budget > 0 },
                        { QStringLiteral("budget"), budget },
                        { QStringLiteral("resolution"), resolution },
                        { QStringLiteral("shadows"), shadows },
                        { QStringLiteral("activeActors"), active } };
}

QVariantMap WorldApi::setPlanarReflections(const QVariantMap &params)
{
    auto scene = sceneOrFail(QStringLiteral("world.setPlanarReflections"));
    if (!scene) return QVariantMap();

    if (params.contains(QStringLiteral("budget"))) {
        const QVariant v = params.value(QStringLiteral("budget"));
        if (planarAuto(v)) {
            // "Follow the mode" = drop the pin and let the tier write through.
            // In Custom mode there is no tier to fall back to, so the sentinel
            // goes back into the field itself.
            worldmodes::clearOverride(scene, QStringLiteral("planarBudget"));
            if (worldmodes::mode(scene) == worldmodes::Mode::Custom)
                scene->planarReflectionBudget = -1;
        } else {
            bool ok = false;
            const int b = v.toInt(&ok);
            if (!ok || b < -1 || b > 8) {
                fail(QStringLiteral("world.setPlanarReflections: budget must be 0..8, or -1/\"auto\" "
                                    "to follow the World Mode"));
                return QVariantMap();
            }
            if (b < 0) {
                worldmodes::clearOverride(scene, QStringLiteral("planarBudget"));
                if (worldmodes::mode(scene) == worldmodes::Mode::Custom)
                    scene->planarReflectionBudget = -1;
            } else {
                // setRowValue writes the field AND records the pin, so the value
                // survives a later mode switch — the same contract world.override
                // gives every other row.
                worldmodes::setRowValue(scene, QStringLiteral("planarBudget"), b);
            }
        }
    }

    if (params.contains(QStringLiteral("resolution"))) {
        const QVariant v = params.value(QStringLiteral("resolution"));
        if (planarAuto(v)) {
            scene->planarReflectionResolution = 0;
        } else {
            bool ok = false;
            const int r = v.toInt(&ok);
            if (!ok || (r != 0 && (r < 256 || r > 2048))) {
                fail(QStringLiteral("world.setPlanarReflections: resolution must be 256..2048, or "
                                    "0/\"auto\" to follow the budget"));
                return QVariantMap();
            }
            scene->planarReflectionResolution = r;
        }
    }

    if (params.contains(QStringLiteral("shadows"))) {
        const QVariant v = params.value(QStringLiteral("shadows"));
        if (planarAuto(v)) scene->planarReflectionShadows = -1;
        else               scene->planarReflectionShadows = v.toBool() ? 1 : 0;
    }

    // Step the viewport so activeActors in the returned state is the truth
    // rather than the previous frame's — the arm is rebuilt at the next sync.
    if (host.isEngineReady() && host.viewport) host.viewport->renderFrames(2);
    return planarReflections();
}

bool WorldApi::resolveTexture(const QVariant &ref, QString &guidOut, QString &pathOut)
{
    if (!host.db || !host.project) return false;
    const QString value = ref.toString();
    if (value.isEmpty()) return false;

    // guid first (assets are guid-keyed), then by file name like the sky panel
    QString guid;
    if (!host.db->fetchAsset(value).guid.isEmpty()) guid = value;
    else guid = host.db->fetchAssetGUIDByName(QFileInfo(value).fileName(), host.project->getProjectGuid());
    if (guid.isEmpty()) return false;

    // Pin-first through the CAS (the flat projectFolder copy died with the
    // asset pipeline — joining it resolved NOTHING for pinned textures, which
    // silently broke world.sky's equirect/cubemap for every imported image;
    // found building the Showroom sample, 2026-09-03). Same ladder as
    // materialpropertywidget.cpp.
    QSqlDatabase conn = QSqlDatabase::database();
    QString path = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                           host.project->getProjectGuid(), guid);
    if (path.isEmpty())
        path = AssetCas::resolveSource(conn, AssetStorePaths::root(), guid);
    if (path.isEmpty())
        path = QDir(host.project->getProjectFolder())
                   .filePath(host.db->fetchAsset(guid).name);
    if (!QFileInfo::exists(path)) return false;
    guidOut = guid;
    pathOut = path;
    return true;
}

bool WorldApi::sky(const QString &type, const QVariantMap &params)
{
    auto scene = sceneOrFail(QStringLiteral("world.sky"));
    if (!scene) return false;

    const QString t = type.trimmed().toLower();

    // Contract per WorldSkyPropertyWidget: set the live fields AND rebuild
    // scene->skyData[<key>] (SceneWriter serializes only skyData), then
    // switchSkyTexture + queueSkyCapture for the legacy renderer. SceneMirror
    // polls the fields, so the engine picks everything up next frame.
    if (t == "color" || t == "singlecolor") {
        // As everywhere else: omitting the colour keeps the scene's, a colour
        // that was given and cannot be read is refused.
        QColor c = scene->skyColor;
        if (params.contains("color")) {
            bool ok = false;
            c = colorFromJs(params.value("color"), scene->skyColor, &ok);
            if (!ok) return fail(QStringLiteral("world.sky: %1").arg(colorHelp(params.value("color"))));
        }
        scene->skyColor = c;
        QJsonObject def;
        def.insert("skyColor", SceneWriter::jsonColor(c));
        scene->skyData.insert("SingleColor", def);
        scene->skyType = iris::SkyType::SINGLE_COLOR;
    } else if (t == "gradient") {
        struct { const char *key; QColor *field; } stops[] = {
            { "top",    &scene->gradientTop },
            { "mid",    &scene->gradientMid },
            { "bottom", &scene->gradientBot },
        };
        for (const auto &stop : stops) {
            const QString key = QString::fromLatin1(stop.key);
            if (!params.contains(key)) continue;   // omitted = keep this stop
            const QVariant given = params.value(key);
            bool ok = false;
            const QColor c = colorFromJs(given, *stop.field, &ok);
            if (!ok)
                return fail(QStringLiteral("world.sky: %1 (gradient stop '%2')")
                                .arg(colorHelp(given), key));
            *stop.field = c;
        }
        if (params.contains("offset")) scene->gradientOffset = params.value("offset").toFloat();
        QJsonObject def;
        def.insert("gradientTop", SceneWriter::jsonColor(scene->gradientTop));
        def.insert("gradientMid", SceneWriter::jsonColor(scene->gradientMid));
        def.insert("gradientBot", SceneWriter::jsonColor(scene->gradientBot));
        def.insert("gradientOffset", double(scene->gradientOffset));
        scene->skyData.insert("Gradient", def);
        scene->skyType = iris::SkyType::GRADIENT;
    } else if (t == "realistic") {
        auto &r = scene->skyRealistic;
        auto take = [&params](const char *key, float current) {
            return params.contains(key) ? params.value(key).toFloat() : current;
        };
        r.luminance = take("luminance", r.luminance);
        r.reileigh = take("reileigh", r.reileigh);
        r.mieCoefficient = take("mieCoefficient", r.mieCoefficient);
        r.mieDirectionalG = take("mieDirectionalG", r.mieDirectionalG);
        r.turbidity = take("turbidity", r.turbidity);
        r.sunPosX = take("sunPosX", r.sunPosX);
        r.sunPosY = take("sunPosY", r.sunPosY);
        r.sunPosZ = take("sunPosZ", r.sunPosZ);
        // azimuth/elevation are the readable spelling of the same three floats
        // and win over raw sunPos* when both are given (VISUAL_PARITY item 1).
        if (params.contains("azimuth") || params.contains("elevation"))
            r.setSunAngles(take("azimuth", r.sunAzimuth()), take("elevation", r.sunElevation()));
        if (params.contains("detail")) {
            const int d = params.value("detail").toInt();
            scene->skyBakeResolution = d >= 1024 ? 1024 : d >= 512 ? 512 : 256;
            worldmodes::pinRowValue(scene, QStringLiteral("skyBakeResolution"),
                                    scene->skyBakeResolution);
        }
        QJsonObject def;
        def.insert("luminance", double(r.luminance));
        def.insert("reileigh", double(r.reileigh));
        def.insert("mieCoefficient", double(r.mieCoefficient));
        def.insert("mieDirectionalG", double(r.mieDirectionalG));
        def.insert("turbidity", double(r.turbidity));
        def.insert("sunPosX", double(r.sunPosX));
        def.insert("sunPosY", double(r.sunPosY));
        def.insert("sunPosZ", double(r.sunPosZ));
        scene->skyData.insert("Realistic", def);
        scene->skyType = iris::SkyType::REALISTIC;
    } else if (t == "equirectangular" || t == "equirect") {
        if (!requireProject()) return false;   // texture resolution needs the project folder
        QString guid, path;
        if (!resolveTexture(params.value("texture"), guid, path))
            return fail("world.sky: 'texture' must be a texture asset guid or file name in the project");
        scene->setSkyTexture(iris::Texture2D::load(path, false));
        QJsonObject def;
        def.insert("equiSkyGuid", guid);
        scene->skyData.insert("Equirectangular", def);
        scene->skyType = iris::SkyType::EQUIRECTANGULAR;
        // dependency bookkeeping, exactly like the sky panel
        host.db->removeDependenciesByType(scene->skyGuid, ModelTypes::Texture);
        host.db->createDependency(static_cast<int>(ModelTypes::Sky), static_cast<int>(ModelTypes::Texture),
                                  scene->skyGuid, guid, host.project->getProjectGuid());
    } else if (t == "cubemap") {
        if (!requireProject()) return false;
        static const char *faces[] = { "front", "back", "left", "right", "top", "bottom" };
        QMap<QString, QString> guids, paths;
        // F9 (AI_SURFACE_AUDIT): a face that was GIVEN but could not be
        // resolved used to be dropped without a word — the cubemap came back
        // with a hole and world.sky still answered true. A face that is simply
        // absent is still fine (a partial cubemap is legal).
        for (const char *face : faces) {
            QString guid, path;
            if (!params.contains(face)) { guids[face] = QString(); continue; }
            if (!resolveTexture(params.value(face), guid, path))
                return fail(QStringLiteral(
                                "world.sky: cubemap face '%1' — '%2' is not a texture asset "
                                "guid or a file name in the project (assets.list({type:\"texture\"}) "
                                "lists them)")
                                .arg(QString::fromLatin1(face),
                                     params.value(face).toString()));
            guids[face] = guid;
            paths[face] = path;
        }
        if (paths.isEmpty())
            return fail("world.sky: cubemap needs at least one face texture (front/back/left/right/top/bottom)");
        QImage info(paths.first());
        scene->setSkyTexture(iris::Texture2D::createCubeMap(
            paths.value("front"), paths.value("back"),
            paths.value("top"), paths.value("bottom"),
            paths.value("left"), paths.value("right"), &info));
        QJsonObject def;
        for (const char *face : faces) def.insert(face, guids.value(face));
        scene->skyData.insert("Cubemap", def);
        scene->skyType = iris::SkyType::CUBEMAP;
        host.db->removeDependenciesByType(scene->skyGuid, ModelTypes::Texture);
        for (const char *face : faces) {
            if (!guids.value(face).isEmpty())
                host.db->createDependency(static_cast<int>(ModelTypes::Sky), static_cast<int>(ModelTypes::Texture),
                                          scene->skyGuid, guids.value(face), host.project->getProjectGuid());
        }
    } else {
        return fail(QStringLiteral("world.sky: unknown type '%1' (color, gradient, realistic, equirectangular, cubemap)").arg(type));
    }

    return true;
}

QVariantMap WorldApi::get()
{
    QVariantMap out;
    auto scene = sceneOrFail(QStringLiteral("world.get"));
    if (!scene) return out;

    out["ambient"] = colorToJs(scene->ambientColor);
    out["gravity"] = scene->gravity;
    out["shadows"] = scene->shadowEnabled;
    out["antiAliasing"] = scene->antiAliasing;   // requested; world.antiAliasing() reads achieved
    out["shadowResolution"] = scene->shadowResolution;   // 0 = Auto; the verb reads the applied value
    out["ambientFromSky"] = scene->ambientFromSky;
    // Resolved, not raw: the three document fields carry "follow" sentinels and
    // a caller reading world.get() wants what the renderer will do.
    out["planarReflections"] = planarReflections();
    // World Mode (POST_CHAIN_SPEC §9.6): the tier plus every resolved row, so a
    // script reads the whole quality picture from one call.
    out["mode"] = worldmodes::modeName(worldmodes::mode(scene));
    out["settings"] = settings();
    out["postFx"] = postFx();
    out["fog"] = QVariantMap{ { "enabled", scene->fogEnabled },
                              { "color", colorToJs(scene->fogColor) },
                              { "density", scene->fogDensity },
                              { "heightDensity", scene->fogHeightDensity },
                              { "heightFalloff", scene->fogHeightFalloff },
                              { "heightLevel", scene->fogHeightLevel },
                              { "breakMinBrightness", scene->fogBreakMinBrightness },
                              { "breakFalloff", scene->fogBreakFalloff },
                              { "start", scene->fogStart },
                              { "end", scene->fogEnd } };
    static const char *giModeNames[] = { "off", "instant_radiosity", "vct", "vct_pcc_hybrid" };
    static const char *giQualityNames[] = { "low", "medium", "high" };
    out["gi"] = QVariantMap{ { "mode", giModeNames[qBound(0, int(scene->giMode), 3)] },
                             { "quality", giQualityNames[qBound(0, int(scene->giQuality), 2)] },
                             { "bounces", scene->giNumBounces },
                             { "light", scene->giLightGuid },
                             { "boundsMin", vecToJs(scene->giBoundsMin) },
                             { "boundsMax", vecToJs(scene->giBoundsMax) },
                             { "pccGrid", vecToJs(scene->giPccGrid) },
                             { "autoRefresh", scene->giAutoRefresh } };
    QVariantMap sky;
    const int typeIndex = qBound(0, int(scene->skyType), scene->skyTypeToStr.size() - 1);
    sky["type"] = scene->skyTypeToStr.at(typeIndex);
    sky["data"] = scene->skyData.value(scene->skyTypeToStr.at(typeIndex)).toVariantMap();
    if (scene->skyType == iris::SkyType::SINGLE_COLOR) sky["color"] = colorToJs(scene->skyColor);
    if (scene->skyType == iris::SkyType::REALISTIC) {
        // The panel's spelling of the same stored floats, so a script can read
        // back what it set with {azimuth, elevation} (VISUAL_PARITY item 1).
        sky["azimuth"] = scene->skyRealistic.sunAzimuth();
        sky["elevation"] = scene->skyRealistic.sunElevation();
    }
    sky["detail"] = scene->skyBakeResolution;
    out["sky"] = sky;
    return out;
}

// ---------------------------------------------------------------------------
// World Modes (POST_CHAIN_SPEC.md §9.6). Nothing below knows a row by name:
// every one of these verbs walks the worldmodes registry, so a new quality row
// is a table entry in src/services/worldmodes.cpp and nothing else.

QVariantMap WorldApi::rowState(const iris::ScenePtr &scene, const worldmodes::Row &r)
{
    const int value = worldmodes::resolved(scene, r);
    const worldmodes::Mode m = worldmodes::mode(scene);
    return QVariantMap{
        { "value", value },
        { "valueId", worldmodes::valueId(r, value) },
        { "label", worldmodes::valueLabel(r, value) },
        { "source", worldmodes::source(scene, r) },
        { "tierValue", worldmodes::tierValue(r, m, scene) },
        { "available", r.available },
    };
}

QString WorldApi::mode(const QVariantMap &params)
{
    auto scene = sceneOrFail(QStringLiteral("world.mode"));
    if (!scene) return QString();
    if (params.contains("mode")) {
        bool ok = false;
        const QString requested = params.value("mode").toString();
        const worldmodes::Mode m = worldmodes::modeFromName(requested, &ok);
        if (!ok) {
            fail(QStringLiteral("world.mode: unknown mode '%1' (%2, custom)")
                     .arg(requested, worldmodes::modeNames().join(QStringLiteral(", "))));
            return QString();
        }
        worldmodes::setMode(scene, m);
    }
    return worldmodes::modeName(worldmodes::mode(scene));
}

QVariantMap WorldApi::settings()
{
    QVariantMap out;
    auto scene = sceneOrFail(QStringLiteral("world.settings"));
    if (!scene) return out;
    for (const worldmodes::Row &r : worldmodes::rows())
        out.insert(r.id, rowState(scene, r));
    return out;
}

QVariantMap WorldApi::override(const QVariantMap &params)
{
    QVariantMap out;
    auto scene = sceneOrFail(QStringLiteral("world.override"));
    if (!scene) return out;
    const QString id = params.value("id").toString();
    const worldmodes::Row *r = worldmodes::row(id);
    if (!r) {
        fail(QStringLiteral("world.override: unknown row '%1' — world.modeTable() lists them").arg(id));
        return out;
    }
    if (!params.contains("value")) {
        fail(QStringLiteral("world.override: 'value' is required"));
        return out;
    }
    // A value may arrive as the row's id spelling ("4x", "vct", "off") or as
    // the raw number; both resolve through the row's own option table.
    const QVariant raw = params.value("value");
    int value = 0;
    bool resolvedValue = false;
    if (raw.typeId() == QMetaType::Bool) {
        value = raw.toBool() ? 1 : 0;
        resolvedValue = true;
    } else if (raw.typeId() == QMetaType::QString) {
        resolvedValue = worldmodes::valueFromId(*r, raw.toString(), value);
    } else {
        bool ok = false;
        value = raw.toInt(&ok);
        resolvedValue = ok;
    }
    if (!resolvedValue || !worldmodes::setRowValue(scene, id, value)) {
        fail(QStringLiteral("world.override: '%1' is not a valid value for row '%2'")
                 .arg(raw.toString(), id));
        return out;
    }
    return rowState(scene, *r);
}

QVariantMap WorldApi::clearOverride(const QVariantMap &params)
{
    QVariantMap out;
    auto scene = sceneOrFail(QStringLiteral("world.clearOverride"));
    if (!scene) return out;
    const QString id = params.value("id").toString();
    const worldmodes::Row *r = worldmodes::row(id);
    if (!r) {
        fail(QStringLiteral("world.clearOverride: unknown row '%1'").arg(id));
        return out;
    }
    worldmodes::clearOverride(scene, id);
    return rowState(scene, *r);
}

QVariantMap WorldApi::clearOverrides()
{
    auto scene = sceneOrFail(QStringLiteral("world.clearOverrides"));
    if (!scene) return QVariantMap();
    worldmodes::clearOverrides(scene);
    return settings();
}

QVariantMap WorldApi::postFx(const QVariantMap &params)
{
    QVariantMap out;
    auto scene = sceneOrFail(QStringLiteral("world.postFx"));
    if (!scene) return out;
    // Deliberately NOT World Mode rows: a tier answers "how much machinery", and
    // these answer "how does it look". Tiering an art decision would mean a mode
    // switch silently regrading the user's scene.
    if (params.contains("exposure"))
        scene->exposure = float(qBound(-8.0, params.value("exposure").toDouble(), 8.0));
    if (params.contains("bloomThreshold"))
        scene->bloomThreshold = float(qBound(0.0, params.value("bloomThreshold").toDouble(), 64.0));
    if (params.contains("ssaoPower"))
        scene->ssaoPower = float(qBound(0.1, params.value("ssaoPower").toDouble(), 8.0));
    if (params.contains("ssaoRadius"))
        scene->ssaoRadius = float(qBound(0.05, params.value("ssaoRadius").toDouble(), 64.0));
    out["exposure"] = scene->exposure;
    out["bloomThreshold"] = scene->bloomThreshold;
    out["ssaoPower"] = scene->ssaoPower;
    out["ssaoRadius"] = scene->ssaoRadius;
    return out;
}

QVariantMap WorldApi::modeTable()
{
    QVariantMap out;
    QVariantList rowList;
    for (const worldmodes::Row &r : worldmodes::rows()) {
        QVariantMap row;
        row["id"] = r.id;
        row["label"] = r.label;
        row["group"] = r.group;
        row["type"] = r.type == worldmodes::RowType::Bool ? QStringLiteral("bool")
                    : r.type == worldmodes::RowType::Enum ? QStringLiteral("enum")
                                                          : QStringLiteral("int");
        row["cost"] = r.cost;
        row["available"] = r.available;
        if (r.type == worldmodes::RowType::Int) {
            row["min"] = r.minValue;
            row["max"] = r.maxValue;
        }
        QVariantList options;
        for (const worldmodes::EnumOption &o : r.options)
            options.append(QVariantMap{ { "id", o.id }, { "label", o.label }, { "value", o.value } });
        if (!options.isEmpty()) row["options"] = options;
        QVariantMap tiers;
        const QStringList names = worldmodes::modeNames();
        for (int i = 0; i < names.size(); ++i)
            tiers.insert(names[i], QVariantMap{ { "value", r.tier[i] },
                                                { "valueId", worldmodes::valueId(r, r.tier[i]) } });
        row["tiers"] = tiers;
        rowList.append(row);
    }
    out["modes"] = worldmodes::modeNames();
    out["rows"] = rowList;
    return out;
}
