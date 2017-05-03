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
#include <QSharedPointer>

#include <QJsonObject>
#include <QSignalMapper>
#include <QLayout>

#include "../accordianbladewidget.h"
#include "../../irisgl/src/graphics/material.h"
#include "../../irisgl/src/materials/propertytype.h"

namespace iris {
    class SceneNode;
    class MeshNode;
    class Material;
    class CustomMaterial;
}

class MaterialReader;
class PropertyWidget;

/**
 *  Displays properties for materials
 */
class MaterialPropertyWidget : public AccordianBladeWidget, iris::PropertyListener
{
    Q_OBJECT

public:
    MaterialPropertyWidget();

    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);
    QSharedPointer<iris::MeshNode> meshNode;

    QSharedPointer<iris::CustomMaterial> customMaterial;

    MaterialReader* materialReader;

    void setupCustomMaterial();
    void setupShaderSelector();

    void forceShaderRefresh(const QString &matName);


protected slots:
    void onMaterialSelectorChanged(const QString&);

private:
    ComboBoxWidget* materialSelector;

public:
    void onPropertyChanged(iris::Property*);
    PropertyWidget* materialPropWidget;
    void handleMat();
};

#endif // MATERIALPROPERTYWIDGET_H
