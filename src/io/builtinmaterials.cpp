/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "io/builtinmaterials.h"
#include "app/qtstdgui.h"

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
        mat->setValue("baseColor",           qtstd::colourValue(preset.baseColor));
        mat->setValue("metallic",            preset.metallic);
        mat->setValue("roughness",           preset.roughness);
        mat->setValue("roughnessLowerBound", preset.roughnessLowerBound);
        mat->setValue("roughnessUpperBound", preset.roughnessUpperBound);
        mat->setValue("normalFactor",        preset.pbrNormalFactor);
        mat->setValue("emissiveColor",       qtstd::colourValue(preset.emissiveColor));
        mat->setValue("emissiveIntensity",   preset.emissiveIntensity);
        mat->setValue("textureScale",        preset.textureScale);
        mat->setValue("alphaMode",           preset.alphaMode);
        mat->setValue("alpha",               preset.alpha);
        mat->setValue("alphaCutoff",         preset.alphaCutoff);
        mat->setValue("baseColorMap",  iris::Value(preset.baseColorMap.toStdString()));
        mat->setValue("metallicMap",   iris::Value(preset.metallicMap.toStdString()));
        mat->setValue("roughnessMap",  iris::Value(preset.roughnessMap.toStdString()));
        mat->setValue("normalMap",     iris::Value(preset.pbrNormalMap.toStdString()));
        mat->setValue("emissiveMap",   iris::Value(preset.emissiveMap.toStdString()));
        return mat;
    }

    return mat;
}

}
