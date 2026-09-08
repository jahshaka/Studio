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

// THE RESERVED GUIDS, spelled out here rather than read from
// Constants::Reserved::BuiltinShaders. That map still exists and still points
// each guid at a `.shader` FILE — those files stay on disk as the record of
// what the uniforms were, and of what a legacy `values{}` block's key names
// mean — but nothing loads one as a shader any more, and this translation must
// not need the app's constants table to run. (It is what lets the mirror suite,
// which links no Studio code, drive the conversion directly.)
const char *kDefault         = "00000000-0000-0000-0000-000000000001";
const char *kDefaultAnimated = "00000000-0000-0000-0000-000000000002";
const char *kEdgeMaterial    = "00000000-0000-0000-0000-000000000003";
const char *kFlat            = "00000000-0000-0000-0000-000000000004";
const char *kGlass           = "00000000-0000-0000-0000-000000000005";
const char *kMatcap          = "00000000-0000-0000-0000-000000000006";

QColor colourOf(const QJsonObject &values, const char *key, bool *found = nullptr)
{
    const QJsonValue v = values.value(QLatin1String(key));
    if (found) *found = false;
    if (!v.isString()) return QColor();
    QColor c;
    c.setNamedColor(v.toString());
    if (!c.isValid()) return QColor();
    if (found) *found = true;
    return c;
}

bool floatOf(const QJsonObject &values, const char *key, float &out)
{
    const QJsonValue v = values.value(QLatin1String(key));
    if (!v.isDouble()) return false;
    out = float(v.toDouble());
    return true;
}

QString textureOf(const QJsonObject &values, const char *key,
                  const std::function<QString(const QString &)> &resolve)
{
    const QString stored = values.value(QLatin1String(key)).toString();
    if (stored.isEmpty() || !resolve) return QString();
    return resolve(stored);
}

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

/// The values every legacy Default-family material carries, applied onto `mat`.
/// Shared by the Default/DefaultAnimated builtins and by the unknown-shader
/// fallback, because a stranger's `.shader` uniforms were almost always copies
/// of Default's.
void applyDefaultFamily(const iris::PbrMaterialPtr &mat, const QJsonObject &values,
                        const std::function<QString(const QString &)> &resolve)
{
    bool found = false;
    // The colour, under any of the four spellings the tree has used.
    for (const char *key : { "diffuseColor", "color", "albedo", "baseColor" }) {
        const QColor c = colourOf(values, key, &found);
        if (found) { mat->setValue(QStringLiteral("baseColor"), c); break; }
    }

    // Roughness: a real roughness wins; shininess is only CONSULTED when there
    // is none. (This ordering is the mirror's, and it exists because running
    // the shininess remap unconditionally stamped over every imported glTF
    // roughness.)
    float f = 0.0f;
    if (floatOf(values, "roughness", f) || floatOf(values, "roughnessFactor", f))
        mat->setValue(QStringLiteral("roughness"), f);
    else if (floatOf(values, "shininess", f))
        mat->setValue(QStringLiteral("roughness"), roughnessFromShininess(f));

    if (floatOf(values, "metallic", f) || floatOf(values, "metalness", f))
        mat->setValue(QStringLiteral("metallic"), f);
    if (floatOf(values, "textureScale", f))
        mat->setValue(QStringLiteral("textureScale"), f);
    if (floatOf(values, "normalIntensity", f) || floatOf(values, "normalFactor", f))
        mat->setValue(QStringLiteral("normalFactor"), f);

    // `useAlpha` meant "respect the diffuse texture's alpha channel", which is
    // glTF BLEND. It reached nothing in the engine before this (the mirror
    // never read it), so honouring it is a small restoration, not a change.
    if (values.value(QLatin1String("useAlpha")).toBool(false))
        mat->setValue(QStringLiteral("alphaMode"), 2);

    for (const char *key : { "diffuseTexture", "baseColorMap", "albedoMap" }) {
        const QString p = textureOf(values, key, resolve);
        if (!p.isEmpty()) { mat->setValue(QStringLiteral("baseColorMap"), p); break; }
    }
    for (const char *key : { "normalTexture", "normalMap" }) {
        const QString p = textureOf(values, key, resolve);
        if (!p.isEmpty()) { mat->setValue(QStringLiteral("normalMap"), p); break; }
    }
    const QString em = textureOf(values, "emissiveMap", resolve);
    if (!em.isEmpty()) mat->setValue(QStringLiteral("emissiveMap"), em);

    // NOT CARRIED, and deliberately: ambientColor, specularColor,
    // specularTexture, reflectionTexture, reflectionInfluence. There is no
    // metallic-roughness parameter for any of them, and the renderer has not
    // read one of them for as long as the engine viewport has existed — the
    // loss is being made explicit here, not created here.
}

} // namespace

