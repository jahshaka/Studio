/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef EXPORT_MATERIALTEXTUREREADER_H
#define EXPORT_MATERIALTEXTUREREADER_H

// Material texture enumeration for exporters (ASSET_PIPELINE_SPEC §3.3).
//
// Every iris::Material carries its textures in the `textures` slot map
// (slot uniform name -> Texture2D); CustomMaterial additionally references
// textures through Texture-typed properties. Exporters that need "every
// texture this material uses" (manifests, dependency gathering, inventory)
// enumerate through here; exporters with per-slot semantics (glTF's channel
// packing) read single slots via textureSlotSource.

#include <QString>
#include <QVector>

namespace iris {
class Material;
}

namespace exportwalk {

struct TextureSlot
{
    QString role;     // the document slot name ("u_baseColorMap", …) or the
                      // CustomMaterial property name ("diffuseTexture", …)
    QString source;   // texture source path (file or Qt resource), never empty
};

/// Every non-empty texture the material references: the slot map first (in map
/// order), then any Texture-typed CustomMaterial properties not already seen.
QVector<TextureSlot> materialTextureSlots(iris::Material *material);

/// One slot's source path from the material's texture map ("" when the slot is
/// absent or holds no texture) — the glTF writer's per-slot read.
QString textureSlotSource(iris::Material *material, const char *slot);

} // namespace exportwalk

#endif // EXPORT_MATERIALTEXTUREREADER_H
