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

#include "irisgl/irisglfwd.h"

struct MaterialPreset;
namespace iris { struct MeshMaterialData; }

/// The two conversions INTO a PbrMaterial that are not a saved definition: a
/// shipped PRESET and an IMPORTED mesh's material data. (The pre-PBR builtin
/// shaders, their reserved guids and the legacy `values{}` readers are gone —
/// FORWARD-ONLY-1: a material that is not stamped `materialType: "pbr"` is
/// refused by the reader, never converted.)
namespace BuiltinMaterials
{
/// A drawer/library PRESET (`app/content/materials/*-pbr.material`) as a
/// material. Only the PBR flavour exists; any other preset type yields the
/// default PbrMaterial under the preset's name.
iris::PbrMaterialPtr fromPreset(const MaterialPreset &preset);

/// The material an IMPORTED mesh's assimp material data becomes: real glTF
/// metallic-roughness when the source carried it (MeshMaterialData::hasPbr),
/// otherwise the Blinn fields through the shininess remap.
iris::PbrMaterialPtr fromMeshData(const iris::MeshMaterialData &data);
}

#endif // BUILTINMATERIALS_H
