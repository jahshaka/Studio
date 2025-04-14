/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENENODEPROPERTYWIDGET_H
#define SCENENODEPROPERTYWIDGET_H

#include <QWidget>
#include <QListWidgetItem>
#include <QVBoxLayout>
#include <QSharedPointer>

namespace iris {
    class SceneNode;
}

class AccordianBladeWidget;
class TransformEditor;
class MaterialPropertyWidget;
class WorldPropertyWidget;
class LightPropertyWidget;
class FogPropertyWidget;
class EmitterPropertyWidget;
class NodePropertyWidget;
class MeshPropertyWidget;
class PhysicsPropertyWidget;
class HandPropertyWidget;
class DemoPane;
class SceneViewWidget;
class Database;

// These are special and a kind of hack since this widget was never really designed to work with non scenenode types
class ShaderPropertyWidget;
class SkyPropertyWidget;
class WorldSkyPropertyWidget;

/**
 * This class shows the properties of selected nodes in the scene
 */
class SceneNodePropertiesWidget : public QWidget
{
    Q_OBJECT
public:
    SceneNodePropertiesWidget(QWidget *parent = nullptr);

    /**
     * sets active scene node to show properties for
     * @param sceneNode
     */

    void setScene(QSharedPointer<iris::Scene> scene);
    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);
    void setAssetItem(QListWidgetItem *item);
	void setSceneView(SceneViewWidget *sceneView);

    /**
     * Updates material properties if active scene node is a mesh
     */
    void refreshMaterial(const QString &matName);

	void refreshTransform();

    void setDatabase(Database*);

	WorldSkyPropertyWidget *worldSkyPropView;

public slots:
	void acceptCubemapTexturesFromSkyPresets(QStringList guids);

private:
    void clearLayout(QLayout*);

private:
    QSharedPointer<iris::SceneNode> sceneNode;

    AccordianBladeWidget* transformPropView;
    TransformEditor* transformWidget;

    MaterialPropertyWidget* materialPropView;
    EmitterPropertyWidget* emitterPropView;
    // NodePropertyWidget* nodePropView;
    LightPropertyWidget* lightPropView;
    WorldPropertyWidget* worldPropView;
    FogPropertyWidget*  fogPropView;
	SkyPropertyWidget *skyPropView;
	MeshPropertyWidget* meshPropView;
	HandPropertyWidget* handPropView;
    PhysicsPropertyWidget *physicsPropView;
    DemoPane* demoPane;

    QSharedPointer<iris::Scene> scene;

    Database *db;
	ShaderPropertyWidget *shaderPropView;
    SceneViewWidget *sceneView;

    QWidget *widgetProperty;
    QVBoxLayout *widgetPropertyLayout;
};

#endif // PROPERTYWIDGET_H