namespace BuiltinMaterials {

QString builtinName(const QString &shaderGuid)
{
    if (shaderGuid == QLatin1String(kDefault))         return QStringLiteral("Default");
    if (shaderGuid == QLatin1String(kDefaultAnimated)) return QStringLiteral("DefaultAnimated");
    if (shaderGuid == QLatin1String(kEdgeMaterial))    return QStringLiteral("EdgeMaterial");
    if (shaderGuid == QLatin1String(kFlat))            return QStringLiteral("Flat");
    if (shaderGuid == QLatin1String(kGlass))           return QStringLiteral("Glass");
    if (shaderGuid == QLatin1String(kMatcap))          return QStringLiteral("Matcap");
    return QString();
}

bool isBuiltin(const QString &shaderGuid)
{
    return !builtinName(shaderGuid).isEmpty();
}

iris::PbrMaterialPtr fromBuiltin(const QString &shaderGuid, const QJsonObject &values,
                                 const std::function<QString(const QString &)> &resolveTexture)
{
    auto mat = iris::PbrMaterial::create();
    mat->setName(builtinName(shaderGuid));
    mat->setGuid(shaderGuid);

    if (shaderGuid == QLatin1String(kFlat)) {
        // D-P4b, and the reason P4a came first: `Flat` was the one builtin with
        // NO metallic-roughness equivalent — a flat unlit colour is not a PBR
        // surface with the lighting turned down, it is the other shading model.
        // Now that the renderer has one, Flat is simply an Unlit preset and the
        // conversion is exact rather than approximate.
        mat->setValue(QStringLiteral("shadingModel"), 1);
        bool found = false;
        const QColor c = colourOf(values, "color", &found);
        mat->setValue(QStringLiteral("baseColor"), found ? c : QColor(255, 255, 255));
        return mat;
    }

    if (shaderGuid == QLatin1String(kGlass)) {
        // The three legacy uniforms (refractivity / Influence / Transparency)
        // parameterise a hand-written refraction shader that no longer exists,
        // and none of them is a metallic-roughness quantity. The preset is the
        // drawer's Glass PBR (app/content/materials/Glass-pbr.material) — the
        // same values the 2026-08-31 sample conversion already applied to every
        // shipped Glass material, so shipped content does not move.
        mat->setValue(QStringLiteral("baseColor"), QColor(QStringLiteral("#EEF4F8")));
        mat->setValue(QStringLiteral("metallic"), 0.0f);
        mat->setValue(QStringLiteral("roughness"), 0.05f);
        mat->setValue(QStringLiteral("alphaMode"), 3);   // Glass
        mat->setValue(QStringLiteral("alpha"), 0.3f);
        return mat;
    }

    if (shaderGuid == QLatin1String(kMatcap)) {
        // A matcap is a pre-lit sphere image: a SHADING MODEL, not a texture
        // slot, so there is no honest slot to bind matTexture into. The drawer's
        // Silver PBR is what the sample conversion chose for exactly this case
        // and it is what a matcap most often stood in for.
        mat->setValue(QStringLiteral("baseColor"), QColor(QStringLiteral("#F5F5F7")));
        mat->setValue(QStringLiteral("metallic"), 1.0f);
        mat->setValue(QStringLiteral("roughness"), 0.22f);
        return mat;
    }

    if (shaderGuid == QLatin1String(kEdgeMaterial)) {
        // The base colour carries; the fresnel rim (edge_color, fresnelPow)
        // does not, because a rim term is shading and not a material parameter.
        // Anything wanting it back builds it in the graph.
        bool found = false;
        const QColor c = colourOf(values, "color", &found);
        mat->setValue(QStringLiteral("baseColor"), found ? c : QColor(255, 255, 255));
        mat->setValue(QStringLiteral("metallic"), 0.0f);
        mat->setValue(QStringLiteral("roughness"), 0.5f);
        return mat;
    }

    // Default and DefaultAnimated. They differ only in that the animated one
    // was compiled with skinning — which is geometry, not material — so ONE
    // conversion serves both.
    applyDefaultFamily(mat, values, resolveTexture);
    return mat;
}

iris::PbrMaterialPtr fromLegacyValues(const QJsonObject &values,
                                      const std::function<QString(const QString &)> &resolveTexture)
{
    auto mat = iris::PbrMaterial::create();
    applyDefaultFamily(mat, values, resolveTexture);
    return mat;
}

} // namespace BuiltinMaterials

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

    // The LEGACY flavour, through the one conversion. Presets store paths, not
    // asset guids, so the texture resolver is the identity — an existence check
    // would be wrong here (a preset naming a missing file should clear the slot
    // the same way it always did, which setValue's loadTexture already does).
    QJsonObject values;
    values[QStringLiteral("diffuseColor")]   = preset.diffuseColor.name();
    values[QStringLiteral("shininess")]      = double(preset.shininess);
    values[QStringLiteral("normalIntensity")]= double(preset.normalIntensity);
    values[QStringLiteral("textureScale")]   = double(preset.textureScale);
    values[QStringLiteral("diffuseTexture")] = preset.diffuseTexture;
    values[QStringLiteral("normalTexture")]  = preset.normalTexture;
    applyDefaultFamily(mat, values, [](const QString &p) { return p; });
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

