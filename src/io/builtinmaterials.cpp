/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/builtinmaterials.h"

#include <QColor>
#include <QFileInfo>
#include <QJsonValue>
#include <algorithm>
#include <cmath>

#include "irisgl/document/materials/pbrmaterial.h"
#include "data/materialpreset.h"
#include "irisgl/document/assets/mesh.h"

namespace {

/// THE SHININESS REMAP, and it is the mirror's, not a new one
/// (SceneMirror::toPbrParams). Legacy Blinn shininess is inverse-sense and
/// assimp encodes glTF roughness into it as (1-r)^2 * 1000, so the 128 clamp
/// and the 0.9 span are load-bearing: without the clamp every imported smooth
/// material came back a near-mirror. Keeping ONE formula is why this function
/// exists rather than an inline expression at each call site.
float roughnessFromShininess(float shininess)
{
    const float s = std::max(0.0f, std::min(shininess, 128.0f));
    return 1.0f - std::sqrt(s / 128.0f) * 0.9f;
}

} // namespace

namespace BuiltinMaterials {

iris::PbrMaterialPtr fromPreset(const MaterialPreset &preset)
{
    auto mat = iris::PbrMaterial::create();
    mat->setName(preset.name);

    if (preset.type.compare(QStringLiteral("PBR"), Qt::CaseInsensitive) == 0) {
        mat->setValue(QStringLiteral("baseColor"),           preset.baseColor);
        mat->setValue(QStringLiteral("metallic"),            preset.metallic);
        mat->setValue(QStringLiteral("roughness"),           preset.roughness);
        mat->setValue(QStringLiteral("roughnessLowerBound"), preset.roughnessLowerBound);
        mat->setValue(QStringLiteral("roughnessUpperBound"), preset.roughnessUpperBound);
        mat->setValue(QStringLiteral("normalFactor"),        preset.pbrNormalFactor);
        mat->setValue(QStringLiteral("emissiveColor"),       preset.emissiveColor);
        mat->setValue(QStringLiteral("emissiveIntensity"),   preset.emissiveIntensity);
        mat->setValue(QStringLiteral("textureScale"),        preset.textureScale);
        mat->setValue(QStringLiteral("alphaMode"),           preset.alphaMode);
        mat->setValue(QStringLiteral("alpha"),               preset.alpha);
        mat->setValue(QStringLiteral("alphaCutoff"),         preset.alphaCutoff);
        mat->setValue(QStringLiteral("baseColorMap"),  preset.baseColorMap);
        mat->setValue(QStringLiteral("metallicMap"),   preset.metallicMap);
        mat->setValue(QStringLiteral("roughnessMap"),  preset.roughnessMap);
        mat->setValue(QStringLiteral("normalMap"),     preset.pbrNormalMap);
        mat->setValue(QStringLiteral("emissiveMap"),   preset.emissiveMap);
        return mat;
    }

    return mat;
}

}

