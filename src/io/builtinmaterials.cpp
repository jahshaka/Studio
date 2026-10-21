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
#include <QJsonValue>

#include "irisgl/document/materials/pbrmaterial.h"
#include "data/materialpreset.h"


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
