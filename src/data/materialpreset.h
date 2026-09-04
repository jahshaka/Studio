/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPRESET_H
#define MATERIALPRESET_H

#include <QColor>

struct MaterialPreset
{
    QString name;
    QString icon;
    QString type;

    QColor ambientColor;

    QColor diffuseColor;
    QString diffuseTexture;

    QColor specularColor;
    QString specularTexture;
    float shininess;

    QString normalTexture;
    float normalIntensity;

    QString reflectionTexture;
    float reflectionInfluence;

    float textureScale;

    // --- PBR (material_type: "PBR") ---
    QColor  baseColor;
    QString baseColorMap;
    float   metallic;
    QString metallicMap;
    float   roughness;
    QString roughnessMap;
    float   roughnessLowerBound;
    float   roughnessUpperBound;
    QString pbrNormalMap;
    float   pbrNormalFactor;
    QString occlusionMap;
    float   occlusionFactor;
    QColor  emissiveColor;
    float   emissiveIntensity;
    QString emissiveMap;
    int     alphaMode;      // 0 opaque, 1 masked, 2 translucent, 3 glass,
                            // 4 additive, 5 modulate (PbrMaterial::alphaMode)
    float   alpha;
    float   alphaCutoff;
};

#endif // MATERIALPRESET_H
