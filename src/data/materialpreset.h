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
#include <QJsonObject>

// A SHIPPED MATERIAL PRESET, as read from `app/content/materials/*.material`.
//
// PBR ONLY (MATERIAL_BUNDLE_SPEC phase 3). The pre-PBR Blinn fields —
// ambient/diffuse/specular colours, shininess, the three legacy texture slots
// and the reflection pair — are DELETED with the fourteen `.material` files
// that carried them: the preset list has skipped every non-PBR file since the
// HLMS adoption, so nothing had read them for months. `type` survives because
// the reader still reports what a file claims to be and `materials.presets()`
// reports it to a script.
struct MaterialPreset
{
    QString name;
    QString icon;
    QString type;

    /// EVERY SHIPPED PRESET IS A GRAPH (PRESET-UNIFY-1, the owner 2026-09-20:
    /// "if i select a preset i should see the graph"). The `shadergraph`
    /// payload authored beside the preset's values in
    /// `app/content/materials/graphs/`, its texture nodes naming their images
    /// the way the map slots above do — an absolute path once the reader has
    /// resolved it. Empty only for a build whose graph file is missing, which
    /// the preset list refuses to list.
    QJsonObject graph;

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
    QColor  emissiveColor;
    float   emissiveIntensity;
    QString emissiveMap;
    int     alphaMode;      // 0 opaque, 1 masked, 2 translucent, 3 glass,
                            // 4 additive, 5 modulate (PbrMaterial::alphaMode)
    float   alpha;
    float   alphaCutoff;
};

#endif // MATERIALPRESET_H
