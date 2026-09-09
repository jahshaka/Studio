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

    // ONE material, ONE path (HLMS_ADOPTION P4b). This widget used to carry a
    // CustomMaterial member AND a generic one, with a dynamicCast choosing
    // between them and every method branching on which was set — a split that
    // existed only because a shader-graph material was a different class. It is
    // not any more: every material a mesh can carry is rendered from
    // Material::properties.
    iris::MaterialPtr material;

    /// Kept as the name the rest of the widget reads through.
    iris::MaterialPtr currentMaterial() const { return material; }

    void setSceneNode(iris::SceneNodePtr sceneNode);
    void forceShaderRefresh(const QString&);
    void setWidgetProperties();

    void setServices(StudioServices *s) { services = s; }
    void setDatabase(Database *db) {
        this->db = db;
    }


    /// The texture paths the CURRENT material had when this panel adopted it —
    /// the "before" side of updateTextureDependency's project bookkeeping.
    /// Public so the blade-reuse contract is assertable: the properties panel
    /// keeps its blades as hidden children and reuses them across selections,
    /// so this must be a snapshot of ONE material and never an accumulation.
    const QMap<QString, QString> &shownTextures() const { return existingTextures; }

protected slots:
    void materialChanged(int);
    void materialChanged(const QString&);

private:
    QSharedPointer<iris::MeshNode> meshNode;
    ComboBoxWidget* materialSelector;
    PropertyWidget* materialPropWidget;

    void setupShaderSelector();
    /// Re-reads `existingTextures` from the material currently shown (empty when
    /// there is none). Called wherever the shown material changes.
    void snapshotTextures();
    void updateTextureDependency(iris::Property*);
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
