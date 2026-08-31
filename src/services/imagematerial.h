/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef IMAGEMATERIAL_H
#define IMAGEMATERIAL_H

// The ONE image→PBR-material builder (IMAGE_PLANE_SPEC §2/§3).
//
// Shared by SceneEditService::addImagePlane (Option A — the anonymous
// in-document material on a dropped image plane) and the Option B material
// asset creation (materials.createFromImage + the automatic companion
// material on a direct image add-to-project). Owner-approved defaults (§8):
// roughness 1, metallic 0, and images WITH an alpha channel default to
// alphaMode 2 (BLEND — "the material carries the image's true alpha";
// cutout stays a manual option in the panel).

#include <QString>

#include "irisgl/irisglfwd.h"

class Database;
class Project;

namespace ImageMaterial
{

/// Resolves the texture's bytes (pin-first through the CAS; falls back to
/// the library source, so it works in both project and store context) and
/// builds the standard image PBR material with the file as baseColorMap.
/// Returns null when the guid resolves to no readable file.
/// `resolvedPathOut`/`hasAlphaOut` report what the builder found.
iris::PbrMaterialPtr fromTexture(const QString &textureGuid, Database *db,
                                 Project *project,
                                 QString *resolvedPathOut = nullptr,
                                 bool *hasAlphaOut = nullptr);

/// Option B1: serializes the standard image material as a Material ASSET —
/// a LIBRARY row (materialType "pbr", values.baseColorMap = the texture's
/// GUID so readers resolve pin-first), a Material→Texture dependency row,
/// and a thumbnail scaled straight from the image (headless-safe, no
/// engine). Callers in project context pin it via ProjectAssets::
/// addToProject so it lands in the bin. Returns the new material guid,
/// empty on failure.
QString createMaterialAsset(const QString &textureGuid, Database *db,
                            Project *project, QString *errorOut = nullptr);

/// True when a Material asset already depends on this texture — the
/// idempotence guard for the automatic companion material (a re-add of the
/// same image must not mint a second material).
bool hasCompanionMaterial(const QString &textureGuid);

} // namespace ImageMaterial

#endif // IMAGEMATERIAL_H
