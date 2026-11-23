/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/changematerialpropertycommand.h"
#include "irisgl/document/properties/property.h"
#include "irisgl/document/materials/material.h"


ChangeMaterialPropertyCommand::ChangeMaterialPropertyCommand(iris::MaterialPtr material, std::string name,
                                                             iris::Value oldValue, iris::Value newValue)
    : material(std::move(material)), propName(std::move(name)), oldValue(std::move(oldValue)), newValue(std::move(newValue))
{
}

void ChangeMaterialPropertyCommand::undo()
{
    setMaterialProperty(oldValue);
}

void ChangeMaterialPropertyCommand::redo()
{
    setMaterialProperty(newValue);
}

void ChangeMaterialPropertyCommand::setMaterialProperty(const iris::Value &value)
{
    if (!material)
        return;

    // ONE PATH since HLMS_ADOPTION P4b. The CustomMaterial branch that used to
    // sit here resolved textures through the shader's uniform table and asserted
    // on an unknown property name; there is no uniform table and no
    // CustomMaterial any more.
    // PbrMaterial (and any future material) bridges by name — its setValue
    // carries the value onto the real fields the shader reads, textures as paths.
    // Its answer is not read: both Values were built of the row's kind by the
    // verb or the panel that pushed this command (the edge, qtstd::toRowValue).
    material->setValue(propName, value);
}
