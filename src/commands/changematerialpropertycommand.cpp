/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "commands/changematerialpropertycommand.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/materials/material.h"


ChangeMaterialPropertyCommand::ChangeMaterialPropertyCommand(iris::MaterialPtr material, QString name, QVariant oldValue, QVariant newValue)
{
    this->material = material;
    propName = name;
    this->newValue = newValue;
    this->oldValue = oldValue;
}

void ChangeMaterialPropertyCommand::undo()
{
    setMaterialProperty(propName, oldValue);
}

void ChangeMaterialPropertyCommand::redo()
{
    setMaterialProperty(propName, newValue);
}

void ChangeMaterialPropertyCommand::setMaterialProperty(QString name, QVariant value)
{
    if (!material)
        return;

    // ONE PATH since HLMS_ADOPTION P4b. The CustomMaterial branch that used to
    // sit here resolved textures through the shader's uniform table and asserted
    // on an unknown property name; there is no uniform table and no
    // CustomMaterial any more.
    // PbrMaterial (and any future material) bridges by name — its setValue
    // carries the value onto the real fields the shader reads, textures as paths.
    material->setValue(name, value);
}
