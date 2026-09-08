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
#include <QVector>

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
class CameraPostFxPropertyWidget;
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
class WorldModesPropertyWidget;
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
	class WorldPostFxPropertyWidget *worldPostFxPropView = nullptr;
	WorldAaPropertyWidget *worldAaPropView;
	WorldModesPropertyWidget *worldModesPropView;
	WorldShadowPropertyWidget *worldShadowPropView;

public slots:
	void acceptCubemapTexturesFromSkyPresets(QStringList guids);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    /// Logs (once per offender) when the panel's minimum width does not fit the
    /// dock it is scrolled in — the shape of failure that made the World
    /// sections look empty in 2026-09-08: rows laid out past the right edge of
    /// a scroll area with no horizontal bar, present and unreachable.
    void warnIfWiderThanDock();
    QString lastWidthWarning;

	StudioServices *services = nullptr;
	Project *project = nullptr;
    void clearLayout(QLayout*);

    /// The blades this panel owns permanently (everything built in the
    /// constructor). SELECTION COST, 2026-09-08: a selection change moves
    /// blades in and out of the LAYOUT and never in and out of the widget
    /// HIERARCHY — see clearLayout()'s comment for the regression that shape
    /// fixes.
    QVector<QWidget *> bladeWidgets() const;
    /// Makes a blade a permanent hidden child of this panel. Called once per
    /// blade, ever.
    void adoptBlade(QWidget *blade);
    /// Adds an adopted blade to the layout and shows it.
    void mount(QWidget *blade);

private:
    QSharedPointer<iris::SceneNode> sceneNode;

public:
    WorldPropertyWidget *getWorldPropertyWidget() const { return worldPropView; }

private:
    AccordianBladeWidget* transformPropView;
    TransformEditor* transformWidget;

    /// Built on the first mesh selection and REUSED (it used to be rebuilt per
    /// selection and orphaned, which leaked a whole widget tree every time).
    MaterialPropertyWidget* materialPropView = nullptr;
    EmitterPropertyWidget* emitterPropView;
    /// The camera panel's "Exposure & Post" section (CAMERA_LENS_SPEC §4/§5).
    CameraPostFxPropertyWidget* cameraPostFxPropView;
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

    Database *db = nullptr;   // was uninitialized: the ctor forwards it to panels before setDatabase()
	ShaderPropertyWidget *shaderPropView;
    IEditorViewport *sceneView = nullptr;   // was uninitialized: read before setSceneView() on some paths

    QWidget *widgetProperty;
    QVBoxLayout *widgetPropertyLayout;
};

#endif // PROPERTYWIDGET_H
