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
        { "fog", "world.fog({enabled, color, start, end}) -> bool",
          "Sets any subset of the fog settings.",
          Needs::Document },
        { "shadows", "world.shadows({enabled}) -> bool",
          "Toggles shadow rendering.",
          Needs::Document },
        { "gi", "world.gi({mode, quality, bounces, light, boundsMin, boundsMax, autoRefresh}) -> bool",
          "Global illumination: mode off|instant_radiosity|vct|vct_pcc_hybrid, quality low|medium|high, bounces 1-4, light = driving light guid ('' = auto).",
          Needs::Document },
        { "sky", "world.sky(type, {...}) -> bool",
          "Sets the sky. Types: color {color}; gradient {top, mid, bottom, offset}; realistic {luminance, reileigh, mieCoefficient, mieDirectionalG, turbidity, sunPosX, sunPosY, sunPosZ}; equirectangular {texture}; cubemap {front, back, left, right, top, bottom} (textures = asset guids or file names in the project).",
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
    if (params.contains("end"))     scene->fogEnd = params.value("end").toFloat();
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
    if (params.contains("autoRefresh"))
        scene->giAutoRefresh = params.value("autoRefresh").toBool();
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
    out["fog"] = QVariantMap{ { "enabled", scene->fogEnabled },
                              { "color", colorToJs(scene->fogColor) },
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
                             { "autoRefresh", scene->giAutoRefresh } };
    QVariantMap sky;
    const int typeIndex = qBound(0, int(scene->skyType), scene->skyTypeToStr.size() - 1);
    sky["type"] = scene->skyTypeToStr.at(typeIndex);
    sky["data"] = scene->skyData.value(scene->skyTypeToStr.at(typeIndex)).toVariantMap();
    if (scene->skyType == iris::SkyType::SINGLE_COLOR) sky["color"] = colorToJs(scene->skyColor);
    out["sky"] = sky;
    return out;
}
