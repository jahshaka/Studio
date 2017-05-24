/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPROPERTYWIDGET_H
#define MATERIALPROPERTYWIDGET_H

#include <QWidget>

#include "../../io/assetmanager.h"
#include "../accordianbladewidget.h"
#include "../../irisgl/src/graphics/material.h"
#include "../../irisgl/src/materials/propertytype.h"

namespace iris {
    class SceneNode;
    class MeshNode;
    class Material;
    class CustomMaterial;
}

class PropertyWidget;

/**
 *  Displays properties for materials
 */
class MaterialPropertyWidget : public AccordianBladeWidget, iris::PropertyListener
{
    Q_OBJECT

public:
    MaterialPropertyWidget() = default;
    QSharedPointer<iris::CustomMaterial> material;

    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);
    void forceShaderRefresh(const QString&);
    void setWidgetProperties();


protected slots:
    void materialChanged(const QString&);

private:
    QSharedPointer<iris::MeshNode> meshNode;
    ComboBoxWidget* materialSelector;
    PropertyWidget* materialPropWidget;

    void setupShaderSelector();
    void onPropertyChanged(iris::Property*);
};

#endif // MATERIALPROPERTYWIDGET_H
