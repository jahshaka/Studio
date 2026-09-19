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

    // (THE LEGACY BLINN FIELDS ARE NO LONGER READ — MATERIAL_BUNDLE_SPEC
    // phase 3. ambient/diffuse/specular colours, shininess, the three legacy
    // texture slots and the reflection pair described the pre-PBR material
    // class, which is gone; the fourteen files that carried them are deleted,
    // and the preset list has skipped every non-PBR file since the HLMS
    // adoption. An old file with those keys still LOADS — every key here is
    // optional — it simply arrives as the PBR material its remaining fields
    // describe.)

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
    material.emissiveMap         = pbrTex("emissiveMap");

    material.metallic            = static_cast<float>(matObj["metallic"].toDouble(0.0));
    material.roughness           = static_cast<float>(matObj["roughness"].toDouble(0.5));
    // NOTE: lower > upper is intentional and inverts a legacy spec/gloss map.
    material.roughnessLowerBound = static_cast<float>(matObj["roughnessLowerBound"].toDouble(0.0));
    material.roughnessUpperBound = static_cast<float>(matObj["roughnessUpperBound"].toDouble(1.0));
    material.pbrNormalFactor     = static_cast<float>(matObj["normalFactor"].toDouble(1.0));
    // "occlusionFactor"/"occlusionMap" are still present in shipped and user
    // .material files and are deliberately UNREAD (HLMS_ADOPTION P2 removed the
    // ghost AO chain). Tolerated, never a load failure.
    material.emissiveIntensity   = static_cast<float>(matObj["emissiveIntensity"].toDouble(0.0));
    material.alphaMode           = matObj["alphaMode"].toInt(0);
    material.alpha               = static_cast<float>(matObj["alpha"].toDouble(1.0));
    material.alphaCutoff         = static_cast<float>(matObj["alphaCutoff"].toDouble(0.5));

    return material;
}
