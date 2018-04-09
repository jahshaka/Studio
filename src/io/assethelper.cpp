/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "assethelper.h"

#include "irisgl/src/core/property.h"
#include "irisgl/src/materials/custommaterial.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/meshnode.h"

// Thanks to Qt not allowing updating its json values and instead returning temp objects
// This class updates a meshnode with the values in a material definition
// We handle special cases such as textures that need to be matched with guids
// and colors that need to be converted from hex, the rest can be implicitly set
void AssetHelper::updateNodeMaterial(iris::SceneNodePtr &node, const QJsonObject definition)
{
    if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
        auto materialDefinition = definition.value("material").toObject();
        auto nodeMaterial = node.staticCast<iris::MeshNode>()->getMaterial().staticCast<iris::CustomMaterial>();

        for (const iris::Property* property : nodeMaterial->properties) {
            if (property->type == iris::PropertyType::Texture) {
                if (!materialDefinition.value(property->name).toString().isEmpty()) {
                    nodeMaterial->setValue(property->name, materialDefinition.value(property->name).toString());
                }
            }
            else if (property->type == iris::PropertyType::Color) {
                nodeMaterial->setValue(
                    property->name,
                    QVariant::fromValue(materialDefinition.value(property->name).toVariant().value<QColor>())
                );
            }
            else {
                nodeMaterial->setValue(property->name, QVariant::fromValue(materialDefinition.value(property->name)));
            }
        }
    }

    QJsonArray children = definition["children"].toArray();
    // These will always be in sync since the definition is derived from the mesh
    if (node->hasChildren()) {
        for (int i = 0; i < node->children.count(); i++) {
            updateNodeMaterial(node->children[i], children[i].toObject());
        }
    }
}