namespace BuiltinMaterials {

iris::PbrMaterialPtr fromMeshData(const iris::MeshMaterialData &data)
{
    auto mat = iris::PbrMaterial::create();
    auto bindIfFile = [&mat](const QString &row, const QString &path) {
        if (!path.isEmpty() && QFileInfo(path).isFile()) mat->setValue(row, path);
    };

    const auto isFile = [](const QString &p) {
        return !p.isEmpty() && QFileInfo(p).isFile();
    };

    if (data.hasPbr) {
        // The source really was a PBR one (glTF): use its values verbatim
        // rather than round-tripping through assimp's lossy shininess
        // back-conversion, which is what produced near-mirror imports. A
        // spec-gloss source arrives already converted (MaterialHelper).
        mat->setValue(QStringLiteral("baseColor"), data.baseColorFactor);
        mat->setValue(QStringLiteral("metallic"), data.metallicFactor);
        mat->setValue(QStringLiteral("roughness"), data.roughnessFactor);
        bindIfFile(QStringLiteral("baseColorMap"), data.baseColorTexture);
        bindIfFile(QStringLiteral("metallicMap"),  data.metallicTexture);
        bindIfFile(QStringLiteral("roughnessMap"), data.roughnessTexture);

        // ---- the WORKFLOW (MATERIAL_GAPS_SPEC GAP 1) ----------------------
        // A spec-gloss or KHR_materials_specular source now arrives in its own
        // workflow instead of converted. The IOR rides along on EVERY workflow
        // (inert on metallic, restored by a switch); kS and F0 only mean
        // something on the two specular ones but are stored the same way, for
        // the same reason.
        mat->setValue(QStringLiteral("workflow"), data.workflow);
        mat->setValue(QStringLiteral("ior"), data.ior);
        if (data.workflow != 0) {
            mat->setValue(QStringLiteral("specularColor"), data.specularFactor);
            mat->setValue(QStringLiteral("useFresnelColor"), data.useFresnelColor);
            if (data.useFresnelColor)
                mat->setValue(QStringLiteral("fresnelColor"), data.fresnelFactor);
            // The specular / spec-gloss map binds to the SHARED metallic-specular
            // texture unit — the renderer reinterprets one unit by workflow, so
            // `metallicMap` is the right row and only one of the two sources can
            // fill it. Before the workflow switch this map had nowhere to go and
            // was dropped with a warning.
            bindIfFile(QStringLiteral("metallicMap"), data.specularMapTexture);
        }
        bindIfFile(QStringLiteral("normalMap"),    data.normalTexture);
        bindIfFile(QStringLiteral("emissiveMap"),  data.emissiveTexture);

        // EMISSIVE INTENSITY IS PART OF "THERE IS EMISSION" (2026-09-08).
        // emissiveColor without emissiveIntensity is emission multiplied by
        // zero — the mirror pushes colour*intensity (scenemirror.cpp) — and
        // the import path that Studio actually uses set the colour and left
        // the intensity at the document default 0, so every model came back
        // with "#ffffff at 0". The two travel together or not at all.
        //
        // A BLACK emissive factor stays dark, map or no map: glTF's
        // emissiveFactor defaults to [0,0,0] and multiplies the emissive
        // texture, so "there is an emissive map" is NOT on its own a
        // statement that the surface emits. Inventing white emission for one
        // would light up every model whose exporter wrote a map the artist
        // muted. (Unlit is the one place a black factor is overridden, below,
        // and for a different reason: there the map is the COLOUR.)
        const bool emissiveMap = isFile(data.emissiveTexture);
        const bool emissiveTinted =
            data.emissionColor.isValid() && data.emissionColor != QColor(Qt::black);
        if (emissiveTinted) {
            mat->setValue(QStringLiteral("emissiveColor"), data.emissionColor);
            mat->setValue(QStringLiteral("emissiveIntensity"), 1.0f);
        }

        // KHR_materials_unlit. The engine's Unlit family consumes NO lighting
        // inputs at all — no emissive, no maps but the colour one
        // (PbrMaterial::rowsUnusedWhenUnlit) — so an unlit material's visible
        // colour has to end up on baseColor/baseColorMap or it is lost. The
        // common unlit export (Sketchfab and friends) leaves baseColorFactor
        // BLACK and puts the artwork in emissiveTexture/emissiveFactor: read
        // literally that is a black model, which is exactly how those files
        // imported. So on an unlit material the emissive slot BECOMES the
        // colour slot when the base-colour one is empty or black.
        if (data.unlit) {
            mat->setValue(QStringLiteral("shadingModel"), 1);
            const bool baseIsBlack = !data.baseColorFactor.isValid() ||
                                     data.baseColorFactor == QColor(Qt::black);
            if (!isFile(data.baseColorTexture) && emissiveMap)
                mat->setValue(QStringLiteral("baseColorMap"), data.emissiveTexture);
            if (baseIsBlack && (emissiveTinted || emissiveMap))
                mat->setValue(QStringLiteral("baseColor"),
                              emissiveTinted ? data.emissionColor : QColor(Qt::white));
        }
        return mat;
    }

    mat->setValue(QStringLiteral("baseColor"),
                  data.diffuseColor.isValid() ? data.diffuseColor : QColor(200, 200, 200));
    mat->setValue(QStringLiteral("metallic"), 0.0f);
    mat->setValue(QStringLiteral("roughness"), roughnessFromShininess(data.shininess));
    bindIfFile(QStringLiteral("baseColorMap"), data.diffuseTexture);
    bindIfFile(QStringLiteral("normalMap"),    data.normalTexture);
    // specularTexture / hightTexture have no metallic-roughness home, and never
    // reached the renderer through the old path either.
    return mat;
}

}
