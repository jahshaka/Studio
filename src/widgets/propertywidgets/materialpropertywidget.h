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
#include "../../src/graphics/material.h"

namespace iris {
    class SceneNode;
    class MeshNode;
    class Material;
    class CustomMaterial;
}

class MaterialReader;

/**
 *  Displays properties for materials
 */
class MaterialPropertyWidget : public AccordianBladeWidget
{
    Q_OBJECT

public:
    MaterialPropertyWidget(QSharedPointer<iris::SceneNode> sceneNode, QWidget *parent = nullptr);

    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);
    QSharedPointer<iris::MeshNode> meshNode;

    QSharedPointer<iris::CustomMaterial> customMaterial;

    MaterialReader* materialReader;
    void createWidgets(const QJsonObject &jahShader);

    void setupDefaultMaterial();
    void setupCustomMaterial();
    void setupShaderSelector();

protected slots:
    void onCustomSliderChanged(QWidget*);
    void onCustomColorChanged(QWidget*);
    void onCustomTextureChanged(QWidget*);

    void onMaterialSelectorChanged(const QString&);

private:
    ComboBoxWidget* materialSelector;

    std::map<QString, HFloatSliderWidget*> materialSliders;

    std::vector<iris::MatStruct<HFloatSliderWidget*>> sliderUniforms;
    std::vector<iris::MatStruct<ColorValueWidget*>> colorUniforms;
    std::vector<iris::MatStruct<TexturePickerWidget*>> textureUniforms;
};

#endif // MATERIALPROPERTYWIDGET_H
