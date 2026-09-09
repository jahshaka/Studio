/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef BUILTINMATERIALS_H
#define BUILTINMATERIALS_H

#include <QJsonObject>
#include <QString>
#include <functional>

#include "irisgl/irisglfwd.h"

struct MaterialPreset;
namespace iris { struct MeshMaterialData; }

/// THE BUILTIN RETIREMENT (HLMS_ADOPTION_SPEC P4b = ENGINEERING_DEBT item 2).
///
/// Jahshaka shipped six "builtin shaders" — Default, DefaultAnimated,
/// EdgeMaterial, Flat, Glass, Matcap — as `app/shader_defs/*.shader` files
/// behind reserved GUIDs, and every scene that used one stored the GUID plus a
/// `values{}` block of the shader's own uniform names. The GLSL half of that
/// pipeline died with the materials evaluator; the class that carried it
/// (iris::CustomMaterial) died here. What is left is a translation problem:
/// **an old scene naming a reserved GUID must open, and must look right.**
///
/// This is that translation, in ONE place. The reserved GUIDs survive — they
/// still name a builtin — but a builtin is now a **PbrMaterial preset**, not a
/// shader definition.
///
/// WHAT IS HONESTLY LOST, and it is not nothing:
///   * `ambientColor`, `specularColor`, `specularTexture`, `reflectionTexture`
///     and `reflectionInfluence` have no metallic-roughness home at all. They
///     were ALREADY lost on the renderer — the mirror never read one of them —
///     so this makes an existing silent loss explicit rather than causing it.
///   * EdgeMaterial's fresnel rim (`edge_color`, `fresnelPow`) is a shading
///     effect, not a material parameter. Its base `color` carries over; the rim
///     does not. A rim light belongs to the graph, or to a shading model.
///   * Matcap's whole point is a pre-lit sphere image, which is a shading
///     model and not a texture slot. It converts to the neutral metal preset —
///     the same call the 2026-08-31 sample conversion already made by hand.
namespace BuiltinMaterials
{
/// A stored texture reference plus the PBR SLOT it is being bound into, to an
/// absolute file path. See fromBuiltin for why the slot travels with the value.
using TextureResolver = std::function<QString(const QString &stored, const QString &slotName)>;

/// Is this one of the six reserved builtin GUIDs?
bool isBuiltin(const QString &shaderGuid);

/// The display name a reserved GUID now stands for ("Default", "Flat", ...);
/// empty for anything else.
QString builtinName(const QString &shaderGuid);

/// Build the PbrMaterial a reserved builtin GUID now means, with the saved
/// `values{}` block applied on top of the preset.
///
/// `resolveTexture(stored, slotName)` turns a stored texture reference (an asset
/// GUID, or a path) into an absolute file path — the caller owns that, because
/// the two readers resolve textures differently (project pins vs scene-relative
/// paths). It may be empty, in which case no texture is bound.
///
/// `slotName` IS THE PBR SLOT THE VALUE IS BOUND INTO — `baseColorMap`,
/// `normalMap`, `emissiveMap` — and not the legacy key it was read from
/// (`diffuseTexture` and friends). It exists because the guid alone is no
/// longer enough to name a texture: an imported model collapsed every slot onto
/// the one OBJECT guid, and `AssetCas::textureGuidForSlot` needs the slot's
/// role words to say which member texture was meant (the 2026-09-03 save
/// defect, SceneReader::repairTextureSlot). A resolver that does not care may
/// ignore it.
///
/// NEVER NULL for a reserved GUID: a scene that names a builtin gets the
/// equivalent preset, never a load failure.
iris::PbrMaterialPtr fromBuiltin(const QString &shaderGuid, const QJsonObject &values,
                                 const TextureResolver &resolveTexture);

/// The fallback for a NON-reserved legacy shader material — an imported
/// `.shader` asset whose GLSL is long gone, or a shader GUID whose definition
/// cannot be found. Maps whatever recognisable legacy uniform names the saved
/// `values{}` block carries (diffuseColor, shininess, diffuseTexture, ...) onto
/// PBR rows and ignores the rest.
///
/// This is the reason the reader cannot fail: with iris::CustomMaterial gone
/// there is no "some other material class" to fall back to, so the fallback has
/// to be a PbrMaterial that carries across everything that still has a meaning.
iris::PbrMaterialPtr fromLegacyValues(const QJsonObject &values,
                                      const TextureResolver &resolveTexture);

/// A drawer/library PRESET (`app/content/materials/*.material`) as a material.
///
/// Presets come in two flavours on disk — `material_type: "PBR"` and the older
/// Default-shader flavour with ambient/diffuse/specular/shininess — and BOTH
/// now produce a PbrMaterial. The legacy flavour goes through exactly the same
/// conversion a legacy SCENE material does, so a preset and a saved material
/// carrying the same values cannot end up looking different.
///
/// It lives here rather than beside MaterialPreset because it is the same
/// translation as everything else in this file, and it had two independent
/// copies (the asset panel's preset registration and SceneEditService's
/// preset apply) that were already drifting.
iris::PbrMaterialPtr fromPreset(const MaterialPreset &preset);

/// The material an IMPORTED mesh's assimp material data becomes.
///
/// Real glTF metallic-roughness is used when the source carried it
/// (MeshMaterialData::hasPbr); otherwise the legacy Blinn fields go through the
/// same shininess remap as everything else here. Five separate copies of this
/// lambda existed across the import, session-restore, add-mesh and thumbnail
/// paths, each building a Default-shader CustomMaterial slightly differently.
iris::PbrMaterialPtr fromMeshData(const iris::MeshMaterialData &data);

/// Renames the legacy Default-shader keys in a saved `.material` DEFINITION to
/// their PbrMaterial equivalents, so a caller can drive a PbrMaterial's declared
/// rows straight off it. Keys with no equivalent (ambient, specular, reflection)
/// are dropped; keys already in PBR spelling pass through untouched.
///
/// This is the shape the importer and the asset panel need: they walk a
/// material's property rows and pick the definition's matching values out, and
/// what changed in P4b is only WHICH rows those are.
QJsonObject normaliseLegacyDefinition(const QJsonObject &definition);
}

#endif // BUILTINMATERIALS_H