QJsonObject normaliseLegacyDefinition(const QJsonObject &definition)
{
    QJsonObject out;
    for (auto it = definition.constBegin(); it != definition.constEnd(); ++it) {
        const QString &key = it.key();
        if (key == QLatin1String("diffuseColor"))        out[QStringLiteral("baseColor")] = it.value();
        else if (key == QLatin1String("color"))          out[QStringLiteral("baseColor")] = it.value();
        else if (key == QLatin1String("diffuseTexture")) out[QStringLiteral("baseColorMap")] = it.value();
        else if (key == QLatin1String("normalTexture"))  out[QStringLiteral("normalMap")] = it.value();
        else if (key == QLatin1String("normalIntensity"))out[QStringLiteral("normalFactor")] = it.value();
        else if (key == QLatin1String("shininess"))
            out[QStringLiteral("roughness")] = double(roughnessFromShininess(float(it.value().toDouble())));
        // Dropped, with no equivalent: ambientColor, specularColor,
        // specularTexture, reflectionTexture, reflectionInfluence, useAlpha's
        // shader-side meaning. See applyDefaultFamily for why.
        else if (key == QLatin1String("ambientColor") || key == QLatin1String("specularColor") ||
                 key == QLatin1String("specularTexture") || key == QLatin1String("reflectionTexture") ||
                 key == QLatin1String("reflectionInfluence"))
            continue;
        else out[key] = it.value();
    }
    // An explicit PBR spelling always wins over one derived from a legacy key.
    for (const char *pbrKey : { "baseColor", "baseColorMap", "normalMap", "normalFactor", "roughness" })
        if (definition.contains(QLatin1String(pbrKey))) out[QLatin1String(pbrKey)] = definition.value(QLatin1String(pbrKey));
    return out;
}

}
