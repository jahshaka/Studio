/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALWIDGET_H
#define MATERIALWIDGET_H

#include <QWidget>
#include <QMap>
#include "../materials/materialpresets.h"

namespace Ui {
class MaterialWidget;
}

namespace Qt3DRender
{
    class QMaterial;
}

class SceneNode;
enum class MaterialType;
class MaterialProxy;
class MaterialData;
class QLabel;

class MaterialWidget;
class AdvanceMaterial;
class QListWidgetItem;

class PredefinedMaterial:public QObject
{
    Q_OBJECT
public:
    PredefinedMaterial():
        QObject()
    {

    }

    virtual void apply(SceneNode* node, MaterialWidget* widget)
    {
        Q_UNUSED(node);
        Q_UNUSED(widget);
    }
};

class ShinyRedMaterial:public PredefinedMaterial
{
    Q_OBJECT

public:
    void apply(SceneNode* node, MaterialWidget* widget) override;
};

class MaterialWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MaterialWidget(QWidget *parent = 0);
    ~MaterialWidget();

    //not owned by this class
    SceneNode* node;
    MaterialProxy* matProxy;
    //MaterialData* matData;//and this?
    void setSceneNode(SceneNode* node);
    void setMaterial(AdvanceMaterial* mat);

    void setShininess(float);

    //textures
    void setDiffuseMap(QString path);
    void setNormalMap(QString path);
    void setSpecularMap(QString path);
    void setReflectionMap(QString path);

    QString getMaterialName(MaterialType type);

private slots:
    //void materialChanged(QString);

    void setAmbient(QColor col);
    void setDiffuse(QColor col);
    void setSpecular(QColor col);

    void normalIntensityChanged(int val);
    void shininessChanged(int val);
    void alphaChanged(int val);
    void reflectionFactorChanged(int val);

    void changeDiffuseMap();
    void changeSpecularMap();
    void changeNormalMap();
    void changeReflectionMap();

    void clearDiffuseMap();
    void clearSpecularMap();
    void clearNormalMap();
    void clearReflectionMap();
    //void initMaterialProxy(Qt3DRender::QMaterial* mat,MaterialType matType);
    //void choosePredefMaterial();

    void changeTextureScale(int val);

    void materialPresetChanged(QListWidgetItem* item);
private:

    /// \brief Sets label's image from specified file
    /// if file is null or empty then the label will be cleared
    /// \param label
    /// \param file
    void setLabelImage(QLabel* label,QString file);

    void clearLabelImage(QLabel* label);

    /**
     * @brief Updates ui to reflect values from material
     * @param material
     */
    void updateUI(AdvanceMaterial* material);

    void addPreset(MaterialPreset preset);

    QString loadTexture();

    Ui::MaterialWidget *ui;

    QString specularTex;
    QString diffuseTex;
    QString normalTex;

    void setMaterialData(MaterialData* data);
    MaterialData* getMaterialData();

    AdvanceMaterial* material;

    //material presets
    QMap<QString,MaterialPreset> presets;
};

#endif // MATERIALWIDGET_H
