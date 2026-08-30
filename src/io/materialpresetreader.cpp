/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/document/materials/defaultmaterial.h"
#include "io/materialpresetreader.h"
#include "data/materialpreset.h"

#include <QJsonDocument>
#include <QJsonObject>

QJsonObject MaterialPresetReader::getMatPreset(const QString &filename)
{
    this->setAssetPath(filename);

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
        qWarning("MaterialPresetReader::getMatPreset: failed to open %s", qUtf8Printable(filename));

    auto data = file.readAll();
    auto doc = QJsonDocument::fromJson(data);

    return doc.object();
}

MaterialPreset MaterialPresetReader::readMaterialPreset(QString filename)
{
    this->setAssetPath(filename);

    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly))
        qWarning("MaterialPresetReader::readMaterialPreset: failed to open %s", qUtf8Printable(filename));

    auto data = file.readAll();
    auto doc = QJsonDocument::fromJson(data);

    auto matObj = doc.object();

    MaterialPreset material;

    material.name = matObj["name"].toString("");

    auto icon = matObj["icon"].toString("");
    if (!icon.isEmpty()) material.icon = getAbsolutePath(icon);

    material.type = matObj["material_type"].toString();

    auto colObj = matObj["ambientColor"].toString();
    QColor col;
    col.setNamedColor(colObj);
    material.ambientColor = col;

    colObj = matObj["diffuseColor"].toString();
    col.setNamedColor(colObj);
    material.diffuseColor = col;

    auto tex = matObj["diffuseTexture"].toString("");
    if (!tex.isEmpty()) material.diffuseTexture = getAbsolutePath(tex);

    colObj = matObj["specularColor"].toString();
    col.setNamedColor(colObj);
    material.specularColor = col;
    material.shininess = (float)matObj["shininess"].toDouble(0.0f);

    tex = matObj["specularTexture"].toString("");
    if (!tex.isEmpty())  material.specularTexture = getAbsolutePath(tex);

    tex = matObj["normalTexture"].toString("");
    if (!tex.isEmpty()) material.normalTexture = getAbsolutePath(tex);
    material.normalIntensity = (float)matObj["normalIntensity"].toDouble(0.0f);

    tex = matObj["reflectionTexture"].toString("");
    if (!tex.isEmpty()) material.reflectionTexture = getAbsolutePath(tex);
    material.reflectionInfluence = (float)matObj["reflectionInfluence"].toDouble(0.0f);

    material.textureScale = (float)matObj["textureScale"].toDouble(1.0f);

    // --- PBR fields, read when material_type is "PBR" ---
    // Absent on every legacy preset, so the defaults below leave them inert.
    {
        QColor c;
        c.setNamedColor(matObj["baseColor"].toString("#FFFFFF"));
        material.baseColor = c;
        c.setNamedColor(matObj["emissiveColor"].toString("#000000"));
        material.emissiveColor = c;
    }

    auto pbrTex = [&](const char* key) -> QString {
        auto v = matObj[key].toString("");
        return v.isEmpty() ? QString() : getAbsolutePath(v);
    };

    material.baseColorMap        = pbrTex("baseColorMap");
    material.metallicMap         = pbrTex("metallicMap");
    material.roughnessMap        = pbrTex("roughnessMap");
    material.pbrNormalMap        = pbrTex("normalMap");
    material.occlusionMap        = pbrTex("occlusionMap");
    material.emissiveMap         = pbrTex("emissiveMap");

    material.metallic            = static_cast<float>(matObj["metallic"].toDouble(0.0));
    material.roughness           = static_cast<float>(matObj["roughness"].toDouble(0.5));
    // NOTE: lower > upper is intentional and inverts a legacy spec/gloss map.
    material.roughnessLowerBound = static_cast<float>(matObj["roughnessLowerBound"].toDouble(0.0));
    material.roughnessUpperBound = static_cast<float>(matObj["roughnessUpperBound"].toDouble(1.0));
    material.pbrNormalFactor     = static_cast<float>(matObj["normalFactor"].toDouble(1.0));
    material.occlusionFactor     = static_cast<float>(matObj["occlusionFactor"].toDouble(1.0));
    material.emissiveIntensity   = static_cast<float>(matObj["emissiveIntensity"].toDouble(0.0));
    material.alphaMode           = matObj["alphaMode"].toInt(0);
    material.alpha               = static_cast<float>(matObj["alpha"].toDouble(1.0));
    material.alphaCutoff         = static_cast<float>(matObj["alphaCutoff"].toDouble(0.5));

    return material;
}
