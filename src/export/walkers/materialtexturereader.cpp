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
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/core/properties/property.h"

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

    for (auto it = material->textures.constBegin(); it != material->textures.constEnd(); ++it) {
        if (!it.value() || it.value()->source.isEmpty()) continue;
        found.append({ it.key(), it.value()->source });
    }

    if (auto *custom = dynamic_cast<iris::CustomMaterial *>(material)) {
        for (iris::Property *prop : custom->properties) {
            if (!prop || prop->type != iris::PropertyType::Texture) continue;
            const QString path = prop->getValue().toString();
            if (path.isEmpty() || seen(path)) continue;
            found.append({ prop->name, path });
        }
    }
    return found;
}

QString textureSlotSource(iris::Material *material, const char *slot)
{
    if (!material) return QString();
    auto it = material->textures.constFind(slot);
    if (it != material->textures.constEnd() && it.value()) return it.value()->source;
    return QString();
}

} // namespace exportwalk
