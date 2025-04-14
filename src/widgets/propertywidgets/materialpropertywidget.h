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
#include "../../irisgl/src/core/property.h"

namespace iris {
    class SceneNode;
    class MeshNode;
    class Material;
    class CustomMaterial;
}

class PropertyWidget;
class Database;

/**
 *  Displays properties for materials
 */
class MaterialPropertyWidget : public AccordianBladeWidget, iris::PropertyListener
{
    Q_OBJECT

public:
    MaterialPropertyWidget() = default;
    QSharedPointer<iris::CustomMaterial> material;

    void setSceneNode(iris::SceneNodePtr sceneNode);
    void forceShaderRefresh(const QString&);
    void setWidgetProperties();

    void setDatabase(Database *db) {
        this->db = db;
    }


protected slots:
    void materialChanged(int);
    void materialChanged(const QString&);

private:
    QSharedPointer<iris::MeshNode> meshNode;
    ComboBoxWidget* materialSelector;
    PropertyWidget* materialPropWidget;

    void setupShaderSelector();
    void onPropertyChanged(iris::Property*) override;
    void onPropertyChangeStart(iris::Property*) override;
    void onPropertyChangeEnd(iris::Property*) override;

    // for undo/redo
    QVariant startValue;
    Database *db;
    QString meshNodeGuid;
    QMap<QString, QString> existingTextures;
};

#endif // MATERIALPROPERTYWIDGET_H
