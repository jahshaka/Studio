/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "scripting/modules/worldapi.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonObject>

#include "scripting/modules/moduleshared.h"
#include "data/database/database.h"
#include "data/project.h"
#include "io/scenewriter.h"
#include "shell/mainwindow.h"
#include "services/sceneeditservice.h"
#include "services/services.h"
#include "viewport/ieditorviewport.h"
#include "irisgl/document/assets/texture2d.h"

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
        { "sky", "world.sky(type, {...}) -> bool",
          "Sets the sky. Types: color {color}; gradient {top, mid, bottom, offset}; realistic {luminance, reileigh, mieCoefficient, mieDirectionalG, turbidity, azimuth, elevation | sunPosX, sunPosY, sunPosZ, detail}; equirectangular {texture}; cubemap {front, back, left, right, top, bottom} (textures = asset guids or file names in the project). For the realistic sky, azimuth (degrees clockwise from +Z) and elevation (degrees above the horizon) are the readable way to place the sun and win over raw sunPos*; turbidity is Preetham's 1..20 haze; detail is the equirect bake width (256, 512 or 1024).",
          Needs::Document },
        { "get", "world.get() -> {ambient, gravity, fog, shadows, gi, sky}",
          "Reads the current world settings.",
          Needs::Document },
    };
}

iris::ScenePtr WorldApi::sceneOrFail(const QString &verb)
{
    auto scene = (host.services && host.services->sceneEdit) ? host.services->sceneEdit->scene() : iris::ScenePtr();
    if (!scene) fail(QStringLiteral("%1: no scene is open").arg(verb));
    return scene;
}

bool WorldApi::ambient(const QVariant &color)
{
    auto scene = sceneOrFail(QStringLiteral("world.ambient"));
    if (!scene) return false;
    scene->setAmbientColor(colorFromJs(color, scene->ambientColor));
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
    if (params.contains("color"))   scene->fogColor = colorFromJs(params.value("color"), scene->fogColor);
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
    }
    if (params.contains("quality")) {
        const QString q = params.value("quality").toString().trimmed().toLower();
        if (q == "low")         scene->giQuality = iris::GiQuality::LOW;
        else if (q == "medium") scene->giQuality = iris::GiQuality::MEDIUM;
        else if (q == "high")   scene->giQuality = iris::GiQuality::HIGH;
        else return fail(QStringLiteral("world.gi: unknown quality '%1' (low, medium, high)").arg(q));
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
        const QVector3D g = vecFromJs(params.value("pccGrid"), scene->giPccGrid);
        scene->giPccGrid = QVector3D(qBound(1, qRound(g.x()), 8), qBound(1, qRound(g.y()), 8),
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
    return true;
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

    const QString path = QDir(host.project->getProjectFolder())
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
        const QColor c = colorFromJs(params.value("color"), scene->skyColor);
        scene->skyColor = c;
        QJsonObject def;
        def.insert("skyColor", SceneWriter::jsonColor(c));
        scene->skyData.insert("SingleColor", def);
        scene->skyType = iris::SkyType::SINGLE_COLOR;
    } else if (t == "gradient") {
        scene->gradientTop = colorFromJs(params.value("top"), scene->gradientTop);
        scene->gradientMid = colorFromJs(params.value("mid"), scene->gradientMid);
        scene->gradientBot = colorFromJs(params.value("bottom"), scene->gradientBot);
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
        for (const char *face : faces) {
            QString guid, path;
            if (params.contains(face) && resolveTexture(params.value(face), guid, path)) {
                guids[face] = guid;
                paths[face] = path;
            } else {
                guids[face] = QString();
            }
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
