/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "export/walkers/materialtexturereader.h"

#include "irisgl/document/materials/material.h"
#include "irisgl/document/assets/livetextures.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/properties/property.h"
#include "app/qtstd.h"

namespace exportwalk {

QVector<TextureSlot> materialTextureSlots(iris::Material *material)
{
    QVector<TextureSlot> found;
    if (!material) return found;

    auto seen = [&found](const QString &source) {
        for (const TextureSlot &s : found)
            if (s.source == source) return true;
        return false;
    };

    // A LIVE TEXTURE IS NOT AN EXPORTABLE DEPENDENCY (MATERIAL_GAPS_SPEC A-1):
    // its "source" is a session reference, not a file, so an archive or a glTF
    // that named it would carry a path to nothing. Every walker that asks this
    // question is asking WHICH FILES TRAVEL WITH THE PROJECT, and the honest
    // answer for pixels that only exist while the app runs is "none".
    for (auto it = material->textures.cbegin(); it != material->textures.cend(); ++it) {
        if (!it->second || it->second->source.empty()) continue;
        if (iris::LiveTextures::isLiveRef(it->second->source)) continue;
        found.append({ QString::fromStdString(it->first), QString::fromStdString(it->second->source) });
    }

    // Texture-typed PROPERTY rows, for anything the slot map above missed. A
    // PbrMaterial keeps both in step through setValue, so this is normally a
    // no-op; it was the only route a CustomMaterial's maps could be found by
    // (HLMS_ADOPTION P4b) and it stays as the belt to that brace.
    for (iris::Property *prop : material->properties()) {
        if (!prop || prop->type != iris::PropertyType::Texture) continue;
        const QString path = qtstd::toQString(static_cast<const iris::TextureProperty *>(prop)->value);
        if (path.isEmpty() || seen(path) || iris::LiveTextures::isLiveRef(path.toStdString())) continue;
        found.append({ qtstd::toQString(prop->name), path });
    }
    return found;
}

QString textureSlotSource(iris::Material *material, const char *slot)
{
    if (!material) return QString();
    auto it = material->textures.find(slot);
    if (it != material->textures.cend() && it->second &&
        !iris::LiveTextures::isLiveRef(it->second->source))
        return QString::fromStdString(it->second->source);
    return QString();
}

} // namespace exportwalk
