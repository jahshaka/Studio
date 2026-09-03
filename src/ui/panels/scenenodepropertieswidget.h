/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

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
class DecalPropertyWidget;
class FogPropertyWidget;
class EmitterPropertyWidget;
class NodePropertyWidget;
class MeshPropertyWidget;
class PhysicsPropertyWidget;
class DemoPane;
class IEditorViewport;
struct StudioServices;
class Database;
class Project;

// These are special and a kind of hack since this widget was never really designed to work with non scenenode types
class ShaderPropertyWidget;
class SkyPropertyWidget;
class WorldSkyPropertyWidget;
class WorldGiPropertyWidget;
class WorldAaPropertyWidget;
class WorldShadowPropertyWidget;

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
	void setSceneView(IEditorViewport *sceneView);
	void setServices(StudioServices *services);

    /**
     * Updates material properties if active scene node is a mesh
     */
    void refreshMaterial(const QString &matName);

	void refreshTransform();

    void setDatabase(Database*);

    /// Forwards the one live Project to every property panel that reads it
    /// (Phase 4: was the Globals::project static). Panels created lazily
    /// (materialPropView) get it at construction.
    void setProject(Project*);

	WorldSkyPropertyWidget *worldSkyPropView;
	WorldGiPropertyWidget *worldGiPropView;
	WorldAaPropertyWidget *worldAaPropView;
	WorldShadowPropertyWidget *worldShadowPropView;

public slots:
	void acceptCubemapTexturesFromSkyPresets(QStringList guids);

private:
	StudioServices *services = nullptr;
	Project *project = nullptr;
    void clearLayout(QLayout*);

private:
    QSharedPointer<iris::SceneNode> sceneNode;

public:
    WorldPropertyWidget *getWorldPropertyWidget() const { return worldPropView; }

private:
    AccordianBladeWidget* transformPropView;
    TransformEditor* transformWidget;

    MaterialPropertyWidget* materialPropView;
    EmitterPropertyWidget* emitterPropView;
    // NodePropertyWidget* nodePropView;
    LightPropertyWidget* lightPropView;
    DecalPropertyWidget* decalPropView;
    WorldPropertyWidget* worldPropView;
    FogPropertyWidget*  fogPropView;
	SkyPropertyWidget *skyPropView;
	MeshPropertyWidget* meshPropView;
    PhysicsPropertyWidget *physicsPropView;
    DemoPane* demoPane;

    QSharedPointer<iris::Scene> scene;

    Database *db;
	ShaderPropertyWidget *shaderPropView;
    IEditorViewport *sceneView = nullptr;   // was uninitialized: read before setSceneView() on some paths

    QWidget *widgetProperty;
    QVBoxLayout *widgetPropertyLayout;
};

#endif // PROPERTYWIDGET_H
