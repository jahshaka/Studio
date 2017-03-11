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
#include "../accordianbladewidget.h"

#include <QJsonObject>
#include <QSignalMapper>
#include <QLayout>

namespace iris {
    class SceneNode;
    class MeshNode;
    class Material;
    class DefaultMaterial;
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

    QSharedPointer<iris::DefaultMaterial> material;
    QSharedPointer<iris::CustomMaterial> customMaterial;

    MaterialReader* materialReader;
    void parseJahShader(const QJsonObject &jahShader);

    void setupDefaultMaterial();
    void setupCustomMaterial();
    void setupShaderSelector();

protected slots:
    void onAmbientColorChanged(QColor color);

    void onDiffuseColorChanged(QColor color);
    void onDiffuseTextureChanged(QString texture);

    void onSpecularColorChanged(QColor color);
    void onSpecularTextureChanged(QString texture);
    void onShininessChanged(float shininess);

    void onNormalTextureChanged(QString texture);
    void onNormalIntensityChanged(float intensity);

    void onReflectionTextureChanged(QString texture);
    void onReflectionInfluenceChanged(float intensity);

    void onTextureScaleChanged(float scale);
    void onCustomSliderChanged(QWidget *t);

    void onMaterialSelectorChanged(const QString&);

private:
    QString currentMaterial;
    ComboBoxWidget* materialSelector;

    ColorValueWidget* ambientColor;

    ColorValueWidget* diffuseColor;
    TexturePickerWidget* diffuseTexture;

    ColorValueWidget* specularColor;
    TexturePickerWidget* specularTexture;
    HFloatSliderWidget* shininess;

    TexturePickerWidget* normalTexture;
    HFloatSliderWidget* normalIntensity;

    TexturePickerWidget* reflectionTexture;
    HFloatSliderWidget* reflectionInfluence;

    HFloatSliderWidget* textureScale;

    // 2
    int allocated;
    HFloatSliderWidget* customSliders[12];
};

#endif // MATERIALPROPERTYWIDGET_H
