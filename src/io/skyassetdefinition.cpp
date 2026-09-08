/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/skyassetdefinition.h"

#include <QFileInfo>
#include <QImage>

#include "io/assetiobase.h"
#include "irisgl/document/assets/texture2d.h"

namespace skyassets
{

namespace {

QString resolvePath(const TexturePathResolver &resolve, const QString &guid)
{
    if (!resolve || guid.isEmpty()) return QString();
    const QString path = resolve(guid);
    return (!path.isEmpty() && QFileInfo(path).isFile()) ? path : QString();
}

}

bool applyToScene(const iris::ScenePtr &scene, iris::SkyType type,
                  const QJsonObject &skyData,
                  const TexturePathResolver &resolve,
                  const QStringList &textureDependees)
{
    if (!scene) return false;

    scene->skyType = type;

    switch (type) {
    case iris::SkyType::SINGLE_COLOR:
        scene->skyColor = AssetIOBase::readColor(skyData.value("skyColor").toObject());
        return true;

    case iris::SkyType::GRADIENT:
        scene->gradientTop = AssetIOBase::readColor(skyData.value("gradientTop").toObject());
        scene->gradientMid = AssetIOBase::readColor(skyData.value("gradientMid").toObject());
        scene->gradientBot = AssetIOBase::readColor(skyData.value("gradientBot").toObject());
        scene->gradientOffset = float(skyData.value("gradientOffset").toDouble());
        return true;

    case iris::SkyType::REALISTIC: {
        // The same eight keys, with the same per-key defaults, that
        // SceneReader::readScene reads for a project scene: a definition
        // written before a key existed lands on the model's working value
        // rather than on zero (VISUAL_PARITY item 1).
        const iris::SkyRealistic d = iris::SkyRealistic::defaults();
        iris::SkyRealistic r;
        r.luminance       = float(skyData.value("luminance").toDouble(d.luminance));
        r.reileigh        = float(skyData.value("reileigh").toDouble(d.reileigh));
        r.mieCoefficient  = float(skyData.value("mieCoefficient").toDouble(d.mieCoefficient));
        r.mieDirectionalG = float(skyData.value("mieDirectionalG").toDouble(d.mieDirectionalG));
        r.turbidity       = float(skyData.value("turbidity").toDouble(d.turbidity));
        r.sunPosX         = float(skyData.value("sunPosX").toDouble(d.sunPosX));
        r.sunPosY         = float(skyData.value("sunPosY").toDouble(d.sunPosY));
        r.sunPosZ         = float(skyData.value("sunPosZ").toDouble(d.sunPosZ));
        scene->skyRealistic = r;
        return true;
    }

    case iris::SkyType::EQUIRECTANGULAR: {
        // The TEXTURE's own bytes, by ITS guid. (The pre-CAS join built
        // <root>/<skyGuid>/<textureName> — the sky asset's folder with the
        // texture asset's file name — which only ever resolved because the old
        // .jaf import dropped both in one directory.)
        QString guid = skyData.value("equiSkyGuid").toString();
        QString image = resolvePath(resolve, guid);
        if (image.isEmpty()) {
            for (const QString &dependee : textureDependees) {
                image = resolvePath(resolve, dependee);
                if (!image.isEmpty()) break;
            }
        }
        if (image.isEmpty()) return false;
        scene->setSkyTexture(iris::Texture2D::load(image, false));
        return true;
    }

    case iris::SkyType::CUBEMAP: {
        // Six texture-asset guids under their face roles, each resolved the way
        // the equirect branch resolves its one. createCubeMap's argument order
        // is front, back, TOP, BOTTOM, left, right (SceneReader passes the same
        // permutation) — getting it wrong rotates the sky silently.
        static const char *roles[6] = { "front", "back", "top", "bottom", "left", "right" };
        QString sides[6];
        for (int i = 0; i < 6; ++i) {
            sides[i] = resolvePath(resolve, skyData.value(QLatin1String(roles[i])).toString());
            // ALL SIX or nothing: Texture2D::createCubeMap returns null the
            // moment one face fails to decode (texture2d.cpp:85), so a partial
            // definition can only ever produce "no sky" — say so here instead
            // of handing the mirror a null texture and calling it success.
            if (sides[i].isEmpty()) return false;
        }

        QImage info(sides[0]);
        if (info.isNull()) return false;

        auto cube = iris::Texture2D::createCubeMap(sides[0], sides[1], sides[2],
                                                   sides[3], sides[4], sides[5], &info);
        if (!cube) return false;
        scene->setSkyTexture(cube);
        return true;
    }

    case iris::SkyType::MATERIAL:
    default:
        // The Material sky type was retired from the UI (it never worked, even
        // on the legacy renderer): show a flat colour rather than nothing.
        scene->skyType = iris::SkyType::SINGLE_COLOR;
        return false;
    }
}

}
