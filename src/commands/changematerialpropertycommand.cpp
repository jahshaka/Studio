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
#include "irisgl/document/materials/custommaterial.h"


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
    // CustomMaterial keeps its historical path: textures resolve through the
    // uniform table, everything else lands on the Property object.
    if (auto custom = material.dynamicCast<iris::CustomMaterial>()) {
        iris::Property* prop = nullptr;
        for (auto property : custom->properties) {
            if (property->name == name)
                prop = property;
        }

        Q_ASSERT(prop);

        // special case for textures since we have to generate these
        if (prop->type == iris::PropertyType::Texture) {
            custom->setTextureWithUniform(prop->uniform, value.toString());
        } else {
            prop->setValue(value);
        }
        return;
    }

    // PbrMaterial (and any future material) bridges by name — its setValue
    // carries the value onto the real fields the shader reads, textures as paths.
    material->setValue(name, value);
}
