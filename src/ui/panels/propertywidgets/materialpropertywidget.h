/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPROPERTYWIDGET_H
#define MATERIALPROPERTYWIDGET_H

#include <QWidget>

#include "io/assetmanager.h"
#include "ui/controls/accordionbladewidget.h"
#include "irisgl/document/materials/material.h"
#include "irisgl/core/properties/property.h"

namespace iris {
    class SceneNode;
    class MeshNode;
    class Material;
    class CustomMaterial;
}

class PropertyWidget;
class Database;
struct StudioServices;

/**
 *  Displays properties for materials
 */
class MaterialPropertyWidget : public AccordianBladeWidget, iris::PropertyListener
{
    Q_OBJECT

public:
    MaterialPropertyWidget() = default;

    // The shader-graph material, when the mesh carries one. Its panel offers the
    // shader selector and the generate/purge machinery, none of which exists on
    // the Material base class.
    QSharedPointer<iris::CustomMaterial> material;

    // Any other Material subclass (PbrMaterial, DefaultMaterial...). These get a
    // plain parameter list rendered from Material::properties - no shader
    // selector, since they are not authored by the shader graph.
    iris::MaterialPtr genericMaterial;

    // Whichever of the two is currently set, as a base pointer.
    // Defined in the .cpp: CustomMaterial is only forward-declared here, so the
    // derived-to-base conversion is not visible at this point.
    iris::MaterialPtr currentMaterial() const;

    void setSceneNode(iris::SceneNodePtr sceneNode);
    void forceShaderRefresh(const QString&);
    void setWidgetProperties();

    void setServices(StudioServices *s) { services = s; }
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
    StudioServices *services = nullptr;
    QString meshNodeGuid;
    QMap<QString, QString> existingTextures;
};

#endif // MATERIALPROPERTYWIDGET_H
